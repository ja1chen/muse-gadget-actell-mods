/* SPDX-License-Identifier: Apache-2.0 */
#include "actell_lan.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>

static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static actell_pair_status state;
static bool running, cancelled, rpc_allowed, booted, reconnect_blocked;
static int64_t last_probe;
static atomic_bool clock_changed;
void actell_pair_clock_synchronized(void) {
    /* SNTP thread only posts a wakeup; network I/O stays on the worker. */
    atomic_store(&clock_changed, true);
    actell_bridge_pair_wake();
}
static void message(const char *s) {
    portENTER_CRITICAL(&lock);
    snprintf(state.message, sizeof(state.message), "%s", s);
    state.revision++;
    portEXIT_CRITICAL(&lock);
}
static void connection_result(bool connected, bool checked) {
    portENTER_CRITICAL(&lock);
    state.connected = connected;
    state.connection_checked = checked;
    state.revision++;
    portEXIT_CRITICAL(&lock);
}
const char *actell_bridge_pair_summary(const actell_pair_status *s) {
    if (!s->paired) return s->busy ? "Pairing" : "Not paired";
    if (s->pending) return s->busy ? "Connecting" : "Finish pairing";
    if (s->connected) return "Connected";
    if (!s->connection_checked && !s->auto_connect && !s->busy) return "Saved";
    return s->busy || !s->connection_checked ? "Connecting" : "Offline";
}
bool actell_pair_current(void) {
    portENTER_CRITICAL(&lock);
    bool ok = running && !cancelled;
    portEXIT_CRITICAL(&lock);
    return ok;
}
bool actell_pair_rpc_allowed(void) {
    portENTER_CRITICAL(&lock);
    bool ok = rpc_allowed;
    portEXIT_CRITICAL(&lock);
    return ok;
}
void actell_bridge_pair_status(actell_pair_status *out) {
    portENTER_CRITICAL(&lock);
    *out = state;
    portEXIT_CRITICAL(&lock);
}
void actell_bridge_pair_cancel(void) {
    portENTER_CRITICAL(&lock);
    cancelled = true;
    reconnect_blocked = true;
    state.count = 0;
    state.revision++;
    portEXIT_CRITICAL(&lock);
}
bool actell_bridge_pair_action(actell_pair_action action, unsigned index) {
    if (action < ACTELL_PAIR_SCAN || action > ACTELL_PAIR_AUTO_CONNECT ||
        (action == ACTELL_PAIR_AUTO_CONNECT && index > 1) || action == ACTELL_PAIR_SELECT)
        return false;
    portENTER_CRITICAL(&lock);
    bool ok = booted && !state.busy && !(action == ACTELL_PAIR_SCAN && state.paired);
    if (ok) {
        state.busy = true;
        if (action == ACTELL_PAIR_RETRY) {
            state.connected = false;
            state.connection_checked = false;
        }
        cancelled = false;
        if (action != ACTELL_PAIR_SCAN && action != ACTELL_PAIR_AUTO_CONNECT)
            rpc_allowed = false;
        state.revision++;
    }
    portEXIT_CRITICAL(&lock);
    if (!ok)
        return false;
    if (actell_bridge_local_job(action, index))
        return true;
    portENTER_CRITICAL(&lock);
    state.busy = false;
    portEXIT_CRITICAL(&lock);
    message("Connection worker unavailable");
    return false;
}
static void finish(bool paired, bool allowed, bool pending) {
    portENTER_CRITICAL(&lock);
    state.busy = false;
    state.paired = paired;
    state.pending = paired && pending;
    if (!paired) {
        state.connected = false;
        state.connection_checked = false;
    }
    rpc_allowed = allowed;
    running = false;
    state.revision++;
    portEXIT_CRITICAL(&lock);
}
static bool wait_seconds(unsigned sec) {
    int64_t end = esp_timer_get_time() + sec * 1000000LL;
    while (esp_timer_get_time() < end) {
        if (!actell_pair_current())
            return false;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    return actell_pair_current();
}
static bool acknowledge(actell_config *c, bool pending, unsigned attempts, int64_t end) {
    connection_result(false, false);
    message(pending ? "Approval received. Confirming pairing with Actell" : "Connecting to Actell");
    for (unsigned i = 0; i < attempts && actell_pair_current() && esp_timer_get_time() < end; i++) {
        bool rejected = false;
        esp_err_t e = actell_pair_hello(c, &rejected);
        if (e == ESP_OK && actell_pair_current()) {
            if (pending && actell_peer_save(c, ACTELL_PEER_ACTIVE) != ESP_OK) {
                connection_result(false, true);
                message("Could not save connection");
                return false;
            }
            connection_result(true, true);
            message("Connected to Actell.");
            return true;
        }
        if (rejected) {
            connection_result(false, true);
            reconnect_blocked = true;
            message("Connection refused. Pairing kept. Check Actell, then tap Reconnect.");
            return false;
        }
        if (i + 1 < attempts && !wait_seconds(2u << i))
            break;
    }
    connection_result(false, true);
    if (actell_pair_current())
        message(pending ? "Pairing saved. Tap Complete pairing."
                        : "Connection unavailable. Pairing kept. Tap Reconnect.");
    return false;
}
static bool pair(const actell_candidate *, actell_config *);
static bool scan(actell_config *c) {
    message("Looking for Actell pairing window");
    unsigned count = 0;
    actell_candidate *found = calloc(SLAN_CANDIDATES, sizeof(*found));
    if (!found) {
        message("Not enough memory");
        return false;
    }
    esp_err_t e = actell_lan_discover(found, &count, NULL);
    if (!actell_pair_current()) { free(found); return false; }
    if (e == ESP_OK && count == 1) {
        actell_candidate target = found[0];
        free(found);
        /* The iPhone approves the Muse request. Never interpret the service
         * instance label as a physical phone name or ask for a second tap. */
        return pair(&target, c);
    }
    message(e == ESP_OK && count > 1
        ? "Multiple Actell pairing windows. Keep Add Muse open on one iPhone only."
        : e == ESP_ERR_NOT_SUPPORTED ? "Incompatible Actell version. Update iPhone app."
        : "On iPhone, enable Muse connections and tap Add Muse. Then try again.");
    free(found);
    return false;
}
static bool pair(const actell_candidate *candidate, actell_config *c) {
    actell_candidate target = *candidate;
    if (!actell_clock_ready()) {
        message("Time not synchronized");
        return false;
    }
    char id[37], token[44], *reply = calloc(1, 4097);
    uint8_t random[32];
    slan_message *m = calloc(1, sizeof(*m));
    if (!reply || !m) {
        free(reply);
        free(m);
        message("Not enough memory");
        return false;
    }
    actell_random_uuid(id);
    esp_fill_random(random, 32);
    slan_secret_encode(random, token);
    slan_zero(random, 32);
    cJSON *j = cJSON_CreateObject();
    cJSON_AddNumberToObject(j, "v", 1);
    cJSON_AddStringToObject(j, "request_id", id);
    cJSON_AddStringToObject(j, "device_name", ACTELL_DEVICE_NAME);
    char *request = cJSON_PrintUnformatted(j);
    cJSON_Delete(j);
    char claim[96];
    snprintf(claim, sizeof(claim), "{\"v\":1,\"request_id\":\"%s\"}", id);
    snprintf(c->origin, sizeof(c->origin), "https://%s:%u", target.host, target.port);
    bool requested = false, saved = false, active = false, approved = false;
    unsigned backoff = 2;
    int64_t deadline = esp_timer_get_time() + 120000000;
    message("Approve this Muse in Actell on iPhone");
    while (request && actell_pair_current() && esp_timer_get_time() < deadline) {
        int status = 0;
        esp_err_t e =
            actell_pair_post(&target, c, requested ? "/v1/pair/claim" : "/v1/pair/request", token,
                             requested ? claim : request, &status, reply);
        if (e != ESP_OK) {
            if (c->fingerprint[0]) {
                actell_candidate *relocated = calloc(SLAN_CANDIDATES, sizeof(*relocated));
                unsigned n = 0;
                if (relocated && actell_lan_discover(relocated, &n, c) == ESP_OK && n &&
                    !strcmp(relocated[0].host, target.host) && relocated[0].port == target.port)
                    strcpy(target.address, relocated[0].address);
                free(relocated);
            }
            if (!wait_seconds(backoff))
                break;
            backoff = backoff < 8 ? backoff * 2 : 8;
            continue;
        }
        backoff = 2;
        if (status == 202) {
            if (!slan_decode(SLAN_PENDING, reply, strlen(reply), m) || strcmp(m->request_id, id)) {
                message("Invalid pairing response");
                break;
            }
            if (!strcmp(m->state, "approved") && !approved) {
                approved = true;
                message("Approval received. Retrieving connection from Actell");
                deadline = esp_timer_get_time() + 300000000;
            }
            requested = true;
            if (!wait_seconds(2))
                break;
            continue;
        }
        if (status == 200 && requested) {
            if (!slan_decode(SLAN_APPROVED, reply, strlen(reply), m) ||
                !slan_claim_matches(m, id, target.host, target.port, c->fingerprint) ||
                !slan_secret_decode(m->secret, c->secret)) {
                message("Phone identity changed. Pair again.");
                break;
            }
            snprintf(c->origin, sizeof(c->origin), "https://%s:%u", target.host, target.port);
            strcpy(c->client, m->client_id);
            message("Saving connection");
            if (!actell_pair_current())
                break;
            if (actell_peer_save(c, ACTELL_PEER_PENDING) != ESP_OK) {
                message("Could not save connection");
                break;
            }
            saved = true;
            unsigned remaining = m->remaining;
            slan_zero(token, sizeof(token));
            slan_zero(reply, 4097);
            slan_zero(m, sizeof(*m));
            active = acknowledge(c, true, 3, esp_timer_get_time() + (int64_t)remaining * 1000000);
            break;
        }
        if (slan_decode(SLAN_ERROR, reply, strlen(reply), m)) {
            if (status == 403)
                message("iPhone declined the request");
            else if (status == 410)
                message("Pairing expired. Open Add Muse again.");
            else if (status == 429)
                message("Too many requests. Try again later.");
            else if (status == 409)
                message("iPhone is handling another request");
            else if (status == 503)
                message(!strcmp(m->error, "time_unavailable") ? "Time not synchronized"
                                                              : "iPhone could not save connection");
            else
                message("Pairing request was refused");
        } else
            message("Invalid pairing response");
        break;
    }
    if (!saved && actell_pair_current() && esp_timer_get_time() >= deadline)
        message("Pairing expired. Open Add Muse again.");
    slan_zero(token, sizeof(token));
    slan_zero(reply, 4097);
    slan_zero(m, sizeof(*m));
    free(reply);
    free(m);
    free(request);
    return active;
}
void actell_pair_process(unsigned action, unsigned index) {
    portENTER_CRITICAL(&lock);
    running = true;
    portEXIT_CRITICAL(&lock);
    if (action == ACTELL_PAIR_AUTO_CONNECT) {
        esp_err_t e = actell_auto_connect_save(index != 0);
        portENTER_CRITICAL(&lock);
        if (e == ESP_OK) {
            state.auto_connect = index != 0;
            if (index) reconnect_blocked = false;
        }
        state.busy = false;
        running = false;
        state.revision++;
        portEXIT_CRITICAL(&lock);
        if (e != ESP_OK) message("Could not save auto-connect");
        else message(index ? "Auto-connect on. Saved pairing will reconnect."
                           : "Auto-connect off. Tap Reconnect when needed.");
        last_probe = esp_timer_get_time() - 30000000;
        return;
    }
    if (action == ACTELL_PAIR_RETRY || action == ACTELL_PAIR_SCAN)
        reconnect_blocked = false;
    actell_pair_status prior;
    actell_bridge_pair_status(&prior);
    actell_config *c = calloc(1, sizeof(*c));
    if (!c) {
        actell_pair_status snapshot;
        actell_bridge_pair_status(&snapshot);
        message("Not enough memory. Pairing kept.");
        finish(snapshot.paired, false, snapshot.pending);
        return;
    }
    bool active = false;
    actell_peer_phase phase;
    esp_err_t loaded = actell_peer_load(c, &phase);
    if (action == ACTELL_PAIR_FORGET) {
        connection_result(false, true);
        if (actell_peer_forget() == ESP_OK)
            message("Disconnected. Remove Muse on iPhone too.");
        else
            message("Could not remove connection");
    } else if (action == ACTELL_PAIR_SCAN) {
        if (loaded == ESP_OK)
            message("Remove the saved connection before pairing again");
        else
            active = scan(c);
    } else if (action == ACTELL_PAIR_RETRY) {
        if (loaded != ESP_OK)
            message("No saved connection. Tap Connect Actell.");
        else if (!actell_clock_ready())
            message("Time not synchronized");
        else
            active = acknowledge(c, phase == ACTELL_PEER_PENDING, 1, esp_timer_get_time() + 10000000);
    }
    if (!actell_pair_current()) {
        /* Cancel stops transport only; deleting saved trust requires Forget. */
        reconnect_blocked = true;
        message("Connection cancelled. Saved pairing kept.");
    }
    if (action == ACTELL_PAIR_RETRY && !active) connection_result(false, true);
    loaded = actell_peer_load(c, &phase);
    bool paired = loaded == ESP_OK;
    if (!paired && prior.paired && action != ACTELL_PAIR_FORGET) {
        /* A transient read failure is not proof that saved trust was removed. */
        paired = true;
        phase = prior.pending ? ACTELL_PEER_PENDING : ACTELL_PEER_ACTIVE;
        message("Could not read saved pairing. Pairing kept. Tap Reconnect.");
    }
    /* A failed read/delete never reopens RPC. Pending cannot execute. */
    bool allowed = loaded == ESP_OK && phase != ACTELL_PEER_PENDING && action != ACTELL_PAIR_FORGET && !reconnect_blocked;
    if (active)
        allowed = true;
    slan_zero(c, sizeof(*c));
    free(c);
    finish(paired, allowed, paired && phase == ACTELL_PEER_PENDING);
    last_probe = esp_timer_get_time();
}
void actell_pair_boot(void) {
    actell_config *c = calloc(1, sizeof(*c));
    if (!c)
        return;
    bool automatic = false;
    actell_auto_connect_load(&automatic);
    reconnect_blocked = false;
    atomic_store(&clock_changed, false);
    actell_peer_phase phase;
    esp_err_t e = actell_peer_load(c, &phase);
    portENTER_CRITICAL(&lock);
    state.auto_connect = automatic;
    state.paired = e == ESP_OK;
    state.pending = e == ESP_OK && phase == ACTELL_PEER_PENDING;
    state.connected = false;
    state.connection_checked = false;
    rpc_allowed = e == ESP_OK && phase != ACTELL_PEER_PENDING;
    booted = true;
    portEXIT_CRITICAL(&lock);
    message(e != ESP_OK ? "Pair with Actell to get started"
            : automatic ? "Paired. Connecting automatically."
                        : "Pairing saved. Tap Reconnect.");
    slan_zero(c, sizeof(*c));
    free(c);
    last_probe = esp_timer_get_time() - 30000000;
}
void actell_pair_poll(void) {
    int64_t elapsed = esp_timer_get_time() - last_probe;
    portENTER_CRITICAL(&lock);
    if (state.busy) { portEXIT_CRITICAL(&lock); return; }
    bool synchronized = atomic_exchange(&clock_changed, false);
    bool due = elapsed >= 30000000 || (synchronized && !state.connected);
    bool probe = due && state.auto_connect && state.paired && !reconnect_blocked;
    if (probe) {
        state.busy = true;
        running = true;
        cancelled = false;
    }
    portEXIT_CRITICAL(&lock);
    if (!probe)
        return;
    actell_pair_status snapshot;
    actell_bridge_pair_status(&snapshot);
    actell_config *c = calloc(1, sizeof(*c));
    actell_peer_phase phase = snapshot.pending ? ACTELL_PEER_PENDING : ACTELL_PEER_ACTIVE;
    bool paired = snapshot.paired, allowed = actell_pair_rpc_allowed();
    if (c && actell_peer_load(c, &phase) == ESP_OK) {
        paired = true;
        allowed = phase != ACTELL_PEER_PENDING;
        bool rejected = false;
        bool reachable = actell_clock_ready() && actell_pair_hello(c, &rejected) == ESP_OK;
        /* Recover a lost final acknowledgement using only authenticated hello.
         * Never re-request pairing or replay a phone action. */
        if (reachable && phase == ACTELL_PEER_PENDING) {
            reachable = actell_peer_save(c, ACTELL_PEER_ACTIVE) == ESP_OK;
            if (reachable) { phase = ACTELL_PEER_ACTIVE; allowed = true; }
        }
        connection_result(reachable, true);
        if (reachable)
            message("Connected to Actell.");
        else
            message(actell_clock_ready() ? "Phone unavailable. Reconnecting."
                                         : "Time not synchronized");
        if (rejected) {
            allowed = false;
            reconnect_blocked = true;
            message("Connection refused. Check Actell, then tap Reconnect.");
        }
    } else {
        connection_result(false, true);
        message("Could not read saved pairing. Pairing kept. Tap Reconnect.");
        allowed = false;
    }
    if (c) {
        slan_zero(c, sizeof(*c));
        free(c);
    }
    finish(paired, allowed, paired && phase == ACTELL_PEER_PENDING);
    last_probe = esp_timer_get_time();
}
