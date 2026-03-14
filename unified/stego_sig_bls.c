// SPDX-License-Identifier: Apache-2.0
/**
 * stego_sig_bls.c - BLS backend for the unified stego signature API.
 *
 * BLS has no message recovery: the pHash is prepended to the signature
 * in the payload. The verifier extracts the pHash from the payload
 * and checks the BLS signature against it.
 */

#include "stego_sig.h"
#include <string.h>
#include <relic.h>

/* ------------------------------------------------------------------ */
/* Internal state                                                      */
/* ------------------------------------------------------------------ */

static int s_initialized = 0;

/* ------------------------------------------------------------------ */
/* Scheme information                                                  */
/* ------------------------------------------------------------------ */

const char *stego_scheme_name(void) {
#if FP_PRIME == 381
    return "BLS12-381";
#elif FP_PRIME == 158
    return "BLS-BN158";
#else
    return "BLS-Unknown";
#endif
}

int stego_security_bits(void) {
#if FP_PRIME == 381
    return 120; /* conservative: 117-120 after tower NFS */
#elif FP_PRIME == 158
    return 78;
#else
    return pc_param_level();
#endif
}

int stego_has_message_recovery(void) { return 0; }
int stego_is_post_quantum(void)      { return 0; }

/* ------------------------------------------------------------------ */
/* Size queries (use cached generator points)                          */
/* ------------------------------------------------------------------ */

static int s_sig_bytes = 0;
static int s_pk_bytes = 0;
static int s_sk_bytes = 0;

static void cache_sizes(void) {
    if (s_sig_bytes > 0) return;
    g1_t g1; g1_null(g1); g1_new(g1); g1_get_gen(g1);
    s_sig_bytes = g1_size_bin(g1, 1);
    g1_free(g1);

    g2_t g2; g2_null(g2); g2_new(g2); g2_get_gen(g2);
    s_pk_bytes = g2_size_bin(g2, 1);
    g2_free(g2);

    bn_t n; bn_null(n); bn_new(n); pc_get_ord(n);
    s_sk_bytes = bn_size_bin(n);
    bn_free(n);
}

int stego_sig_bytes(void) { cache_sizes(); return s_sig_bytes; }
int stego_pk_bytes(void)  { cache_sizes(); return s_pk_bytes; }
int stego_sk_bytes(void)  { cache_sizes(); return s_sk_bytes; }

int stego_payload_bytes(int phash_len, int append_pk) {
    cache_sizes();
    int total = phash_len + s_sig_bytes;
    if (append_pk) total += s_pk_bytes;
    return total;
}

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */

int stego_init(void) {
    if (s_initialized) return STEGO_OK;
    if (core_init() != RLC_OK) return STEGO_ERR;
    if (pc_param_set_any() != RLC_OK) {
        core_clean();
        return STEGO_ERR;
    }
    cache_sizes();
    s_initialized = 1;
    return STEGO_OK;
}

void stego_cleanup(void) {
    if (s_initialized) {
        core_clean();
        s_initialized = 0;
    }
}

/* ------------------------------------------------------------------ */
/* Key management                                                      */
/* ------------------------------------------------------------------ */

int stego_keygen(uint8_t *pk, int *pk_len, uint8_t *sk, int *sk_len) {
    bn_t d;
    g2_t q;
    int result = STEGO_ERR;

    bn_null(d); g2_null(q);

    RLC_TRY {
        bn_new(d); g2_new(q);

        if (cp_bls_gen(d, q) == RLC_OK) {
            *sk_len = bn_size_bin(d);
            bn_write_bin(sk, *sk_len, d);

            *pk_len = g2_size_bin(q, 1);
            g2_write_bin(pk, *pk_len, q, 1);

            result = STEGO_OK;
        }
    } RLC_CATCH_ANY {
        result = STEGO_ERR;
    } RLC_FINALLY {
        bn_free(d);
        g2_free(q);
    }
    return result;
}

/* ------------------------------------------------------------------ */
/* Signing                                                             */
/* ------------------------------------------------------------------ */

int stego_sign(uint8_t *payload, int *payload_len,
               const uint8_t *phash, int phash_len,
               const uint8_t *sk, int sk_len,
               const uint8_t *salt, int salt_len,
               int append_pk,
               const uint8_t *pk, int pk_len)
{
    (void)salt; (void)salt_len; /* BLS doesn't use salt */

    bn_t d;
    g1_t sig;
    int result = STEGO_ERR;

    bn_null(d); g1_null(sig);

    RLC_TRY {
        bn_new(d); g1_new(sig);

        bn_read_bin(d, sk, sk_len);

        if (cp_bls_sig(sig, phash, phash_len, d) == RLC_OK) {
            int offset = 0;

            /* Payload: [pHash || signature || optional PK] */
            memcpy(payload + offset, phash, phash_len);
            offset += phash_len;

            int sig_sz = g1_size_bin(sig, 1);
            g1_write_bin(payload + offset, sig_sz, sig, 1);
            offset += sig_sz;

            if (append_pk && pk && pk_len > 0) {
                memcpy(payload + offset, pk, pk_len);
                offset += pk_len;
            }

            *payload_len = offset;
            result = STEGO_OK;
        }
    } RLC_CATCH_ANY {
        result = STEGO_ERR;
    } RLC_FINALLY {
        bn_free(d);
        g1_free(sig);
    }
    return result;
}

/* ------------------------------------------------------------------ */
/* Verification                                                        */
/* ------------------------------------------------------------------ */

int stego_verify(const uint8_t *payload, int payload_len,
                 int phash_len,
                 uint8_t *recovered_phash, int *recovered_len,
                 const uint8_t *ext_pk, int ext_pk_len)
{
    int sig_sz = stego_sig_bytes();
    int pk_sz = stego_pk_bytes();

    /* Parse payload: [pHash (phash_len)] [signature (sig_sz)] [optional PK] */
    if (payload_len < phash_len + sig_sz) return STEGO_ERR;

    const uint8_t *phash = payload;
    const uint8_t *sig_data = payload + phash_len;

    /* Extract pHash */
    memcpy(recovered_phash, phash, phash_len);
    *recovered_len = phash_len;

    /* Determine PK source */
    const uint8_t *pk_data;
    int pk_data_len;
    if (ext_pk && ext_pk_len > 0) {
        pk_data = ext_pk;
        pk_data_len = ext_pk_len;
    } else {
        int remaining = payload_len - phash_len - sig_sz;
        if (remaining < pk_sz) return STEGO_ERR;
        pk_data = payload + phash_len + sig_sz;
        pk_data_len = pk_sz;
    }

    /* Verify BLS signature */
    g1_t s;
    g2_t q;
    int result = STEGO_ERR_VERIFY;

    g1_null(s); g2_null(q);

    RLC_TRY {
        g1_new(s); g2_new(q);

        g1_read_bin(s, sig_data, sig_sz);
        g2_read_bin(q, pk_data, pk_data_len);

        if (cp_bls_ver(s, phash, phash_len, q)) {
            result = STEGO_OK;
        }
    } RLC_CATCH_ANY {
        result = STEGO_ERR;
    } RLC_FINALLY {
        g1_free(s);
        g2_free(q);
    }
    return result;
}
