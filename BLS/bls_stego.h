// SPDX-License-Identifier: Apache-2.0
/**
 * bls_stego.h - BLS signature wrapper for steganographic channels
 *
 * Thin wrapper around RELIC's cp_bls_* API providing:
 *   - Key generation, signing, verification
 *   - Serialization to/from compact byte arrays (compressed points)
 *   - Payload assembly: pHash || salt || signature [|| public key]
 *
 * A random salt is embedded in the payload (like UOV's salt-in-digest) to
 * ensure that similar images with identical perceptual hashes produce
 * different signatures. The BLS signature covers pHash || salt.
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
 * Default salt length in bytes (configurable via -DBLS_SALT_BYTE=N).
 * Set to 0 to disable salt. The salt is embedded in the payload and
 * concatenated with the pHash before signing: BLS_sign(pHash || salt).
 */
#ifndef BLS_SALT_BYTE
#define BLS_SALT_BYTE 2
#endif

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
 * Sign a perceptual hash with salt.
 *
 * The signing input is pHash || salt_actual, where salt_actual is BLS_SALT_BYTE
 * bytes derived from user_salt (hashed down) or random if user_salt is NULL.
 *
 * @param sig_out       Buffer of at least bls_stego_sig_bytes() bytes.
 * @param sig_len       Set to actual signature bytes written.
 * @param salt_out      Buffer of at least BLS_SALT_BYTE bytes (receives the actual salt used).
 *                      Can be NULL if BLS_SALT_BYTE == 0.
 * @param phash         Perceptual hash to sign.
 * @param phash_len     pHash length.
 * @param user_salt     Optional user-provided salt (any length), or NULL for random.
 * @param user_salt_len Length of user_salt (0 for random).
 * @param kp            Key pair (uses secret key).
 * @return BLS_OK on success.
 */
int bls_sign(uint8_t *sig_out, int *sig_len,
             uint8_t *salt_out,
             const uint8_t *phash, size_t phash_len,
             const uint8_t *user_salt, size_t user_salt_len,
             const bls_keypair_t *kp);

/**
 * Verify a signature against pHash || salt.
 * @param sig        Compressed signature (G1 point).
 * @param sig_len    Signature length.
 * @param phash      Perceptual hash.
 * @param phash_len  pHash length.
 * @param salt       Salt bytes (BLS_SALT_BYTE bytes).
 * @param pk         Compressed public key (G2 point).
 * @param pk_len     Public key length.
 * @return 1 if valid, 0 if invalid, BLS_ERR on error.
 */
int bls_verify(const uint8_t *sig, int sig_len,
               const uint8_t *phash, size_t phash_len,
               const uint8_t *salt,
               const uint8_t *pk, int pk_len);

/**
 * Assemble a stego channel payload: pHash || salt || signature [|| public key].
 *
 * @param payload     Output buffer.
 * @param payload_len Set to total bytes written.
 * @param phash       Perceptual hash bytes.
 * @param phash_len   Perceptual hash length (e.g., 12, 18, or 23 bytes).
 * @param salt        Salt bytes (BLS_SALT_BYTE bytes). Can be NULL if BLS_SALT_BYTE == 0.
 * @param sig         Compressed signature.
 * @param sig_len     Signature length.
 * @param pk          Compressed public key (NULL to omit).
 * @param pk_len      Public key length (0 to omit).
 * @return BLS_OK on success.
 */
int bls_payload_assemble(uint8_t *payload, int *payload_len,
                         const uint8_t *phash, int phash_len,
                         const uint8_t *salt,
                         const uint8_t *sig, int sig_len,
                         const uint8_t *pk, int pk_len);

/**
 * Disassemble a stego channel payload: pHash || salt || signature [|| PK].
 *
 * @param payload      Input buffer.
 * @param payload_len  Total payload length.
 * @param phash_len    Known pHash length.
 * @param sig_len      Known signature length (bls_stego_sig_bytes()).
 * @param phash_out    Set to point into payload at pHash start.
 * @param salt_out     Set to point into payload at salt start (NULL if BLS_SALT_BYTE==0).
 * @param sig_out      Set to point into payload at signature start.
 * @param pk_out       Set to point into payload at PK start (NULL if no PK).
 * @param pk_len_out   Set to PK length (0 if no PK appended).
 * @return BLS_OK on success, BLS_ERR if payload too short.
 */
int bls_payload_disassemble(const uint8_t *payload, int payload_len,
                            int phash_len, int sig_len,
                            const uint8_t **phash_out,
                            const uint8_t **salt_out,
                            const uint8_t **sig_out,
                            const uint8_t **pk_out, int *pk_len_out);

#endif /* BLS_STEGO_H */
