// SPDX-License-Identifier: Apache-2.0
/**
 * stego_demo.c - Unified stego channel demo (scheme-agnostic)
 *
 * Works identically with UOV and BLS backends. The caller provides a pHash
 * and gets back a payload; on verification, recovers/extracts the pHash.
 *
 * Build with:
 *   make stego_demo SCHEME=uov-80      (UOV, 80-bit, post-quantum)
 *   make stego_demo SCHEME=uov-100     (UOV, 100-bit, post-quantum)
 *   make stego_demo SCHEME=bls-bn158   (BLS, ~78-bit, classical)
 *   make stego_demo SCHEME=bls12-381   (BLS, ~120-bit, classical)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "stego_sig.h"

static void print_hex(const char *label, const uint8_t *data, int len) {
    int show = len > 32 ? 32 : len;
    printf("  %s (%d B = %d bits): ", label, len, len * 8);
    for (int i = 0; i < show; i++) printf("%02x", data[i]);
    if (len > 32) printf("...");
    printf("\n");
}

/* pHash sizes to demonstrate */
static const int PHASH_SIZES[] = { 12, 18, 23 };  /* 96, 144, 184 bits */
static const int NUM_PHASH = 3;

int main(void) {
    int rc;

    printf("=================================================================\n");
    printf("  Stego Channel Signature Demo (Unified API)\n");
    printf("=================================================================\n\n");

    /* Init */
    if (stego_init() != STEGO_OK) {
        fprintf(stderr, "Failed to initialize signature backend\n");
        return 1;
    }

    printf("Scheme:           %s\n", stego_scheme_name());
    printf("Security:         %d bits (classical)\n", stego_security_bits());
    printf("Post-quantum:     %s\n", stego_is_post_quantum() ? "Yes" : "No");
    printf("Message recovery: %s\n", stego_has_message_recovery()
           ? "Yes (pHash recovered from sig)" : "No (pHash transmitted explicitly)");
    printf("Signature:        %d bytes = %d bits\n", stego_sig_bytes(), stego_sig_bytes() * 8);
    printf("Public key:       %d bytes = %d bits\n", stego_pk_bytes(), stego_pk_bytes() * 8);
    printf("Max pHash:        %d bytes = %d bits\n", stego_max_phash_bytes(), stego_max_phash_bytes() * 8);
    printf("\n");

    /* ============================================================ */
    /* Step 1: Key Generation                                        */
    /* ============================================================ */
    printf("--- Step 1: Key Generation ---\n");

    int pk_sz = stego_pk_bytes();
    int sk_sz = stego_sk_bytes();
    uint8_t *pk = malloc(pk_sz);
    uint8_t *sk = malloc(sk_sz);
    int pk_len, sk_len;

    if (!pk || !sk || stego_keygen(pk, &pk_len, sk, &sk_len) != STEGO_OK) {
        fprintf(stderr, "Key generation failed\n");
        goto cleanup;
    }
    print_hex("Public key", pk, pk_len > 48 ? 48 : pk_len);
    printf("  (PK total: %d bytes, SK total: %d bytes)\n", pk_len, sk_len);
    printf("\n");

    /* ============================================================ */
    /* Step 2: Payload size table                                    */
    /* ============================================================ */
    printf("--- Step 2: Payload Sizes ---\n\n");
    int max_ph = stego_max_phash_bytes();
    printf("  %-10s | %-14s | %-14s | %s\n", "pHash bits", "No PK (bits)", "With PK (bits)", "");
    printf("  %s\n", "-----------|----------------|----------------|---");
    for (int i = 0; i < NUM_PHASH; i++) {
        int ph = PHASH_SIZES[i];
        if (ph > max_ph) {
            printf("  %-10d | %-14s | %-14s | REJECTED (max %d bits)\n",
                   ph * 8, "N/A", "N/A", max_ph * 8);
        } else {
            printf("  %-10d | %-14d | %-14d |\n",
                   ph * 8,
                   stego_payload_bytes(ph, 0) * 8,
                   stego_payload_bytes(ph, 1) * 8);
        }
    }
    printf("\n");

    /* Allocate payload buffer large enough for any variant */
    int max_payload = stego_payload_bytes(23, 1) + 64;
    uint8_t *payload = malloc(max_payload);
    uint8_t recovered[STEGO_MAX_PHASH_BYTES];
    int payload_len, recovered_len;

    /* Simulated pHash */
    uint8_t phash[23];
    for (int i = 0; i < 23; i++) phash[i] = (uint8_t)(0xde + i * 0x11);

    /* ============================================================ */
    /* Step 3: Sign & verify each pHash size (PK out-of-band)        */
    /* ============================================================ */
    printf("--- Step 3: Sign & Verify (PK out-of-band) ---\n\n");

    for (int i = 0; i < NUM_PHASH; i++) {
        int ph_len = PHASH_SIZES[i];

        rc = stego_sign(payload, &payload_len,
                        phash, ph_len, sk, sk_len,
                        NULL, 0,   /* random salt / ignored */
                        0, NULL, 0);
        if (rc != STEGO_OK) {
            if (ph_len > stego_max_phash_bytes()) {
                printf("  %3d-bit pHash: REJECTED (exceeds max %d bits, would be truncated)\n",
                       ph_len * 8, stego_max_phash_bytes() * 8);
            } else {
                printf("  %3d-bit pHash: SIGN FAILED\n", ph_len * 8);
            }
            continue;
        }

        rc = stego_verify(payload, payload_len, ph_len,
                          recovered, &recovered_len, pk, pk_len);

        printf("  %3d-bit pHash: payload %3d bits, verify %s, recovered %d bits",
               ph_len * 8, payload_len * 8,
               rc == STEGO_OK ? "OK" : "FAIL",
               recovered_len * 8);

        if (!stego_has_message_recovery()) {
            /* BLS: recovered bytes ARE the raw pHash */
            int match = (rc == STEGO_OK) && (recovered_len == ph_len) &&
                        (memcmp(recovered, phash, ph_len) == 0);
            printf(", pHash match: %s", match ? "YES" : "NO");
        }
        /* UOV: recovered bytes are H(pHash)[truncated], not raw pHash.
         * The caller would compare against their own H(pHash). */
        printf("\n");
    }
    printf("\n");

    /* ============================================================ */
    /* Step 4: Sign & verify with PK in-band                         */
    /* ============================================================ */
    printf("--- Step 4: Sign & Verify (PK in-band) ---\n\n");

    rc = stego_sign(payload, &payload_len,
                    phash, 18, sk, sk_len,
                    NULL, 0,
                    1, pk, pk_len);  /* append PK */

    if (rc == STEGO_OK) {
        printf("  Payload (with PK): %d bytes = %d bits\n", payload_len, payload_len * 8);

        /* Verify using embedded PK */
        rc = stego_verify(payload, payload_len, 18,
                          recovered, &recovered_len, NULL, 0);
        printf("  Verify (embedded PK): %s\n", rc == STEGO_OK ? "VALID" : "INVALID");

        /* Verify using external PK */
        rc = stego_verify(payload, payload_len, 18,
                          recovered, &recovered_len, pk, pk_len);
        printf("  Verify (external PK): %s\n", rc == STEGO_OK ? "VALID" : "INVALID");
    }
    printf("\n");

    /* ============================================================ */
    /* Step 5: Tamper detection                                      */
    /* ============================================================ */
    printf("--- Step 5: Tamper Detection ---\n\n");

    rc = stego_sign(payload, &payload_len,
                    phash, 18, sk, sk_len, NULL, 0, 0, NULL, 0);
    if (rc == STEGO_OK) {
        /* Verify original: get recovered hash */
        uint8_t orig_hash[STEGO_MAX_PHASH_BYTES];
        int orig_hash_len;
        rc = stego_verify(payload, payload_len, 18,
                          orig_hash, &orig_hash_len, pk, pk_len);
        printf("  Original:  verify=%s\n", rc == STEGO_OK ? "OK" : "FAIL");

        /* Tamper: flip one bit in payload */
        uint8_t *tampered = malloc(payload_len);
        memcpy(tampered, payload, payload_len);
        tampered[0] ^= 0x01;

        uint8_t tamp_hash[STEGO_MAX_PHASH_BYTES];
        int tamp_hash_len;
        rc = stego_verify(tampered, payload_len, 18,
                          tamp_hash, &tamp_hash_len, pk, pk_len);

        if (stego_has_message_recovery()) {
            /* UOV: tampered sig produces a different recovered hash */
            int hash_changed = (orig_hash_len != tamp_hash_len ||
                                memcmp(orig_hash, tamp_hash, orig_hash_len) != 0);
            printf("  Tampered:  recovered hash changed: %s (tamper detected)\n",
                   hash_changed ? "YES" : "NO (ERROR!)");
        } else {
            /* BLS: verify returns FAIL for tampered payload */
            printf("  Tampered:  %s (expected INVALID)\n",
                   rc == STEGO_OK ? "VALID (ERROR!)" : "INVALID");
        }
        free(tampered);
    }
    printf("\n");

    /* ============================================================ */
    /* Step 6: User-provided salt (UOV only)                         */
    /* ============================================================ */
    if (stego_has_message_recovery()) {
        printf("--- Step 6: User-Provided Salt (UOV) ---\n\n");

        const char *salt_str = "image-id-2024";
        uint8_t *p1 = malloc(max_payload), *p2 = malloc(max_payload);
        int l1, l2;

        stego_sign(p1, &l1, phash, 18, sk, sk_len,
                   (const uint8_t *)salt_str, strlen(salt_str), 0, NULL, 0);
        stego_sign(p2, &l2, phash, 18, sk, sk_len,
                   (const uint8_t *)salt_str, strlen(salt_str), 0, NULL, 0);
        printf("  Same salt = same sig:      %s\n",
               (l1 == l2 && memcmp(p1, p2, l1) == 0) ? "YES" : "NO");

        stego_sign(p2, &l2, phash, 18, sk, sk_len,
                   (const uint8_t *)"other", 5, 0, NULL, 0);
        printf("  Different salt = diff sig: %s\n",
               (l1 != l2 || memcmp(p1, p2, l1) != 0) ? "YES" : "NO");

        free(p1); free(p2);
        printf("\n");
    }

    /* ============================================================ */
    /* Summary                                                       */
    /* ============================================================ */
    printf("=================================================================\n");
    printf("  SUMMARY: %s\n", stego_scheme_name());
    printf("=================================================================\n");
    printf("  Security:         %d bits (classical), %s\n",
           stego_security_bits(),
           stego_is_post_quantum() ? "post-quantum" : "NOT quantum-safe");
    printf("  Message recovery: %s\n",
           stego_has_message_recovery() ? "Yes" : "No");
    printf("  Signature:        %d bits\n", stego_sig_bytes() * 8);
    printf("  Public key:       %d bits\n", stego_pk_bytes() * 8);
    printf("  Max pHash:        %d bits\n", stego_max_phash_bytes() * 8);
    printf("\n");
    printf("  Payload sizes:\n");
    for (int i = 0; i < NUM_PHASH; i++) {
        int ph = PHASH_SIZES[i];
        if (ph > stego_max_phash_bytes()) {
            printf("    %3d-bit pHash: REJECTED (exceeds max %d bits)\n",
                   ph * 8, stego_max_phash_bytes() * 8);
            continue;
        }
        printf("    %3d-bit pHash: %4d bits (no PK) / %4d bits (with PK)\n",
               ph * 8,
               stego_payload_bytes(ph, 0) * 8,
               stego_payload_bytes(ph, 1) * 8);
    }
    printf("=================================================================\n");

cleanup:
    free(payload);
    free(pk);
    free(sk);
    stego_cleanup();
    return 0;
}
