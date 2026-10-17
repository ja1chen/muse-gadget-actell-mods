/* SPDX-License-Identifier: Apache-2.0 */
/* Public synthetic protocol/fault model; real pairing worker + codec, fake I/O. */
#include "actell_lan.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
enum {
    HAPPY,
    APPROVED_WAIT,
    REJECT,
    EXPIRE,
    WRONG_TOKEN,
    PIN_CHANGE,
    SAVE_FAIL,
    LOST_ACK,
    REVOKED,
    ACTIVE_WRITE_FAIL,
    CANCEL_REQUEST,
    CLAIM_HOST_CHANGE,
    INCOMPATIBLE,
    CANCEL_ACK,
    MULTIPLE,
    NO_CANDIDATES,
    CANCEL_DISCOVERY
};
static bool auto_pref = true, pref_fail;
esp_err_t actell_auto_connect_load(bool *v) { *v = auto_pref; return ESP_OK; }
esp_err_t actell_auto_connect_save(bool v) { if(pref_fail) return ESP_FAIL; auto_pref=v; return ESP_OK; }
static int scenario, posts, hello, writes, phase, has_record, ack_lost, forgets;
static unsigned wakeups;
void actell_bridge_pair_wake(void) { wakeups++; }
static bool clock_ready = true, fail_load;
static int64_t clock_us;
static actell_config saved;
static char rid[37], last_token[44];
int64_t esp_timer_get_time(void) { return clock_us; }
void vTaskDelay(unsigned ms) { clock_us += (int64_t)ms * 1000; }
void esp_fill_random(void *p, size_t n) { memset(p, 0x42, n); }
void actell_random_uuid(char out[37]) { strcpy(out, "11111111-1111-4111-8111-111111111111"); }
bool actell_clock_ready(void) { return clock_ready; }
cJSON *cJSON_CreateObject(void) { return calloc(1, sizeof(cJSON)); }
void cJSON_Delete(cJSON *p) { free(p); }
void cJSON_AddNumberToObject(cJSON *j, const char *k, double n) {
    (void)j;
    (void)k;
    (void)n;
}
void cJSON_AddStringToObject(cJSON *j, const char *k, const char *s) {
    (void)j;
    if (!strcmp(k, "request_id"))
        strcpy(rid, s);
    if (!strcmp(k, "device_name")) assert(!strcmp(s,"Muse Gadget"));
}
char *cJSON_PrintUnformatted(const cJSON *j) {
    (void)j;
    char *body = malloc(128);
    snprintf(body, 128, "{\"v\":1,\"request_id\":\"%s\",\"device_name\":\"Muse Gadget\"}", rid);
    return body;
}
esp_err_t actell_peer_load(actell_config *c, actell_peer_phase *p) {
    if (fail_load) return ESP_FAIL;
    if (!has_record)
        return ESP_ERR_NOT_FOUND;
    *c = saved;
    *p = phase;
    return ESP_OK;
}
esp_err_t actell_peer_save(const actell_config *c, actell_peer_phase p) {
    writes++;
    if (scenario == SAVE_FAIL || (scenario == ACTIVE_WRITE_FAIL && p == ACTELL_PEER_ACTIVE))
        return ESP_FAIL;
    saved = *c;
    has_record = 1;
    phase = p;
    return ESP_OK;
}
esp_err_t actell_peer_forget(void) {
    forgets++;
    has_record = 0;
    slan_zero(&saved, sizeof(saved));
    return ESP_OK;
}
esp_err_t actell_lan_discover(actell_candidate *c, unsigned *n, const actell_config *p) {
    (void)p;
    if (scenario == INCOMPATIBLE) { *n = 0; return ESP_ERR_NOT_SUPPORTED; }
    if (scenario == NO_CANDIDATES) { *n=0; return ESP_ERR_NOT_FOUND; }
    if (scenario == CANCEL_DISCOVERY) actell_bridge_pair_cancel();
    strcpy(c[0].name, "Synthetic Actell");
    strcpy(c[0].host, "actell-fixture.local");
    strcpy(c[0].address, "192.168.1.2");
    c[0].port = 8765;
    *n = scenario == MULTIPLE ? 2 : 1;
    if (*n==2) { c[1]=c[0]; strcpy(c[1].host,"another-fixture.local"); }
    return ESP_OK;
}
bool actell_bridge_local_job(unsigned a, unsigned i) {
    actell_pair_process(a, i);
    return true;
}
esp_err_t actell_pair_post(const actell_candidate *c, actell_config *anchor, const char *path,
                           const char *token, const char *body, int *status, char reply[4097]) {
    posts++;
    assert(!strcmp(c->host, "actell-fixture.local"));
    assert(strlen(token) == 43);
    assert(strstr(body, token) == NULL);
    slan_message m;
    bool claim = !strcmp(path, "/v1/pair/claim");
    assert(slan_decode(claim ? SLAN_CLAIM : SLAN_REQUEST, body, strlen(body), &m));
    assert(!strcmp(m.request_id, rid));
    if (posts == 1) {
        strcpy(last_token, token);
        memset(anchor->fingerprint, 'a', 64);
        anchor->fingerprint[64] = 0;
        strcpy(anchor->pem, "synthetic test public cert");
    } else
        assert(!strcmp(last_token, token));
    if (scenario == CANCEL_REQUEST) {
        actell_bridge_pair_cancel();
        return ESP_FAIL;
    }
    if (claim && (scenario == REJECT || scenario == EXPIRE || scenario == WRONG_TOKEN)) {
        *status = scenario == REJECT ? 403 : scenario == EXPIRE ? 410 : 401;
        snprintf(reply, 4097, "{\"v\":1,\"error\":\"%s\"}",
                 scenario == REJECT   ? "rejected"
                 : scenario == EXPIRE ? "expired"
                                      : "unauthorized");
        return ESP_OK;
    }
    if (!claim) {
        *status = 202;
        snprintf(reply, 4097,
                 "{\"v\":1,\"request_id\":\"%s\",\"state\":\"pending\",\"retry_after_s\":2}", rid);
        return ESP_OK;
    }
    if (scenario == APPROVED_WAIT && posts == 2) {
        *status = 202;
        snprintf(reply,4097,"{\"v\":1,\"request_id\":\"%s\",\"state\":\"approved\",\"retry_after_s\":2}",rid);
        return ESP_OK;
    }
    if (scenario == APPROVED_WAIT) {
        actell_pair_status view; actell_bridge_pair_status(&view);
        assert(strstr(view.message,"Retrieving connection"));
    }
    *status = 200;
    char secret[44];
    unsigned char raw[32];
    memset(raw, 0x11, 32);
    slan_secret_encode(raw, secret);
    snprintf(reply, 4097,
             "{\"v\":1,\"request_id\":\"%s\",\"state\":\"approved\",\"client_id\":\"22222222-2222-"
             "4222-8222-222222222222\",\"secret\":\"%s\",\"host\":\"%s\","
             "\"port\":8765,\"certificate_sha256\":\"%s\",\"claim_remaining_s\":300}",
             rid, secret, scenario == CLAIM_HOST_CHANGE ? "other.local" : "actell-fixture.local",
             scenario == PIN_CHANGE
                 ? "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
                 : anchor->fingerprint);
    return ESP_OK;
}
esp_err_t actell_pair_hello(const actell_config *c, bool *rejected) {
    hello++;
    if (scenario == APPROVED_WAIT) {
        actell_pair_status view; actell_bridge_pair_status(&view);
        assert(strstr(view.message,"Confirming pairing"));
    }
    assert(has_record);
    assert(phase == ACTELL_PEER_PENDING || phase == ACTELL_PEER_ACTIVE);
    assert(!strcmp(c->client, saved.client));
    assert(!memcmp(c->secret, saved.secret, 32));
    *rejected = false;
    if (scenario == CANCEL_ACK) { actell_bridge_pair_cancel(); return ESP_FAIL; }
    if (scenario == REVOKED) {
        *rejected = true;
        return ESP_FAIL;
    }
    if (scenario == LOST_ACK && !ack_lost++) {
        return ESP_FAIL;
    }
    return ESP_OK;
}
static void reset(int s) {
    auto_pref = true; pref_fail = false;
    scenario = s;
    posts = hello = writes = has_record = ack_lost = forgets = 0;
    clock_ready = true; fail_load = false;
    clock_us = 0;
    actell_pair_boot();
}
static void run(int s) {
    reset(s);
    assert(!actell_bridge_pair_action(ACTELL_PAIR_SELECT, 0));
    assert(actell_bridge_pair_action(ACTELL_PAIR_SCAN, 0));
    assert(!actell_bridge_pair_action(ACTELL_PAIR_SELECT, 0));
}
int main(void) {
    run(APPROVED_WAIT);
    assert(posts == 3 && hello == 1 && phase == ACTELL_PEER_ACTIVE);
    run(HAPPY);
    actell_pair_status view;
    actell_bridge_pair_status(&view);
    assert(view.paired && !view.pending && !view.busy && view.connected);
    assert(!strcmp(actell_bridge_pair_summary(&view), "Connected"));
    assert(posts == 2 && hello == 1 && writes == 2 && phase == ACTELL_PEER_ACTIVE &&
           actell_pair_rpc_allowed());
    /* A failed read-only hello changes reachability, not the saved identity. */
    scenario = LOST_ACK; ack_lost = 0; clock_us += 31000000;
    actell_pair_poll();
    actell_bridge_pair_status(&view);
    assert(view.paired && !view.connected && view.connection_checked);
    assert(!strcmp(actell_bridge_pair_summary(&view), "Offline"));
    scenario = HAPPY; clock_us += 31000000;
    actell_pair_poll();
    actell_bridge_pair_status(&view);
    assert(view.connected && !strcmp(actell_bridge_pair_summary(&view), "Connected"));
    /* Off persists across boot and suppresses only background hello. Manual
     * reconnect still works; on wakes recovery without issuing any action. */
    actell_config identity = saved;
    int old_posts = posts, old_writes = writes;
    assert(actell_bridge_pair_action(ACTELL_PAIR_AUTO_CONNECT, 0));
    actell_pair_boot();
    int old_hello = hello;
    clock_us += 31000000; actell_pair_poll();
    actell_pair_clock_synchronized(); actell_pair_poll();
    assert(hello == old_hello && !auto_pref && !memcmp(&identity,&saved,sizeof(saved)));
    actell_bridge_pair_status(&view);
    assert(!view.auto_connect && !strcmp(actell_bridge_pair_summary(&view),"Saved"));
    assert(actell_bridge_pair_action(ACTELL_PAIR_RETRY, 0));
    assert(hello == old_hello + 1 && !auto_pref);
    pref_fail = true;
    assert(actell_bridge_pair_action(ACTELL_PAIR_AUTO_CONNECT, 1));
    actell_bridge_pair_status(&view); assert(!view.auto_connect);
    pref_fail = false;
    assert(actell_bridge_pair_action(ACTELL_PAIR_AUTO_CONNECT, 1));
    actell_pair_poll();
    assert(hello == old_hello + 2 && auto_pref && posts == old_posts);
    assert(writes == old_writes && !memcmp(&identity,&saved,sizeof(saved)));
    /* Automatic rejection stops retries and preserves the record. */
    scenario = REVOKED; clock_us += 31000000; actell_pair_poll();
    old_hello = hello; clock_us += 31000000; actell_pair_poll();
    actell_pair_clock_synchronized(); actell_pair_poll();
    assert(has_record && hello == old_hello && !actell_pair_rpc_allowed());
    /* Reconnect rejection must never route ACTIVE through pending deletion. */
    int saved_writes = writes;
    assert(actell_bridge_pair_action(ACTELL_PAIR_RETRY, 0));
    actell_bridge_pair_status(&view);
    assert(has_record && !forgets && phase==ACTELL_PEER_ACTIVE);
    assert(view.paired && !view.pending && !view.connected && !actell_pair_rpc_allowed());
    assert(!strcmp(actell_bridge_pair_summary(&view),"Offline"));
    assert(strstr(view.message,"Pairing kept") && !memcmp(&identity,&saved,sizeof(saved)));
    assert(writes==saved_writes);
    scenario=HAPPY;
    assert(actell_bridge_pair_action(ACTELL_PAIR_RETRY,0));
    assert(has_record && !forgets && writes==saved_writes && actell_pair_rpc_allowed());
    /* A clock failure also preserves the reconnect control and stored identity. */
    clock_ready=false; int before_hello=hello;
    assert(actell_bridge_pair_action(ACTELL_PAIR_RETRY,0));
    actell_bridge_pair_status(&view);
    assert(view.paired && !view.pending && hello==before_hello && !forgets);
    assert(!memcmp(&identity,&saved,sizeof(saved))); clock_ready=true;
    unsigned before_wake=wakeups;
    actell_pair_clock_synchronized(); actell_pair_poll();
    assert(wakeups==before_wake+1 && hello==before_hello+1);
    actell_bridge_pair_status(&view); assert(view.connected);
    /* Repeated sync while connected does not cause redundant hellos. */
    actell_pair_clock_synchronized(); actell_pair_poll();
    assert(hello==before_hello+1 && !memcmp(&identity,&saved,sizeof(saved)));
    fail_load=true;
    assert(actell_bridge_pair_action(ACTELL_PAIR_RETRY,0));
    actell_bridge_pair_status(&view);
    assert(view.paired && !view.pending && has_record && !forgets && !actell_pair_rpc_allowed());
    assert(strstr(view.message,"Pairing kept"));
    fail_load=false;
    assert(actell_bridge_pair_action(ACTELL_PAIR_RETRY,0));
    assert(actell_pair_rpc_allowed() && writes==saved_writes && !forgets);
    /* Cancellation after a saved ACTIVE connection stops I/O only. */
    scenario=CANCEL_ACK;
    assert(actell_bridge_pair_action(ACTELL_PAIR_RETRY,0));
    actell_bridge_pair_status(&view);
    assert(view.paired && !view.pending && !forgets && writes==saved_writes);
    assert(!memcmp(&identity,&saved,sizeof(saved)));
    scenario=HAPPY;
    int after_cancel_hello=hello;
    actell_pair_clock_synchronized(); actell_pair_poll();
    assert(hello==after_cancel_hello && !forgets);
    assert(actell_bridge_pair_action(ACTELL_PAIR_AUTO_CONNECT,1));
    actell_pair_poll(); assert(actell_pair_rpc_allowed() && !forgets);
    for (int s = REJECT; s <= PIN_CHANGE; s++) {
        run(s);
        assert(!has_record && !hello && !writes && !actell_pair_rpc_allowed());
    }
    run(SAVE_FAIL);
    assert(writes == 1 && !has_record && !hello);
    run(LOST_ACK);
    assert(hello == 2 && writes == 2 && phase == ACTELL_PEER_ACTIVE);
    run(REVOKED);
    assert(hello == 1 && has_record && phase==ACTELL_PEER_PENDING && !forgets && !actell_pair_rpc_allowed());
    actell_bridge_pair_status(&view);
    assert(view.paired && view.pending && !view.connected);
    scenario=HAPPY; assert(actell_bridge_pair_action(ACTELL_PAIR_RETRY,0));
    assert(phase==ACTELL_PEER_ACTIVE && !forgets && actell_pair_rpc_allowed());
    run(CANCEL_ACK);
    assert(has_record && phase==ACTELL_PEER_PENDING && !forgets && !actell_pair_rpc_allowed());
    actell_bridge_pair_status(&view); assert(view.paired && view.pending);
    scenario=HAPPY; assert(actell_bridge_pair_action(ACTELL_PAIR_RETRY,0));
    assert(phase==ACTELL_PEER_ACTIVE && !forgets);
    run(ACTIVE_WRITE_FAIL);
    assert(has_record && phase == ACTELL_PEER_PENDING && !actell_pair_rpc_allowed());
    actell_bridge_pair_status(&view);
    assert(view.paired && view.pending && !view.busy && !view.connected);
    assert(!strcmp(actell_bridge_pair_summary(&view), "Finish pairing"));
    scenario = REVOKED; clock_us += 31000000;
    actell_pair_poll();
    assert(has_record && phase == ACTELL_PEER_PENDING && !actell_pair_rpc_allowed());
    scenario = HAPPY;
    hello = 0;
    actell_pair_boot();
    actell_pair_poll();
    assert(hello == 1 && phase == ACTELL_PEER_ACTIVE && actell_pair_rpc_allowed());
    assert(actell_bridge_pair_action(ACTELL_PAIR_FORGET, 0));
    assert(!has_record && forgets==1 && !actell_pair_rpc_allowed());
    actell_bridge_pair_status(&view);
    assert(!view.paired && !view.pending && !view.connected);
    assert(!strcmp(actell_bridge_pair_summary(&view), "Not paired"));
    view.busy = true;
    assert(!strcmp(actell_bridge_pair_summary(&view), "Pairing"));
    view.paired = true; view.pending = false; view.connection_checked = false;
    assert(!strcmp(actell_bridge_pair_summary(&view), "Connecting"));
    run(CANCEL_REQUEST);
    assert(!has_record && !hello && !writes);
    run(CLAIM_HOST_CHANGE);
    assert(!has_record && !hello && !writes);
    reset(INCOMPATIBLE);
    assert(actell_bridge_pair_action(ACTELL_PAIR_SCAN, 0));
    actell_pair_status incompatible;
    actell_bridge_pair_status(&incompatible);
    assert(!incompatible.count && strstr(incompatible.message, "Incompatible Actell version"));
    assert(!posts && !hello && !writes);
    /* A single initial button automatically requests; no second user selection.
     * Ambiguity/cancel/no candidates must send nothing to any phone. */
    run(MULTIPLE);
    actell_bridge_pair_status(&view);
    assert(!posts && !hello && !writes && !has_record && !view.count);
    assert(strstr(view.message,"Multiple Actell pairing windows"));
    run(NO_CANDIDATES);
    assert(!posts && !hello && !writes && !has_record);
    run(CANCEL_DISCOVERY);
    assert(!posts && !hello && !writes && !has_record);
    run(HAPPY);
    assert(posts==2 && hello==1 && has_record && phase==ACTELL_PEER_ACTIVE);
    actell_config stable=saved; int before_posts=posts;
    assert(!actell_bridge_pair_action(ACTELL_PAIR_SCAN,0));
    assert(posts==before_posts && !memcmp(&stable,&saved,sizeof(saved)));
    /* Exercise the real bounded parser with deterministic hostile bytes. */
    unsigned seed=17;char fuzz[4097];slan_message parsed;
    for(unsigned i=0;i<20000;i++){
        size_t n=i%sizeof(fuzz);
        for(size_t j=0;j<n;j++){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;fuzz[j]=(char)seed;}
        (void)slan_decode((slan_kind)(i%5),fuzz,n,&parsed);
    }
    puts("PASS real SLAN worker: "
         "allow/reject/expire/token/pin/storage/lost-ACK/reboot/cancel/delete");
    return 0;
}
