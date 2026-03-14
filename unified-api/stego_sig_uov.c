// SPDX-License-Identifier: Apache-2.0
/**
 * stego_sig_uov.c - UOV backend for the unified stego signature API.
 *
 * UOV has message recovery: the pHash is embedded in the signature
 * via the salt-in-digest technique. The payload contains only the
 * signature vector w (+ optional PK). The verifier recovers the
 * pHash by evaluating P(w).
 */

#include "stego_sig.h"
#include <string.h>
#include <stdio.h>

/* UOV headers */
#include "params.h"
#include "ov_keypair.h"
#include "ov.h"
#include "api.h"

/* ------------------------------------------------------------------ */
/* Scheme information                                                  */
/* ------------------------------------------------------------------ */

const char *stego_scheme_name(void) {
    static char name[64];
    snprintf(name, sizeof(name), "UOV (%s)", OV_ALGNAME);
    return name;
}

int stego_security_bits(void) {
#if _O == 20
    return 80;
#elif _O == 25
    return 100;
#elif _O == 44
    return 128;
#else
    return 0;
#endif
}

int stego_has_message_recovery(void) { return 1; }
int stego_is_post_quantum(void)      { return 1; }

/* ------------------------------------------------------------------ */
/* Size queries                                                        */
/* ------------------------------------------------------------------ */

int stego_sig_bytes(void) { return OV_SIGNATUREBYTES; }
int stego_pk_bytes(void)  { return CRYPTO_PUBLICKEYBYTES; }
int stego_sk_bytes(void)  { return CRYPTO_SECRETKEYBYTES; }

int stego_max_phash_bytes(void) {
#if _SALT_BYTE > 0
    return _HASH_EFFECTIVE_BYTE;
#else
    return _PUB_M_BYTE;
#endif
}

int stego_payload_bytes(int phash_len, int append_pk) {
    (void)phash_len; /* pHash not transmitted for UOV (message recovery) */
    int total = OV_SIGNATUREBYTES;
    if (append_pk) total += CRYPTO_PUBLICKEYBYTES;
    return total;
}

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */

int stego_init(void)      { return STEGO_OK; }
void stego_cleanup(void)  { }

/* ------------------------------------------------------------------ */
/* Key management                                                      */
/* ------------------------------------------------------------------ */

int stego_keygen(uint8_t *pk, int *pk_len, uint8_t *sk, int *sk_len) {
    if (crypto_sign_keypair(pk, sk) != 0) return STEGO_ERR;
    *pk_len = CRYPTO_PUBLICKEYBYTES;
    *sk_len = CRYPTO_SECRETKEYBYTES;
    return STEGO_OK;
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
    (void)sk_len;

    /* Reject pHash longer than the recoverable digest size */
    if (phash_len > stego_max_phash_bytes()) return STEGO_ERR;

    uint8_t sig[OV_SIGNATUREBYTES];
    int rc;

    /* Use ov_sign_digest with optional user salt */
    rc = ov_sign_digest(sig, (const sk_t *)sk,
                        phash, (size_t)phash_len,
                        salt, (size_t)salt_len);
    if (rc != 0) return STEGO_ERR;

    /* Assemble payload: [signature] [|| PK] */
    int offset = 0;
    memcpy(payload + offset, sig, OV_SIGNATUREBYTES);
    offset += OV_SIGNATUREBYTES;

    if (append_pk && pk && pk_len > 0) {
        memcpy(payload + offset, pk, pk_len);
        offset += pk_len;
    }

    *payload_len = offset;
    return STEGO_OK;
}

/* ------------------------------------------------------------------ */
/* Verification                                                        */
/* ------------------------------------------------------------------ */

int stego_verify(const uint8_t *payload, int payload_len,
                 int phash_len,
                 uint8_t *recovered_phash, int *recovered_len,
                 const uint8_t *ext_pk, int ext_pk_len)
{
    (void)phash_len; /* UOV doesn't need this to parse payload */

    /* Parse payload: [signature (OV_SIGNATUREBYTES)] [|| PK] */
    if (payload_len < OV_SIGNATUREBYTES) return STEGO_ERR;

    const uint8_t *sig = payload;

    /* Determine which PK to use */
    const uint8_t *pk;
    if (ext_pk && ext_pk_len > 0) {
        pk = ext_pk;
    } else {
        /* Extract embedded PK from payload */
        int remaining = payload_len - OV_SIGNATUREBYTES;
        if (remaining < CRYPTO_PUBLICKEYBYTES) return STEGO_ERR;
        pk = payload + OV_SIGNATUREBYTES;
    }

    /* Recover digest via P(w) */
    uint8_t digest[_PUB_M_BYTE];
    ov_publicmap(digest, ((const pk_t *)pk)->pk, sig);

    /* The recovered digest layout: H(phash)[truncated] || salt
     * Return the hash portion as the recovered pHash. */
#if _SALT_BYTE > 0
    int hash_bytes = _HASH_EFFECTIVE_BYTE;
#else
    int hash_bytes = _PUB_M_BYTE;
#endif
    memcpy(recovered_phash, digest, hash_bytes);
    *recovered_len = hash_bytes;

    /* The recovered hash portion is H(phash)[0..hash_bytes-1].
     * The caller compares this against their own H(phash) to determine
     * authenticity. We don't have the original phash here, so we can't
     * do the full verification — but we CAN check that the signature
     * is a valid preimage under the public map by verifying via
     * ov_verify_digest if the caller provided the phash_len hint. */

    return STEGO_OK;
}
