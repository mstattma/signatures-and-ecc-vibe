// SPDX-License-Identifier: Apache-2.0
/**
 * bls_stego.h - BLS signature wrapper for steganographic channels
 *
 * Thin wrapper around RELIC's cp_bls_* API providing:
 *   - Key generation, signing, verification
 *   - Serialization to/from compact byte arrays (compressed points)
 *   - Payload assembly: pHash || signature [|| public key]
 *
 * Supports BLS12-381 and BN-158 curves (selected at compile time via RELIC config).
 * Uses min-signature mode: signature in G1 (smaller), public key in G2 (larger).
 */

#ifndef BLS_STEGO_H
#define BLS_STEGO_H

#include <stdint.h>
#include <stddef.h>

/**
 * Return codes
 */
#define BLS_OK       0
#define BLS_ERR     -1

/**
 * Initialize the BLS library (calls RELIC core_init + curve setup).
 * Must be called once before any other bls_stego function.
 * Returns BLS_OK on success.
 */
int bls_stego_init(void);

/**
 * Clean up the BLS library (calls RELIC core_clean).
 */
void bls_stego_cleanup(void);

/**
 * Get the name of the configured curve (e.g., "BLS12-381" or "BN-P158").
 */
const char *bls_stego_curve_name(void);

/**
 * Get the security level in bits as reported by RELIC.
 */
int bls_stego_security_bits(void);

/**
 * Get the compressed size of a G1 point (signature) in bytes.
 */
int bls_stego_sig_bytes(void);

/**
 * Get the compressed size of a G2 point (public key) in bytes.
 */
int bls_stego_pk_bytes(void);

/**
 * Get the secret key size in bytes.
 */
int bls_stego_sk_bytes(void);

/**
 * Opaque key types (heap-allocated RELIC objects).
 */
typedef struct bls_keypair bls_keypair_t;

/**
 * Generate a new BLS key pair.
 * Returns NULL on failure. Caller must free with bls_keypair_free().
 */
bls_keypair_t *bls_keypair_gen(void);

/**
 * Free a key pair.
 */
void bls_keypair_free(bls_keypair_t *kp);

/**
 * Export the public key to a compressed byte array.
 * @param out    Buffer of at least bls_stego_pk_bytes() bytes.
 * @param outlen Set to actual bytes written.
 * @return BLS_OK on success.
 */
int bls_pk_export(uint8_t *out, int *outlen, const bls_keypair_t *kp);

/**
 * Export the secret key to a byte array.
 * @param out    Buffer of at least bls_stego_sk_bytes() bytes.
 * @param outlen Set to actual bytes written.
 * @return BLS_OK on success.
 */
int bls_sk_export(uint8_t *out, int *outlen, const bls_keypair_t *kp);

/**
 * Sign a message (pHash or arbitrary data).
 * @param sig_out  Buffer of at least bls_stego_sig_bytes() bytes.
 * @param sig_len  Set to actual signature bytes written.
 * @param msg      Message to sign.
 * @param msg_len  Message length.
 * @param kp       Key pair (uses secret key).
 * @return BLS_OK on success.
 */
int bls_sign(uint8_t *sig_out, int *sig_len,
             const uint8_t *msg, size_t msg_len,
             const bls_keypair_t *kp);

/**
 * Verify a signature.
 * @param sig      Compressed signature (G1 point).
 * @param sig_len  Signature length.
 * @param msg      Original message.
 * @param msg_len  Message length.
 * @param pk       Compressed public key (G2 point).
 * @param pk_len   Public key length.
 * @return 1 if valid, 0 if invalid, BLS_ERR on error.
 */
int bls_verify(const uint8_t *sig, int sig_len,
               const uint8_t *msg, size_t msg_len,
               const uint8_t *pk, int pk_len);

/**
 * Assemble a stego channel payload: pHash || signature [|| public key].
 *
 * @param payload     Output buffer.
 * @param payload_len Set to total bytes written.
 * @param phash       Perceptual hash bytes.
 * @param phash_len   Perceptual hash length (e.g., 12, 18, or 23 bytes).
 * @param sig         Compressed signature.
 * @param sig_len     Signature length.
 * @param pk          Compressed public key (NULL to omit).
 * @param pk_len      Public key length (0 to omit).
 * @return BLS_OK on success.
 */
int bls_payload_assemble(uint8_t *payload, int *payload_len,
                         const uint8_t *phash, int phash_len,
                         const uint8_t *sig, int sig_len,
                         const uint8_t *pk, int pk_len);

/**
 * Disassemble a stego channel payload.
 *
 * @param payload      Input buffer.
 * @param payload_len  Total payload length.
 * @param phash_len    Known pHash length.
 * @param sig_len      Known signature length (bls_stego_sig_bytes()).
 * @param phash_out    Set to point into payload at pHash start.
 * @param sig_out      Set to point into payload at signature start.
 * @param pk_out       Set to point into payload at PK start (NULL if no PK).
 * @param pk_len_out   Set to PK length (0 if no PK appended).
 * @return BLS_OK on success, BLS_ERR if payload too short.
 */
int bls_payload_disassemble(const uint8_t *payload, int payload_len,
                            int phash_len, int sig_len,
                            const uint8_t **phash_out,
                            const uint8_t **sig_out,
                            const uint8_t **pk_out, int *pk_len_out);

#endif /* BLS_STEGO_H */
