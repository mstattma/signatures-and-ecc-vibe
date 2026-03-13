// SPDX-License-Identifier: CC0 OR Apache-2.0
/**
 * stego_demo.c - Demonstrates UOV signature with message recovery
 * for bandwidth-constrained steganographic channels.
 *
 * This demo shows:
 * 1. Key generation (public key exchanged out-of-band)
 * 2. Signing: produces a compact signature (the ONLY thing transmitted)
 * 3. Message recovery: verifier recovers the hash digest from the signature
 *    using the public key, WITHOUT receiving the digest separately
 * 4. Verification: confirms the signature is valid for the original message
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
    printf("  UOV Signature with Message Recovery - Stego Channel Demo\n");
    printf("=================================================================\n\n");

    printf("Parameters: %s\n", OV_ALGNAME);
    printf("  Field:      GF(%d)\n", _GFSIZE);
    printf("  n = %d (v=%d, o=%d)\n", _PUB_N, _V, _O);
    printf("  Signature:  %d bytes = %d bits\n", OV_SIGNATUREBYTES, OV_SIGNATUREBYTES * 8);
    printf("  Public key: %d bytes (classic)\n", OV_PK_UNCOMPRESSED_BYTES);
    printf("  Public key: %d bytes (compressed)\n", OV_PK_COMPRESSED_BYTES);
    printf("  Hash recovered from P(s): %d bytes = %d bits\n", _PUB_M_BYTE, _PUB_M_BYTE * 8);
    printf("  Salt in signature: %d bytes\n", _SALT_BYTE);
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
    printf("  Public key: %d bytes (exchanged out-of-band, e.g., via QR code or URL)\n",
           CRYPTO_PUBLICKEYBYTES);
    printf("\n");

    // ============================================================
    // Step 2: Sender signs a message
    // ============================================================
    printf("--- Step 2: Sender signs a message ---\n");

    // The "message" is some data whose hash we want to authenticate.
    // In the stego use case, this could be an image fingerprint, a document hash, etc.
    const char *original_message = "This is the original content to be authenticated via steganography.";
    size_t mlen = strlen(original_message);

    // Compute the hash of the message (this is what gets "embedded" in the signature)
    uint8_t message_hash[_PUB_M_BYTE];
    {
        // The UOV signing process internally computes H(message || salt),
        // which maps to _PUB_M_BYTE output. We compute it here just for display.
        hash_ctx hctx;
        hash_init(&hctx);
        hash_update(&hctx, (const uint8_t *)original_message, mlen);
        // Note: the actual hash includes the salt, which is in the signature
        hash_final_digest(message_hash, _PUB_M_BYTE, &hctx);
    }
    printf("  Message: \"%s\"\n", original_message);
    print_hex("H(message) [first bytes]", message_hash, _PUB_M_BYTE);
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
    print_hex("Signature (w || salt)", signature, siglen);
    printf("    w (public map input): %d bytes = %d bits\n", _PUB_N_BYTE, _PUB_N_BYTE * 8);
    printf("    salt:                 %d bytes = %d bits\n", _SALT_BYTE, _SALT_BYTE * 8);
    printf("    TOTAL transmitted:    %d bytes = %d bits\n", OV_SIGNATUREBYTES, OV_SIGNATUREBYTES * 8);
    printf("\n");

    // ============================================================
    // Step 3: The stego channel
    // ============================================================
    printf("--- Step 3: Stego Channel Transmission ---\n");
    printf("  ONLY the signature is transmitted through the stego channel.\n");
    printf("  The original message is NOT transmitted.\n");
    printf("  Transmitted payload: %d bits (before outer ECC)\n", OV_SIGNATUREBYTES * 8);
    printf("\n");

    // ============================================================
    // Step 4: Receiver performs message recovery + verification
    // ============================================================
    printf("--- Step 4: Receiver - Message Recovery ---\n");

    // The receiver has:
    //   - The signature (received via stego channel)
    //   - The public key (received out-of-band)
    //   - The original message (they have their own copy, or can reconstruct it)

    // Step 4a: Recover the digest from the signature using the public map
    uint8_t recovered_digest[_PUB_M_BYTE];
    const uint8_t *w = signature;  // first _PUB_N_BYTE bytes are the signature vector
    ov_publicmap(recovered_digest, ((const pk_t *)pk)->pk, w);

    print_hex("Recovered digest P(s)", recovered_digest, _PUB_M_BYTE);
    printf("  This is H(message || salt) = the authenticated hash!\n");
    printf("  Recovered %d bits of hash from %d-bit signature.\n",
           _PUB_M_BYTE * 8, OV_SIGNATUREBYTES * 8);
    printf("\n");

    // Step 4b: Verify the signature against the original message
    printf("--- Step 4b: Verification ---\n");

    // The verifier needs the original message to check:
    //   P(s) == H(message || salt)
    // where salt is extracted from the signature
    rc = crypto_sign_verify(signature, siglen, (const uint8_t *)original_message, mlen, pk);
    printf("  Verification result: %s\n", rc == 0 ? "VALID" : "INVALID");
    printf("\n");

    // Step 4c: Demonstrate that the recovered digest matches
    printf("--- Step 4c: Digest Comparison ---\n");
    const uint8_t *salt_from_sig = signature + _PUB_N_BYTE;
    uint8_t expected_digest[_PUB_M_BYTE];
    {
        hash_ctx hctx;
        hash_init(&hctx);
        hash_update(&hctx, (const uint8_t *)original_message, mlen);
        hash_update(&hctx, salt_from_sig, _SALT_BYTE);
        hash_final_digest(expected_digest, _PUB_M_BYTE, &hctx);
    }
    print_hex("Expected  H(msg||salt)", expected_digest, _PUB_M_BYTE);
    print_hex("Recovered P(s)        ", recovered_digest, _PUB_M_BYTE);
    int match = (memcmp(recovered_digest, expected_digest, _PUB_M_BYTE) == 0);
    printf("  Match: %s\n", match ? "YES - message recovery confirmed!" : "NO - ERROR!");
    printf("\n");

    // ============================================================
    // Summary
    // ============================================================
    printf("=================================================================\n");
    printf("  SUMMARY FOR STEGO CHANNEL DESIGN\n");
    printf("=================================================================\n");
    printf("  What goes through the stego channel:\n");
    printf("    - Signature only: %d bytes = %d bits\n", OV_SIGNATUREBYTES, OV_SIGNATUREBYTES * 8);
    printf("    - NO separate digest needed (recovered via public map)\n");
    printf("\n");
    printf("  What the verifier recovers:\n");
    printf("    - Authenticated hash: %d bytes = %d bits\n", _PUB_M_BYTE, _PUB_M_BYTE * 8);
    printf("    - This hash has %d-bit collision resistance\n", _PUB_M_BYTE * 8 / 2);
    printf("    - And %d-bit preimage resistance\n", _PUB_M_BYTE * 8);
    printf("\n");
    printf("  What goes out-of-band (one-time setup):\n");
    printf("    - Public key: %d bytes\n", CRYPTO_PUBLICKEYBYTES);
    printf("=================================================================\n");

    free(pk);
    free(sk);
    return 0;
}
