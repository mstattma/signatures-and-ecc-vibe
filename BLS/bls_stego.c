// SPDX-License-Identifier: Apache-2.0
/**
 * bls_stego.c - BLS signature wrapper for steganographic channels
 *
 * Implementation using RELIC toolkit's cp_bls_* API.
 * Min-signature mode: signature in G1 (compact), public key in G2.
 */

#include "bls_stego.h"
#include <relic.h>
#include <string.h>
#include <stdlib.h>

struct bls_keypair {
    bn_t sk;
    g2_t pk;
};

/* ------------------------------------------------------------------ */
/* Library init / cleanup                                              */
/* ------------------------------------------------------------------ */

int bls_stego_init(void) {
    if (core_init() != RLC_OK) return BLS_ERR;
    if (pc_param_set_any() != RLC_OK) {
        core_clean();
        return BLS_ERR;
    }
    return BLS_OK;
}

void bls_stego_cleanup(void) {
    core_clean();
}

const char *bls_stego_curve_name(void) {
#if FP_PRIME == 381
    return "BLS12-381";
#elif FP_PRIME == 158
    return "BN-P158";
#else
    return "Unknown";
#endif
}

int bls_stego_security_bits(void) {
    return pc_param_level();
}

/* ------------------------------------------------------------------ */
/* Size queries                                                        */
/* ------------------------------------------------------------------ */

int bls_stego_sig_bytes(void) {
    /* Compressed G1 point size. We need a temporary point to query. */
    g1_t tmp;
    g1_null(tmp);
    g1_new(tmp);
    g1_get_gen(tmp);
    int sz = g1_size_bin(tmp, 1);
    g1_free(tmp);
    return sz;
}

int bls_stego_pk_bytes(void) {
    g2_t tmp;
    g2_null(tmp);
    g2_new(tmp);
    g2_get_gen(tmp);
    int sz = g2_size_bin(tmp, 1);
    g2_free(tmp);
    return sz;
}

int bls_stego_sk_bytes(void) {
    bn_t n;
    bn_null(n);
    bn_new(n);
    pc_get_ord(n);
    int sz = bn_size_bin(n);
    bn_free(n);
    return sz;
}

/* ------------------------------------------------------------------ */
/* Key generation                                                      */
/* ------------------------------------------------------------------ */

bls_keypair_t *bls_keypair_gen(void) {
    bls_keypair_t *kp = calloc(1, sizeof(*kp));
    if (!kp) return NULL;

    bn_null(kp->sk);
    g2_null(kp->pk);

    RLC_TRY {
        bn_new(kp->sk);
        g2_new(kp->pk);
        if (cp_bls_gen(kp->sk, kp->pk) != RLC_OK) {
            free(kp);
            return NULL;
        }
    } RLC_CATCH_ANY {
        free(kp);
        return NULL;
    }
    return kp;
}

void bls_keypair_free(bls_keypair_t *kp) {
    if (!kp) return;
    bn_free(kp->sk);
    g2_free(kp->pk);
    free(kp);
}

/* ------------------------------------------------------------------ */
/* Key export                                                          */
/* ------------------------------------------------------------------ */

int bls_pk_export(uint8_t *out, int *outlen, const bls_keypair_t *kp) {
    if (!kp || !out || !outlen) return BLS_ERR;
    int sz = g2_size_bin(kp->pk, 1);
    g2_write_bin(out, sz, kp->pk, 1);
    *outlen = sz;
    return BLS_OK;
}

int bls_sk_export(uint8_t *out, int *outlen, const bls_keypair_t *kp) {
    if (!kp || !out || !outlen) return BLS_ERR;
    int sz = bn_size_bin(kp->sk);
    bn_write_bin(out, sz, kp->sk);
    *outlen = sz;
    return BLS_OK;
}

/* ------------------------------------------------------------------ */
/* Sign / Verify                                                       */
/* ------------------------------------------------------------------ */

int bls_sign(uint8_t *sig_out, int *sig_len,
             const uint8_t *msg, size_t msg_len,
             const bls_keypair_t *kp)
{
    if (!sig_out || !sig_len || !msg || !kp) return BLS_ERR;

    g1_t sig;
    g1_null(sig);
    int result = BLS_ERR;

    RLC_TRY {
        g1_new(sig);
        if (cp_bls_sig(sig, msg, msg_len, kp->sk) == RLC_OK) {
            int sz = g1_size_bin(sig, 1);
            g1_write_bin(sig_out, sz, sig, 1);
            *sig_len = sz;
            result = BLS_OK;
        }
    } RLC_CATCH_ANY {
        result = BLS_ERR;
    } RLC_FINALLY {
        g1_free(sig);
    }
    return result;
}

int bls_verify(const uint8_t *sig, int sig_len,
               const uint8_t *msg, size_t msg_len,
               const uint8_t *pk, int pk_len)
{
    if (!sig || !msg || !pk) return BLS_ERR;

    g1_t s;
    g2_t q;
    g1_null(s);
    g2_null(q);
    int result = 0;

    RLC_TRY {
        g1_new(s);
        g2_new(q);
        g1_read_bin(s, sig, sig_len);
        g2_read_bin(q, pk, pk_len);
        result = cp_bls_ver(s, msg, msg_len, q);
    } RLC_CATCH_ANY {
        result = 0;
    } RLC_FINALLY {
        g1_free(s);
        g2_free(q);
    }
    return result;
}

/* ------------------------------------------------------------------ */
/* Payload assembly / disassembly                                      */
/* ------------------------------------------------------------------ */

int bls_payload_assemble(uint8_t *payload, int *payload_len,
                         const uint8_t *phash, int phash_len,
                         const uint8_t *sig, int sig_len,
                         const uint8_t *pk, int pk_len)
{
    if (!payload || !payload_len || !phash || !sig) return BLS_ERR;

    int offset = 0;
    memcpy(payload + offset, phash, phash_len);
    offset += phash_len;
    memcpy(payload + offset, sig, sig_len);
    offset += sig_len;
    if (pk && pk_len > 0) {
        memcpy(payload + offset, pk, pk_len);
        offset += pk_len;
    }
    *payload_len = offset;
    return BLS_OK;
}

int bls_payload_disassemble(const uint8_t *payload, int payload_len,
                            int phash_len, int sig_len,
                            const uint8_t **phash_out,
                            const uint8_t **sig_out,
                            const uint8_t **pk_out, int *pk_len_out)
{
    if (!payload || !phash_out || !sig_out) return BLS_ERR;

    if (payload_len < phash_len + sig_len) return BLS_ERR;

    *phash_out = payload;
    *sig_out = payload + phash_len;

    int remaining = payload_len - phash_len - sig_len;
    if (pk_out && pk_len_out) {
        if (remaining > 0) {
            *pk_out = payload + phash_len + sig_len;
            *pk_len_out = remaining;
        } else {
            *pk_out = NULL;
            *pk_len_out = 0;
        }
    }
    return BLS_OK;
}
