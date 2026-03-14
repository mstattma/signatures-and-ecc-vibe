// SPDX-License-Identifier: Apache-2.0
/**
 * stego_sig_bls.c - BLS backend for the unified stego signature API.
 *
 * BLS has no message recovery. Two payload modes:
 *   embed_phash=1: [pHash || salt || signature] [|| PK]
 *     pHash and salt are transmitted in the payload.
 *   embed_phash=0: [salt || signature] [|| PK]
 *     pHash is NOT in the payload; must be provided at verification.
 *
 * The BLS signature always covers (pHash || salt).
 *
 * Verification auto-detects whether pHash is embedded by comparing
 * payload_len against the expected size for sig-only (salt + sig [+ PK]).
 */

#include "stego_sig.h"
#include <string.h>
#include <relic.h>

/* Salt length in bytes */
#ifndef BLS_SALT_BYTE
#define BLS_SALT_BYTE 2
#endif

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
/* Size queries                                                        */
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
int stego_max_phash_bytes(void) { return 1024; }

int stego_payload_bytes(int phash_len, int embed_phash, int append_pk) {
    cache_sizes();
    int total = BLS_SALT_BYTE + s_sig_bytes;
    if (embed_phash) total += phash_len;
    if (append_pk)   total += s_pk_bytes;
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
               int embed_phash,
               int append_pk,
               const uint8_t *pk, int pk_len)
{
    /* Generate or derive salt */
    uint8_t salt_actual[BLS_SALT_BYTE];
    if (salt && salt_len > 0) {
        uint8_t hash_out[RLC_MD_LEN];
        md_map(hash_out, salt, salt_len);
        memcpy(salt_actual, hash_out, BLS_SALT_BYTE);
    } else {
        rand_bytes(salt_actual, BLS_SALT_BYTE);
    }

    /* Build signing input: pHash || salt (signature always covers both) */
    uint8_t sign_input[1024];
    if ((size_t)phash_len + BLS_SALT_BYTE > sizeof(sign_input)) return STEGO_ERR;
    memcpy(sign_input, phash, phash_len);
    memcpy(sign_input + phash_len, salt_actual, BLS_SALT_BYTE);
    size_t sign_len = phash_len + BLS_SALT_BYTE;

    bn_t d;
    g1_t sig;
    int result = STEGO_ERR;

    bn_null(d); g1_null(sig);

    RLC_TRY {
        bn_new(d); g1_new(sig);
        bn_read_bin(d, sk, sk_len);

        if (cp_bls_sig(sig, sign_input, sign_len, d) == RLC_OK) {
            int offset = 0;

            /* Payload: [optional pHash] || salt || signature || [optional PK] */
            if (embed_phash) {
                memcpy(payload + offset, phash, phash_len);
                offset += phash_len;
            }

            memcpy(payload + offset, salt_actual, BLS_SALT_BYTE);
            offset += BLS_SALT_BYTE;

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
                 const uint8_t *ext_pk, int ext_pk_len,
                 const uint8_t *expected_phash, int expected_phash_len)
{
    int sig_sz = stego_sig_bytes();
    int pk_sz  = stego_pk_bytes();

    /* Determine whether pHash is embedded by checking payload length.
     *
     * Without pHash: payload = [salt (BLS_SALT_BYTE)] [sig] [optional PK]
     *   min_no_phash = BLS_SALT_BYTE + sig_sz
     *
     * With pHash:    payload = [pHash (phash_len)] [salt] [sig] [optional PK]
     *   min_with_phash = phash_len + BLS_SALT_BYTE + sig_sz
     *
     * We detect by checking: if payload_len >= min_with_phash, pHash is embedded.
     * If payload_len >= min_no_phash but < min_with_phash, no pHash.
     * (This works because phash_len > 0 always.) */

    int min_no_phash   = BLS_SALT_BYTE + sig_sz;
    int min_with_phash = phash_len + BLS_SALT_BYTE + sig_sz;

    int phash_embedded;
    if (payload_len >= min_with_phash) {
        phash_embedded = 1;
    } else if (payload_len >= min_no_phash) {
        phash_embedded = 0;
    } else {
        return STEGO_ERR;
    }

    const uint8_t *phash_data;
    const uint8_t *salt_data;
    const uint8_t *sig_data;
    int offset = 0;

    if (phash_embedded) {
        phash_data = payload + offset;
        offset += phash_len;
    } else {
        phash_data = NULL;
    }

    salt_data = payload + offset;
    offset += BLS_SALT_BYTE;

    sig_data = payload + offset;
    offset += sig_sz;

    /* Resolve the pHash: embedded, expected, or error */
    const uint8_t *verify_phash;
    int verify_phash_len;

    if (phash_embedded) {
        verify_phash = phash_data;
        verify_phash_len = phash_len;
    } else if (expected_phash && expected_phash_len > 0) {
        verify_phash = expected_phash;
        verify_phash_len = expected_phash_len;
    } else {
        /* No pHash available from either source */
        return STEGO_ERR_NO_PHASH;
    }

    /* Return the pHash to the caller */
    memcpy(recovered_phash, verify_phash, verify_phash_len);
    *recovered_len = verify_phash_len;

    /* Determine PK source */
    const uint8_t *pk_data;
    int pk_data_len;
    if (ext_pk && ext_pk_len > 0) {
        pk_data = ext_pk;
        pk_data_len = ext_pk_len;
    } else {
        int remaining = payload_len - offset;
        if (remaining < pk_sz) return STEGO_ERR;
        pk_data = payload + offset;
        pk_data_len = pk_sz;
    }

    /* Reconstruct signing input: pHash || salt */
    uint8_t verify_input[1024];
    if ((size_t)verify_phash_len + BLS_SALT_BYTE > sizeof(verify_input)) return STEGO_ERR;
    memcpy(verify_input, verify_phash, verify_phash_len);
    memcpy(verify_input + verify_phash_len, salt_data, BLS_SALT_BYTE);
    size_t verify_len = verify_phash_len + BLS_SALT_BYTE;

    /* Verify BLS signature */
    g1_t s;
    g2_t q;
    int result = STEGO_ERR_VERIFY;

    g1_null(s); g2_null(q);

    RLC_TRY {
        g1_new(s); g2_new(q);
        g1_read_bin(s, sig_data, sig_sz);
        g2_read_bin(q, pk_data, pk_data_len);

        if (cp_bls_ver(s, verify_input, verify_len, q)) {
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
