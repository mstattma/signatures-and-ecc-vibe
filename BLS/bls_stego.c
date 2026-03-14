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

/**
 * Generate or derive the BLS_SALT_BYTE salt bytes.
 */
#if BLS_SALT_BYTE > 0
static void derive_salt(uint8_t *salt, const uint8_t *user_salt, size_t user_salt_len) {
    if (user_salt && user_salt_len > 0) {
        /* Derive salt by hashing user-provided data with RELIC's md_map */
        uint8_t hash_out[RLC_MD_LEN];
        md_map(hash_out, user_salt, user_salt_len);
        memcpy(salt, hash_out, BLS_SALT_BYTE);
    } else {
        rand_bytes(salt, BLS_SALT_BYTE);
    }
}
#endif

int bls_sign(uint8_t *sig_out, int *sig_len,
             uint8_t *salt_out,
             const uint8_t *phash, size_t phash_len,
             const uint8_t *user_salt, size_t user_salt_len,
             const bls_keypair_t *kp)
{
    if (!sig_out || !sig_len || !phash || !kp) return BLS_ERR;

#if BLS_SALT_BYTE > 0
    uint8_t salt[BLS_SALT_BYTE];
    derive_salt(salt, user_salt, user_salt_len);
    if (salt_out) memcpy(salt_out, salt, BLS_SALT_BYTE);

    /* Signing input: pHash || salt */
    uint8_t sign_input[1024];
    if (phash_len + BLS_SALT_BYTE > sizeof(sign_input)) return BLS_ERR;
    memcpy(sign_input, phash, phash_len);
    memcpy(sign_input + phash_len, salt, BLS_SALT_BYTE);
    size_t sign_len = phash_len + BLS_SALT_BYTE;
#else
    (void)salt_out; (void)user_salt; (void)user_salt_len;
    const uint8_t *sign_input = phash;
    size_t sign_len = phash_len;
#endif

    g1_t sig;
    g1_null(sig);
    int result = BLS_ERR;

    RLC_TRY {
        g1_new(sig);
        if (cp_bls_sig(sig, sign_input, sign_len, kp->sk) == RLC_OK) {
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
               const uint8_t *phash, size_t phash_len,
               const uint8_t *salt,
               const uint8_t *pk, int pk_len)
{
    if (!sig || !phash || !pk) return BLS_ERR;

#if BLS_SALT_BYTE > 0
    /* Reconstruct signing input: pHash || salt */
    uint8_t verify_input[1024];
    if (phash_len + BLS_SALT_BYTE > sizeof(verify_input)) return BLS_ERR;
    memcpy(verify_input, phash, phash_len);
    if (salt) memcpy(verify_input + phash_len, salt, BLS_SALT_BYTE);
    else return BLS_ERR;
    size_t verify_len = phash_len + BLS_SALT_BYTE;
#else
    (void)salt;
    const uint8_t *verify_input = phash;
    size_t verify_len = phash_len;
#endif

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
        result = cp_bls_ver(s, verify_input, verify_len, q);
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
                         const uint8_t *salt,
                         const uint8_t *sig, int sig_len,
                         const uint8_t *pk, int pk_len)
{
    if (!payload || !payload_len || !phash || !sig) return BLS_ERR;

    int offset = 0;
    memcpy(payload + offset, phash, phash_len);
    offset += phash_len;
#if BLS_SALT_BYTE > 0
    if (salt) {
        memcpy(payload + offset, salt, BLS_SALT_BYTE);
        offset += BLS_SALT_BYTE;
    } else {
        return BLS_ERR;
    }
#else
    (void)salt;
#endif
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
                            const uint8_t **salt_out,
                            const uint8_t **sig_out,
                            const uint8_t **pk_out, int *pk_len_out)
{
    if (!payload || !phash_out || !sig_out) return BLS_ERR;

    int min_len = phash_len + BLS_SALT_BYTE + sig_len;
    if (payload_len < min_len) return BLS_ERR;

    int offset = 0;
    *phash_out = payload + offset;
    offset += phash_len;

#if BLS_SALT_BYTE > 0
    if (salt_out) *salt_out = payload + offset;
    offset += BLS_SALT_BYTE;
#else
    if (salt_out) *salt_out = NULL;
#endif

    *sig_out = payload + offset;
    offset += sig_len;

    int remaining = payload_len - offset;
    if (pk_out && pk_len_out) {
        if (remaining > 0) {
            *pk_out = payload + offset;
            *pk_len_out = remaining;
        } else {
            *pk_out = NULL;
            *pk_len_out = 0;
        }
    }
    return BLS_OK;
}
