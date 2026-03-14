// SPDX-License-Identifier: Apache-2.0
/**
 * stego_sig.h - Scheme-agnostic signature API for steganographic channels
 *
 * Provides a unified interface for signing and verifying perceptual hashes
 * through bandwidth-constrained steganographic channels.
 *
 * Supported backends (selected at compile time via SCHEME=...):
 *   - UOV-80:     Post-quantum, 80-bit security, message recovery
 *   - UOV-100:    Post-quantum, 100-bit security, message recovery
 *   - BLS-BN158:  Classical, ~78-bit security, no message recovery
 *   - BLS12-381:  Classical, ~117-120 bit security, no message recovery
 *
 * Payload format:
 *   UOV (message recovery): [signature] [|| PK]
 *     pHash is recovered from the signature via P(w), not transmitted.
 *   BLS (no message recovery): [pHash || signature] [|| PK]
 *     pHash must be transmitted explicitly.
 *
 * The API abstracts away this difference: the caller always provides a pHash
 * and gets back a payload. On verification, the caller gets back the
 * recovered/extracted pHash regardless of scheme.
 */

#ifndef STEGO_SIG_H
#define STEGO_SIG_H

#include <stdint.h>
#include <stddef.h>

/* Maximum sizes for static buffers */
#define STEGO_MAX_SIG_BYTES     128
#define STEGO_MAX_PK_BYTES      256
#define STEGO_MAX_SK_BYTES      256
#define STEGO_MAX_PAYLOAD_BYTES 512
#define STEGO_MAX_PHASH_BYTES    32

/* Return codes */
#define STEGO_OK        0
#define STEGO_ERR      -1
#define STEGO_ERR_VERIFY -2

/* ------------------------------------------------------------------ */
/* Scheme information                                                  */
/* ------------------------------------------------------------------ */

/** Scheme name (e.g., "UOV-80", "BLS-BN158", "BLS12-381") */
const char *stego_scheme_name(void);

/** Classical security level in bits */
int stego_security_bits(void);

/** Whether this scheme supports message recovery (pHash recovered from sig) */
int stego_has_message_recovery(void);

/** Whether this scheme is post-quantum secure */
int stego_is_post_quantum(void);

/* ------------------------------------------------------------------ */
/* Size queries                                                        */
/* ------------------------------------------------------------------ */

/** Signature size in bytes (compressed) */
int stego_sig_bytes(void);

/** Public key size in bytes (compressed) */
int stego_pk_bytes(void);

/** Secret key size in bytes */
int stego_sk_bytes(void);

/**
 * Compute total payload size for a given configuration.
 * @param phash_len  pHash length in bytes.
 * @param append_pk  1 to include PK in payload, 0 otherwise.
 * @return Total payload bytes.
 */
int stego_payload_bytes(int phash_len, int append_pk);

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */

/** Initialize the signature backend. Must be called once. */
int stego_init(void);

/** Clean up. */
void stego_cleanup(void);

/* ------------------------------------------------------------------ */
/* Key management                                                      */
/* ------------------------------------------------------------------ */

/**
 * Generate a new key pair.
 * @param pk      Public key output buffer (at least stego_pk_bytes()).
 * @param pk_len  Actual PK bytes written.
 * @param sk      Secret key output buffer (at least stego_sk_bytes()).
 * @param sk_len  Actual SK bytes written.
 * @return STEGO_OK on success.
 */
int stego_keygen(uint8_t *pk, int *pk_len, uint8_t *sk, int *sk_len);

/* ------------------------------------------------------------------ */
/* Signing                                                             */
/* ------------------------------------------------------------------ */

/**
 * Sign a perceptual hash and produce a stego channel payload.
 *
 * For UOV: the pHash is embedded in the signature via salt-in-digest;
 *          the payload contains only the signature (+ optional PK).
 * For BLS: the pHash is prepended to the signature in the payload.
 *
 * @param payload      Output buffer (at least stego_payload_bytes()).
 * @param payload_len  Actual payload bytes written.
 * @param phash        Perceptual hash to authenticate.
 * @param phash_len    pHash length in bytes.
 * @param sk           Secret key (from stego_keygen).
 * @param sk_len       Secret key length.
 * @param salt         Salt bytes (UOV only; NULL for random, ignored for BLS).
 * @param salt_len     Salt length (UOV only; 0 for random, ignored for BLS).
 * @param append_pk    1 to append PK to payload, 0 for signature only.
 * @param pk           Public key (required if append_pk=1).
 * @param pk_len       Public key length.
 * @return STEGO_OK on success.
 */
int stego_sign(uint8_t *payload, int *payload_len,
               const uint8_t *phash, int phash_len,
               const uint8_t *sk, int sk_len,
               const uint8_t *salt, int salt_len,
               int append_pk,
               const uint8_t *pk, int pk_len);

/* ------------------------------------------------------------------ */
/* Verification                                                        */
/* ------------------------------------------------------------------ */

/**
 * Verify a stego channel payload and recover/extract the pHash.
 *
 * For UOV: the pHash is recovered from the signature via P(w).
 * For BLS: the pHash is extracted from the payload and the signature
 *          is verified against it.
 *
 * @param payload          Input payload.
 * @param payload_len      Payload length.
 * @param phash_len        Known pHash length (needed to parse the payload).
 * @param recovered_phash  Output: the recovered/extracted pHash.
 * @param recovered_len    Output: recovered pHash length in bytes.
 * @param ext_pk           External PK for verification (NULL to use embedded PK).
 * @param ext_pk_len       External PK length (0 to use embedded PK).
 * @return STEGO_OK if signature is valid,
 *         STEGO_ERR_VERIFY if signature is invalid,
 *         STEGO_ERR on error.
 */
int stego_verify(const uint8_t *payload, int payload_len,
                 int phash_len,
                 uint8_t *recovered_phash, int *recovered_len,
                 const uint8_t *ext_pk, int ext_pk_len);

#endif /* STEGO_SIG_H */
