/* SPDX-License-Identifier: Apache-2.0 */
#include "actell_slan.h"
#include <stdio.h>
#include <string.h>
void slan_zero(void *p, size_t n) {
    volatile unsigned char *v = p;
    while (n--)
        *v++ = 0;
}
bool slan_uuid(const char *s) {
    if (!s || strlen(s) != 36)
        return false;
    for (unsigned i = 0; i < 36; i++) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (s[i] != '-')
                return false;
        } else if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f')))
            return false;
    }
    return true;
}
bool slan_host(const char *s) {
    if (!s)
        return false;
    size_t n = strlen(s);
    if (n < 7 || n > 253 || strcmp(s + n - 6, ".local"))
        return false;
    unsigned label = 0;
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        if (c == '.') {
            if (!label || s[i - 1] == '-')
                return false;
            label = 0;
        } else {
            if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || (c == '-' && label)) ||
                ++label > 63)
                return false;
        }
    }
    return label && s[n - 1] != '-';
}
bool slan_private_ipv4(uint32_t x) {
    return (x >> 24) == 10 || (x >> 20) == 0xac1 || (x >> 16) == 0xc0a8;
}
bool slan_lan_ipv4(uint32_t remote, uint32_t local, uint32_t mask) {
    uint32_t hosts = ~mask;
    return slan_private_ipv4(remote) && slan_private_ipv4(local) && mask && hosts >= 3 &&
           !(hosts & (hosts + 1)) && (remote & mask) == (local & mask) && remote != local &&
           (remote & hosts) != 0 && (remote & hosts) != hosts;
}
static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
void slan_secret_encode(const uint8_t in[32], char out[44]) {
    uint32_t v = 0;
    unsigned bits = 0, j = 0;
    for (unsigned i = 0; i < 32; i++) {
        v = (v << 8) | in[i];
        bits += 8;
        while (bits >= 6) {
            bits -= 6;
            out[j++] = alphabet[(v >> bits) & 63];
        }
    }
    if (bits)
        out[j++] = alphabet[(v << (6 - bits)) & 63];
    out[j] = 0;
}
bool slan_secret_decode(const char *s, uint8_t out[32]) {
    if (!s || strlen(s) != 43)
        return false;
    uint32_t v = 0;
    unsigned bits = 0, j = 0;
    for (unsigned i = 0; i < 43; i++) {
        const char *p = strchr(alphabet, s[i]);
        if (!p)
            return false;
        v = (v << 6) | (unsigned)(p - alphabet);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (j < 32)
                out[j++] = (uint8_t)(v >> bits);
        }
    }
    char check[44];
    slan_secret_encode(out, check);
    bool ok = !strcmp(s, check);
    slan_zero(check, sizeof(check));
    if (!ok)
        slan_zero(out, 32);
    return ok;
}
typedef struct {
    const unsigned char *p;
    size_t n, i;
} reader;
static void ws(reader *r) {
    while (r->i < r->n && strchr(" \r\n\t", r->p[r->i]) && r->p[r->i])
        r->i++;
}
static bool take(reader *r, int c) {
    ws(r);
    if (r->i == r->n || r->p[r->i] != c)
        return false;
    r->i++;
    return true;
}
static int hex(int c) {
    return c >= '0' && c <= '9'   ? c - '0'
           : c >= 'a' && c <= 'f' ? c - 'a' + 10
           : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                  : -1;
}
static bool unicode(reader *r, uint32_t *v) {
    *v = 0;
    for (unsigned j = 0; j < 4; j++) {
        if (r->i == r->n)
            return false;
        int x = hex(r->p[r->i++]);
        if (x < 0)
            return false;
        *v = (*v << 4) | (unsigned)x;
    }
    return true;
}
static bool string(reader *r, char *out, size_t cap) {
    if (!take(r, '"'))
        return false;
    size_t n = 0;
    while (r->i < r->n) {
        uint32_t cp = r->p[r->i++];
        if (cp == '"') {
            out[n] = 0;
            return true;
        }
        if (cp < 32)
            return false;
        if (cp == '\\') {
            if (r->i == r->n)
                return false;
            cp = r->p[r->i++];
            switch (cp) {
            case '"':
            case '\\':
            case '/':
                break;
            case 'b':
                cp = 8;
                break;
            case 'f':
                cp = 12;
                break;
            case 'n':
                cp = 10;
                break;
            case 'r':
                cp = 13;
                break;
            case 't':
                cp = 9;
                break;
            case 'u':
                if (!unicode(r, &cp))
                    return false;
                if (cp >= 0xd800 && cp <= 0xdbff) {
                    uint32_t lo;
                    if (r->n - r->i < 2 || r->p[r->i++] != '\\' || r->p[r->i++] != 'u' ||
                        !unicode(r, &lo) || lo < 0xdc00 || lo > 0xdfff)
                        return false;
                    cp = 0x10000 + ((cp - 0xd800) << 10) + lo - 0xdc00;
                } else if (cp >= 0xdc00 && cp <= 0xdfff)
                    return false;
                break;
            default:
                return false;
            }
        } else if (cp >= 128) {
            unsigned more;
            uint32_t min;
            if (cp >= 0xc2 && cp <= 0xdf) {
                more = 1;
                min = 128;
                cp &= 31;
            } else if (cp >= 0xe0 && cp <= 0xef) {
                more = 2;
                min = 2048;
                cp &= 15;
            } else if (cp >= 0xf0 && cp <= 0xf4) {
                more = 3;
                min = 65536;
                cp &= 7;
            } else
                return false;
            for (unsigned j = 0; j < more; j++) {
                if (r->i == r->n || (r->p[r->i] & 0xc0) != 0x80)
                    return false;
                cp = (cp << 6) | (r->p[r->i++] & 63);
            }
            if (cp < min || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff))
                return false;
        }
        if (!cp)
            return false;
        unsigned char b[4];
        size_t count;
        if (cp < 128) {
            b[0] = cp;
            count = 1;
        } else if (cp < 2048) {
            b[0] = 0xc0 | (cp >> 6);
            b[1] = 0x80 | (cp & 63);
            count = 2;
        } else if (cp < 65536) {
            b[0] = 0xe0 | (cp >> 12);
            b[1] = 0x80 | ((cp >> 6) & 63);
            b[2] = 0x80 | (cp & 63);
            count = 3;
        } else {
            b[0] = 0xf0 | (cp >> 18);
            b[1] = 0x80 | ((cp >> 12) & 63);
            b[2] = 0x80 | ((cp >> 6) & 63);
            b[3] = 0x80 | (cp & 63);
            count = 4;
        }
        if (n + count >= cap)
            return false;
        memcpy(out + n, b, count);
        n += count;
    }
    return false;
}
bool slan_name(const char *s) {
    if (!s || !*s || strlen(s) > 64)
        return false; /* Validate UTF8 using the same decoder, then reject Unicode control code
                         points. */
    char quoted[132], out[65];
    size_t n = 0;
    quoted[n++] = '"';
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        if (*p < 32 || *p == 127 || (*p == 0xc2 && (p[1] >= 0x80 && p[1] <= 0x9f)))
            return false;
        if (*p == '"' || *p == '\\')
            quoted[n++] = '\\';
        quoted[n++] = *p;
    }
    quoted[n++] = '"';
    reader r = {(unsigned char *)quoted, n, 0};
    return string(&r, out, sizeof(out));
}
static bool number(reader *r, unsigned *out) {
    ws(r);
    size_t start = r->i;
    uint64_t n = 0;
    while (r->i < r->n && r->p[r->i] >= '0' && r->p[r->i] <= '9') {
        n = n * 10 + r->p[r->i++] - '0';
        if (n > UINT32_MAX)
            return false;
    }
    if (r->i == start || (r->i - start > 1 && r->p[start] == '0'))
        return false;
    *out = (unsigned)n;
    return true;
}
static bool lower_pin(const char *s) {
    if (strlen(s) != 64)
        return false;
    for (unsigned i = 0; i < 64; i++)
        if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f')))
            return false;
    return true;
}
bool slan_decode(slan_kind kind, const char *raw, size_t n, slan_message *m) {
    if (!m)
        return false;
    memset(m, 0, sizeof(*m));
    if (!raw || !n || n > (kind == SLAN_REQUEST || kind == SLAN_CLAIM ? 512 : 4096))
        return false;
    reader r = {(const unsigned char *)raw, n, 0};
    unsigned seen = 0, v = 0, retry = 0;
    char key[32];
    uint8_t secret[32] = {0};
    const char *keys[] = {"v",
                          "request_id",
                          "device_name",
                          "state",
                          "client_id",
                          "secret",
                          "host",
                          "port",
                          "certificate_sha256",
                          "claim_remaining_s",
                          "retry_after_s",
                          "error"};
    if (!take(&r, '{'))
        goto bad;
    do {
        if (!string(&r, key, sizeof(key)) || !take(&r, ':'))
            goto bad;
        unsigned i;
        for (i = 0; i < 12; i++)
            if (!strcmp(keys[i], key))
                break;
        if (i == 12 || (seen & (1u << i)))
            goto bad;
        seen |= 1u << i;
        switch (i) {
        case 0:
            if (!number(&r, &v))
                goto bad;
            break;
        case 1:
            if (!string(&r, m->request_id, sizeof(m->request_id)))
                goto bad;
            break;
        case 2:
            if (!string(&r, m->device_name, sizeof(m->device_name)))
                goto bad;
            break;
        case 3:
            if (!string(&r, m->state, sizeof(m->state)))
                goto bad;
            break;
        case 4:
            if (!string(&r, m->client_id, sizeof(m->client_id)))
                goto bad;
            break;
        case 5:
            if (!string(&r, m->secret, sizeof(m->secret)))
                goto bad;
            break;
        case 6:
            if (!string(&r, m->host, sizeof(m->host)))
                goto bad;
            break;
        case 7:
            if (!number(&r, &m->port))
                goto bad;
            break;
        case 8:
            if (!string(&r, m->pin, sizeof(m->pin)))
                goto bad;
            break;
        case 9:
            if (!number(&r, &m->remaining))
                goto bad;
            break;
        case 10:
            if (!number(&r, &retry))
                goto bad;
            break;
        case 11:
            if (!string(&r, m->error, sizeof(m->error)))
                goto bad;
            break;
        }
    } while (take(&r, ','));
    if (!take(&r, '}'))
        goto bad;
    ws(&r);
    if (r.i != n || v != 1)
        goto bad;
    unsigned mask = kind == SLAN_REQUEST    ? 7
                    : kind == SLAN_CLAIM    ? 3
                    : kind == SLAN_PENDING  ? 1035
                    : kind == SLAN_APPROVED ? 1019
                                            : 2049;
    if (seen != mask)
        goto bad;
    if (kind != SLAN_ERROR && !slan_uuid(m->request_id))
        goto bad;
    if (kind == SLAN_REQUEST && !slan_name(m->device_name))
        goto bad;
    if (kind == SLAN_PENDING &&
        (retry != 2 || (strcmp(m->state, "pending") && strcmp(m->state, "approved"))))
        goto bad;
    if (kind == SLAN_APPROVED &&
        (strcmp(m->state, "approved") || !slan_uuid(m->client_id) ||
         !slan_secret_decode(m->secret, secret) || !slan_host(m->host) || !m->port ||
         m->port > 65535 || !lower_pin(m->pin) || !m->remaining || m->remaining > 300))
        goto bad;
    if (kind == SLAN_ERROR) {
        const char *codes[] = {"invalid_request", "unauthorized",   "rejected",
                               "conflict",        "busy",           "expired",
                               "rate_limited",    "storage_failed", "time_unavailable"};
        bool ok = false;
        for (unsigned i = 0; i < 9; i++)
            if (!strcmp(m->error, codes[i]))
                ok = true;
        if (!ok)
            goto bad;
    }
    slan_zero(secret, sizeof(secret));
    return true;
bad:
    slan_zero(secret, sizeof(secret));
    slan_zero(m, sizeof(*m));
    return false;
}
bool slan_claim_matches(const slan_message *m, const char *id, const char *host, unsigned port,
                        const char *pin) {
    return m && !strcmp(m->request_id, id) && !strcmp(m->host, host) && m->port == port &&
           !strcmp(m->pin, pin);
}
