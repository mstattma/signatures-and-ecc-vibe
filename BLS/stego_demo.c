// SPDX-License-Identifier: Apache-2.0
/**
 * stego_demo.c - BLS signature stego channel demo
 *
 * Demonstrates BLS signature generation and verification for a
 * bandwidth-constrained steganographic channel.
 *
 * Payload format: [pHash] || [salt] || [BLS signature (G1)] || [optional: PK (G2)]
 * Signing input:  pHash || salt
 *
 * The salt ensures that similar images with identical perceptual hashes
 * produce different signatures.
 *
 * Supports BLS12-381 and BN-158 (selected at compile time via RELIC config).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "bls_stego.h"

static void print_hex(const char *label, const uint8_t *data, size_t len) {
    printf("  %s (%zu bytes = %zu bits): ", label, len, len * 8);
    for (size_t i = 0; i < len && i < 32; i++) {
        printf("%02x", data[i]);
    }
    if (len > 32) printf("...");
    printf("\n");
}

/* pHash sizes to demonstrate */
static const int PHASH_SIZES[] = { 12, 18, 23 };  /* 96, 144, 184 bits */
static const int NUM_PHASH_SIZES = 3;

int main(void) {
    printf("=================================================================\n");
    printf("  BLS Signature for Steganographic Channel - Demo\n");
    printf("=================================================================\n\n");

    /* ============================================================ */
    /* Step 0: Initialize library                                    */
    /* ============================================================ */
    if (bls_stego_init() != BLS_OK) {
        fprintf(stderr, "Failed to initialize BLS library\n");
        return 1;
    }

    int sig_bytes = bls_stego_sig_bytes();
    int pk_bytes  = bls_stego_pk_bytes();
    int sk_bytes  = bls_stego_sk_bytes();

    printf("Curve: %s\n", bls_stego_curve_name());
    printf("  Security level:    %d bits\n", bls_stego_security_bits());
    printf("  Signature (G1):    %d bytes = %d bits (compressed)\n", sig_bytes, sig_bytes * 8);
    printf("  Public key (G2):   %d bytes = %d bits (compressed)\n", pk_bytes, pk_bytes * 8);
    printf("  Secret key:        %d bytes = %d bits\n", sk_bytes, sk_bytes * 8);
    printf("  Salt:              %d bytes = %d bits\n", BLS_SALT_BYTE, BLS_SALT_BYTE * 8);
    printf("\n");

    /* ============================================================ */
    /* Step 1: Key Generation                                        */
    /* ============================================================ */
    printf("--- Step 1: Key Generation ---\n");

    bls_keypair_t *kp = bls_keypair_gen();
    if (!kp) {
        fprintf(stderr, "Key generation failed\n");
        bls_stego_cleanup();
        return 1;
    }

    uint8_t pk_buf[256];
    int pk_len;
    bls_pk_export(pk_buf, &pk_len, kp);
    printf("  Key pair generated.\n");
    print_hex("Public key (G2)", pk_buf, pk_len);
    printf("\n");

    /* ============================================================ */
    /* Step 2: Sign a perceptual hash (random salt)                  */
    /* ============================================================ */
    printf("--- Step 2: Sign a perceptual hash (random salt) ---\n");

    uint8_t phash_full[23];
    for (int i = 0; i < 23; i++) phash_full[i] = (uint8_t)(0xde + i * 0x11);

    print_hex("Perceptual hash (184 bits)", phash_full, 23);

    uint8_t sig_buf[256];
    int sig_len;
    uint8_t salt_used[BLS_SALT_BYTE > 0 ? BLS_SALT_BYTE : 1];
    if (bls_sign(sig_buf, &sig_len, salt_used,
                 phash_full, 23, NULL, 0, kp) != BLS_OK) {
        fprintf(stderr, "Signing failed\n");
        bls_keypair_free(kp);
        bls_stego_cleanup();
        return 1;
    }

    print_hex("BLS signature (G1)", sig_buf, sig_len);
#if BLS_SALT_BYTE > 0
    print_hex("Salt", salt_used, BLS_SALT_BYTE);
#endif
    printf("\n");

    /* ============================================================ */
    /* Step 3: Verification                                          */
    /* ============================================================ */
    printf("--- Step 3: Verification ---\n");

    int valid = bls_verify(sig_buf, sig_len, phash_full, 23, salt_used, pk_buf, pk_len);
    printf("  Verification result: %s\n", valid ? "VALID" : "INVALID");

    /* Tamper test */
    uint8_t tampered[23];
    memcpy(tampered, phash_full, 23);
    tampered[0] ^= 0x01;
    int invalid = bls_verify(sig_buf, sig_len, tampered, 23, salt_used, pk_buf, pk_len);
    printf("  Tampered pHash:      %s (expected INVALID)\n", invalid ? "VALID (ERROR!)" : "INVALID");
    printf("\n");

    /* ============================================================ */
    /* Step 4: Payload assembly                                      */
    /* ============================================================ */
    printf("--- Step 4: Stego Channel Payload ---\n");
    printf("\n");
    printf("  Payload format: [pHash] || [salt (%d B)] || [signature] || [optional: PK]\n", BLS_SALT_BYTE);
    printf("\n");

    printf("  %-10s | %-12s | %-6s | %-12s | %-14s | %-14s\n",
           "pHash bits", "pHash bytes", "Salt", "Sig bytes", "No PK (bits)", "With PK (bits)");
    printf("  %s\n",
           "-----------|-------------|--------|-------------|----------------|----------------");

    for (int i = 0; i < NUM_PHASH_SIZES; i++) {
        int ph_len = PHASH_SIZES[i];
        int ph_bits = ph_len * 8;
        int no_pk_bits = (ph_len + BLS_SALT_BYTE + sig_len) * 8;
        int with_pk_bits = (ph_len + BLS_SALT_BYTE + sig_len + pk_len) * 8;
        printf("  %-10d | %-11d | %-6d | %-11d | %-14d | %-14d\n",
               ph_bits, ph_len, BLS_SALT_BYTE, sig_len, no_pk_bits, with_pk_bits);
    }
    printf("\n");

    /* Example payload for 144-bit pHash */
    printf("  Example: 144-bit pHash payload assembly:\n");
    uint8_t payload[512];
    int payload_len;

    /* Sign 18-byte pHash for the round-trip demo */
    uint8_t sig18[256];
    int sig18_len;
    uint8_t salt18[BLS_SALT_BYTE > 0 ? BLS_SALT_BYTE : 1];
    bls_sign(sig18, &sig18_len, salt18, phash_full, 18, NULL, 0, kp);

    /* Without PK */
    bls_payload_assemble(payload, &payload_len,
                         phash_full, 18, salt18, sig18, sig18_len, NULL, 0);
    printf("    Without PK: %d bytes = %d bits\n", payload_len, payload_len * 8);
    print_hex("  Payload (no PK)", payload, payload_len);

    /* With PK */
    bls_payload_assemble(payload, &payload_len,
                         phash_full, 18, salt18, sig18, sig18_len, pk_buf, pk_len);
    printf("    With PK:    %d bytes = %d bits\n", payload_len, payload_len * 8);
    printf("\n");

    /* ============================================================ */
    /* Step 5: Receiver disassembles and verifies                    */
    /* ============================================================ */
    printf("--- Step 5: Receiver - Disassemble & Verify ---\n");

    const uint8_t *rx_phash, *rx_salt, *rx_sig, *rx_pk;
    int rx_pk_len;

    /* Case A: PK out-of-band */
    bls_payload_assemble(payload, &payload_len,
                         phash_full, 18, salt18, sig18, sig18_len, NULL, 0);
    bls_payload_disassemble(payload, payload_len, 18, sig18_len,
                            &rx_phash, &rx_salt, &rx_sig, &rx_pk, &rx_pk_len);
    valid = bls_verify(rx_sig, sig18_len, rx_phash, 18, rx_salt, pk_buf, pk_len);
    printf("  Case A (PK out-of-band): %s\n", valid ? "VALID" : "INVALID");

    /* Case B: PK in-band */
    bls_payload_assemble(payload, &payload_len,
                         phash_full, 18, salt18, sig18, sig18_len, pk_buf, pk_len);
    bls_payload_disassemble(payload, payload_len, 18, sig18_len,
                            &rx_phash, &rx_salt, &rx_sig, &rx_pk, &rx_pk_len);
    valid = bls_verify(rx_sig, sig18_len, rx_phash, 18, rx_salt, rx_pk, rx_pk_len);
    printf("  Case B (PK in-band):     %s\n", valid ? "VALID" : "INVALID");
    printf("\n");

#if BLS_SALT_BYTE > 0
    /* ============================================================ */
    /* Step 6: User-provided salt                                    */
    /* ============================================================ */
    printf("--- Step 6: User-provided salt ---\n");

    const char *user_salt_str = "my-custom-salt";
    uint8_t sig_us[256];
    int sig_us_len;
    uint8_t salt_us[BLS_SALT_BYTE];
    bls_sign(sig_us, &sig_us_len, salt_us, phash_full, 18,
             (const uint8_t *)user_salt_str, strlen(user_salt_str), kp);
    print_hex("Salt (from user salt)", salt_us, BLS_SALT_BYTE);
    valid = bls_verify(sig_us, sig_us_len, phash_full, 18, salt_us, pk_buf, pk_len);
    printf("  Verify with user salt: %s\n", valid ? "VALID" : "INVALID");

    /* Same salt = same signature (deterministic) */
    uint8_t sig_us2[256];
    int sig_us2_len;
    uint8_t salt_us2[BLS_SALT_BYTE];
    bls_sign(sig_us2, &sig_us2_len, salt_us2, phash_full, 18,
             (const uint8_t *)user_salt_str, strlen(user_salt_str), kp);
    int same = (sig_us_len == sig_us2_len && memcmp(sig_us, sig_us2, sig_us_len) == 0);
    printf("  Same salt + same pHash = same sig: %s\n", same ? "YES (deterministic)" : "NO (ERROR!)");

    /* Different random salts = different signatures */
    uint8_t sig_r1[256], sig_r2[256];
    int sig_r1_len, sig_r2_len;
    uint8_t salt_r1[BLS_SALT_BYTE], salt_r2[BLS_SALT_BYTE];
    bls_sign(sig_r1, &sig_r1_len, salt_r1, phash_full, 18, NULL, 0, kp);
    bls_sign(sig_r2, &sig_r2_len, salt_r2, phash_full, 18, NULL, 0, kp);
    int diff = (sig_r1_len != sig_r2_len || memcmp(sig_r1, sig_r2, sig_r1_len) != 0);
    printf("  Random salts = different sigs: %s\n", diff ? "YES" : "NO (unlikely but possible)");
    printf("\n");
#endif

    /* ============================================================ */
    /* Summary                                                       */
    /* ============================================================ */
    printf("=================================================================\n");
    printf("  SUMMARY FOR STEGO CHANNEL DESIGN (%s)\n", bls_stego_curve_name());
    printf("=================================================================\n");
    printf("\n");
    printf("  Signature scheme: BLS (min-signature mode)\n");
    printf("    Signature in G1: %d bytes = %d bits\n", sig_bytes, sig_bytes * 8);
    printf("    Public key in G2: %d bytes = %d bits\n", pk_bytes, pk_bytes * 8);
    printf("    Salt: %d bytes = %d bits\n", BLS_SALT_BYTE, BLS_SALT_BYTE * 8);
    printf("    Security: %d bits (classical), 0 bits (quantum - broken by Shor)\n",
           bls_stego_security_bits());
    printf("    Message recovery: NO (pHash + salt transmitted explicitly)\n");
    printf("\n");
    printf("  Payload sizes (pHash || salt || signature):\n");
    for (int i = 0; i < NUM_PHASH_SIZES; i++) {
        int ph = PHASH_SIZES[i];
        printf("    %3d-bit pHash: %4d bits (no PK) / %4d bits (with PK)\n",
               ph * 8,
               (ph + BLS_SALT_BYTE + sig_bytes) * 8,
               (ph + BLS_SALT_BYTE + sig_bytes + pk_bytes) * 8);
    }
    printf("=================================================================\n");

    bls_keypair_free(kp);
    bls_stego_cleanup();
    return 0;
}
