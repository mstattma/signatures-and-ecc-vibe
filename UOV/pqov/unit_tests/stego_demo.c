// SPDX-License-Identifier: CC0 OR Apache-2.0
/**
 * stego_demo.c - Demonstrates UOV signature with salt-in-digest message recovery
 * for bandwidth-constrained steganographic channels.
 *
 * This demo shows:
 * 1. Key generation (public key exchanged out-of-band)
 * 2. Signing: produces a compact signature w (the ONLY thing transmitted)
 * 3. Message recovery: verifier recovers the digest P(w) = H(msg)[truncated] || salt
 * 4. Verification: confirms the hash portion matches H(message)
 * 5. User-provided salt: demonstrates signing with a caller-supplied salt
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "params.h"
#include "ov_keypair.h"
#include "ov.h"
#include "api.h"
#include "utils_hash.h"

static void print_hex(const char *label, const uint8_t *data, size_t len) {
    printf("  %s (%zu bytes = %zu bits): ", label, len, len * 8);
    for (size_t i = 0; i < len && i < 32; i++) {
        printf("%02x", data[i]);
    }
    if (len > 32) printf("...");
    printf("\n");
}

int main(void) {
    printf("=================================================================\n");
    printf("  UOV Signature with Salt-in-Digest Recovery - Stego Channel Demo\n");
    printf("=================================================================\n\n");

    printf("Parameters: %s\n", OV_ALGNAME);
    printf("  Field:      GF(%d)\n", _GFSIZE);
    printf("  n = %d (v=%d, o=%d)\n", _PUB_N, _V, _O);
    printf("  Signature:  %d bytes = %d bits (w only, no appended salt)\n",
           OV_SIGNATUREBYTES, OV_SIGNATUREBYTES * 8);
    printf("  Digest from P(w): %d bytes = %d bits\n", _PUB_M_BYTE, _PUB_M_BYTE * 8);
#if _SALT_BYTE > 0
    printf("    Hash portion:   %d bytes = %d bits (%d-bit collision resistance)\n",
           _HASH_EFFECTIVE_BYTE, _HASH_EFFECTIVE_BYTE * 8, _HASH_EFFECTIVE_BYTE * 8 / 2);
    printf("    Salt portion:   %d bytes = %d bits (embedded in digest)\n",
           _SALT_BYTE, _SALT_BYTE * 8);
#else
    printf("    No salt (entire digest is hash, %d-bit collision resistance)\n",
           _PUB_M_BYTE * 8 / 2);
#endif
    printf("  Public key: %d bytes (classic)\n", OV_PK_UNCOMPRESSED_BYTES);
    printf("  Public key: %d bytes (compressed)\n", OV_PK_COMPRESSED_BYTES);
    printf("\n");

    // ============================================================
    // Step 1: Key Generation (done once, PK exchanged out-of-band)
    // ============================================================
    printf("--- Step 1: Key Generation ---\n");

    uint8_t *pk = malloc(CRYPTO_PUBLICKEYBYTES);
    uint8_t *sk = malloc(CRYPTO_SECRETKEYBYTES);
    if (!pk || !sk) {
        fprintf(stderr, "malloc failed\n");
        return 1;
    }

    int rc = crypto_sign_keypair(pk, sk);
    if (rc != 0) {
        fprintf(stderr, "keygen failed: %d\n", rc);
        return 1;
    }
    printf("  Key pair generated successfully.\n");
    printf("  Public key: %d bytes (exchanged out-of-band)\n", CRYPTO_PUBLICKEYBYTES);
    printf("\n");

    // ============================================================
    // Step 2: Sender signs a message (random salt)
    // ============================================================
    printf("--- Step 2: Sender signs a message (random salt) ---\n");

    const char *original_message = "This is the original content to be authenticated via steganography.";
    size_t mlen = strlen(original_message);

    // Show the message hash for reference
    uint8_t message_hash[_PUB_M_BYTE];
    {
        hash_ctx hctx;
        hash_init(&hctx);
        hash_update(&hctx, (const uint8_t *)original_message, mlen);
        hash_final_digest(message_hash, _PUB_M_BYTE, &hctx);
    }
    printf("  Message: \"%s\"\n", original_message);
    print_hex("H(message)", message_hash, _PUB_M_BYTE);
    printf("\n");

    // Sign the message
    uint8_t signature[OV_SIGNATUREBYTES];
    unsigned long long siglen;
    rc = crypto_sign_signature(signature, &siglen, (const uint8_t *)original_message, mlen, sk);
    if (rc != 0) {
        fprintf(stderr, "signing failed: %d\n", rc);
        return 1;
    }

    printf("  Signature generated!\n");
    print_hex("Signature w", signature, siglen);
    printf("    w (public map input): %d bytes = %d bits\n", _PUB_N_BYTE, _PUB_N_BYTE * 8);
    printf("    TOTAL transmitted:    %d bytes = %d bits\n", OV_SIGNATUREBYTES, OV_SIGNATUREBYTES * 8);
    printf("    (salt is inside P(w), NOT appended to signature)\n");
    printf("\n");

    // ============================================================
    // Step 3: The stego channel
    // ============================================================
    printf("--- Step 3: Stego Channel Transmission ---\n");
    printf("  ONLY the signature w is transmitted through the stego channel.\n");
    printf("  The original message is NOT transmitted.\n");
    printf("  Transmitted payload: %d bits (before outer ECC)\n", OV_SIGNATUREBYTES * 8);
    printf("\n");

    // ============================================================
    // Step 4: Receiver performs message recovery + verification
    // ============================================================
    printf("--- Step 4: Receiver - Message Recovery ---\n");

    // Step 4a: Recover the digest from the signature using the public map
    uint8_t recovered_digest[_PUB_M_BYTE];
    ov_publicmap(recovered_digest, ((const pk_t *)pk)->pk, signature);

    print_hex("Recovered digest P(w)", recovered_digest, _PUB_M_BYTE);
#if _SALT_BYTE > 0
    print_hex("  Hash portion", recovered_digest, _HASH_EFFECTIVE_BYTE);
    print_hex("  Salt portion", recovered_digest + _HASH_EFFECTIVE_BYTE, _SALT_BYTE);
#endif
    printf("\n");

    // Step 4b: Verify the signature against the original message
    printf("--- Step 4b: Verification ---\n");
    rc = crypto_sign_verify(signature, siglen, (const uint8_t *)original_message, mlen, pk);
    printf("  Verification result: %s\n", rc == 0 ? "VALID" : "INVALID");
    printf("\n");

    // Step 4c: Demonstrate that the hash portion of the recovered digest matches
    printf("--- Step 4c: Digest Comparison ---\n");
    uint8_t expected_hash[_PUB_M_BYTE];
    {
        hash_ctx hctx;
        hash_init(&hctx);
        hash_update(&hctx, (const uint8_t *)original_message, mlen);
#if _SALT_BYTE > 0
        hash_final_digest(expected_hash, _HASH_EFFECTIVE_BYTE, &hctx);
#else
        hash_final_digest(expected_hash, _PUB_M_BYTE, &hctx);
#endif
    }
#if _SALT_BYTE > 0
    print_hex("Expected  H(msg)[truncated]", expected_hash, _HASH_EFFECTIVE_BYTE);
    print_hex("Recovered P(w)[hash part]  ", recovered_digest, _HASH_EFFECTIVE_BYTE);
    int match = (memcmp(recovered_digest, expected_hash, _HASH_EFFECTIVE_BYTE) == 0);
#else
    print_hex("Expected  H(msg)", expected_hash, _PUB_M_BYTE);
    print_hex("Recovered P(w)  ", recovered_digest, _PUB_M_BYTE);
    int match = (memcmp(recovered_digest, expected_hash, _PUB_M_BYTE) == 0);
#endif
    printf("  Match: %s\n", match ? "YES - message recovery confirmed!" : "NO - ERROR!");
    printf("\n");

#if _SALT_BYTE > 0
    // ============================================================
    // Step 5: Demonstrate user-provided salt
    // ============================================================
    printf("--- Step 5: Signing with user-provided salt ---\n");

    const char *user_salt_str = "my-custom-salt-value-any-length";
    printf("  User salt: \"%s\" (%zu bytes)\n", user_salt_str, strlen(user_salt_str));

    uint8_t signature2[OV_SIGNATUREBYTES];
    rc = ov_sign_salt(signature2, (const sk_t *)sk, (const uint8_t *)original_message, mlen,
                      (const uint8_t *)user_salt_str, strlen(user_salt_str));
    if (rc != 0) {
        fprintf(stderr, "signing with user salt failed: %d\n", rc);
        return 1;
    }

    // Recover digest and show the salt portion
    uint8_t recovered2[_PUB_M_BYTE];
    ov_publicmap(recovered2, ((const pk_t *)pk)->pk, signature2);
    print_hex("Recovered digest P(w)", recovered2, _PUB_M_BYTE);
    print_hex("  Salt portion (derived from user salt)", recovered2 + _HASH_EFFECTIVE_BYTE, _SALT_BYTE);

    // Verify
    rc = crypto_sign_verify(signature2, OV_SIGNATUREBYTES, (const uint8_t *)original_message, mlen, pk);
    printf("  Verification: %s\n", rc == 0 ? "VALID" : "INVALID");

    // Sign again with the same user salt -- should produce the same signature (deterministic)
    uint8_t signature3[OV_SIGNATUREBYTES];
    rc = ov_sign_salt(signature3, (const sk_t *)sk, (const uint8_t *)original_message, mlen,
                      (const uint8_t *)user_salt_str, strlen(user_salt_str));
    int same_sig = (memcmp(signature2, signature3, OV_SIGNATUREBYTES) == 0);
    printf("  Same salt + same message = same signature: %s\n", same_sig ? "YES (deterministic)" : "NO (ERROR!)");
    printf("\n");
#endif

    // ============================================================
    // Step 6: Signing with externally-provided digest (skip SHAKE256)
    // ============================================================
    printf("--- Step 6: Signing with external digest ---\n");

    // Simulate an externally-computed digest (e.g., SHA-256 from another system)
    const uint8_t external_digest[] = {
        0xde, 0xad, 0xbe, 0xef, 0xca, 0xfe, 0xba, 0xbe,
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
        0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
        0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x00, 0x99
    };
    size_t ext_digest_len = 32;  // e.g., SHA-256 output
    print_hex("External digest", external_digest, ext_digest_len);

    uint8_t sig_ext[OV_SIGNATUREBYTES];
    rc = ov_sign_digest(sig_ext, (const sk_t *)sk,
                        external_digest, ext_digest_len,
                        NULL, 0);  // random salt
    if (rc != 0) {
        fprintf(stderr, "signing with external digest failed: %d\n", rc);
        return 1;
    }
    print_hex("Signature w", sig_ext, OV_SIGNATUREBYTES);

    // Verify using ov_verify_digest
    rc = ov_verify_digest(external_digest, ext_digest_len,
                          sig_ext, (const pk_t *)pk);
    printf("  Verification (ov_verify_digest): %s\n", rc == 0 ? "VALID" : "INVALID");

    // Show what was recovered
    uint8_t recovered_ext[_PUB_M_BYTE];
    ov_publicmap(recovered_ext, ((const pk_t *)pk)->pk, sig_ext);
    print_hex("Recovered P(w)", recovered_ext, _PUB_M_BYTE);
#if _SALT_BYTE > 0
    print_hex("  Hash portion (from external digest)", recovered_ext, _HASH_EFFECTIVE_BYTE);
    printf("  Expected first %d bytes of external digest: ", _HASH_EFFECTIVE_BYTE);
    for (int i = 0; i < _HASH_EFFECTIVE_BYTE && i < (int)ext_digest_len; i++) printf("%02x", external_digest[i]);
    printf("\n");
    int ext_match = (memcmp(recovered_ext, external_digest, _HASH_EFFECTIVE_BYTE) == 0);
#else
    int ext_match = (memcmp(recovered_ext, external_digest, _PUB_M_BYTE) == 0);
#endif
    printf("  Hash portion matches external digest: %s\n",
           ext_match ? "YES" : "NO (digest was hashed/truncated)");

    // Also test with user-provided salt + digest
    uint8_t sig_ext2[OV_SIGNATUREBYTES];
    rc = ov_sign_digest(sig_ext2, (const sk_t *)sk,
                        external_digest, ext_digest_len,
                        (const uint8_t *)"fixed-salt", 10);
    rc = ov_verify_digest(external_digest, ext_digest_len,
                          sig_ext2, (const pk_t *)pk);
    printf("  Verify with user salt + external digest: %s\n", rc == 0 ? "VALID" : "INVALID");
    printf("\n");

    // ============================================================
    // Summary
    // ============================================================
    printf("=================================================================\n");
    printf("  SUMMARY FOR STEGO CHANNEL DESIGN\n");
    printf("=================================================================\n");
    printf("  What goes through the stego channel:\n");
    printf("    - Signature w only: %d bytes = %d bits\n", OV_SIGNATUREBYTES, OV_SIGNATUREBYTES * 8);
    printf("    - NO salt appended (salt is inside the recovered digest)\n");
    printf("\n");
    printf("  What the verifier recovers via P(w):\n");
    printf("    - Full digest: %d bytes = %d bits\n", _PUB_M_BYTE, _PUB_M_BYTE * 8);
#if _SALT_BYTE > 0
    printf("    - Hash portion: %d bytes = %d bits (%d-bit collision resistance)\n",
           _HASH_EFFECTIVE_BYTE, _HASH_EFFECTIVE_BYTE * 8, _HASH_EFFECTIVE_BYTE * 8 / 2);
    printf("    - Salt portion: %d bytes = %d bits (multi-target protection)\n",
           _SALT_BYTE, _SALT_BYTE * 8);
#else
    printf("    - All hash, no salt (%d-bit collision resistance)\n", _PUB_M_BYTE * 8 / 2);
#endif
    printf("\n");
    printf("  What goes out-of-band (one-time setup):\n");
    printf("    - Public key: %d bytes\n", CRYPTO_PUBLICKEYBYTES);
    printf("=================================================================\n");

    free(pk);
    free(sk);
    return 0;
}
