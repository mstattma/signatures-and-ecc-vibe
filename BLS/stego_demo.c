// SPDX-License-Identifier: Apache-2.0
/**
 * stego_demo.c - BLS signature stego channel demo
 *
 * Demonstrates BLS signature generation and verification for a
 * bandwidth-constrained steganographic channel, analogous to the
 * UOV stego demo but without message recovery (pHash must be
 * transmitted explicitly).
 *
 * Payload format: [pHash] || [BLS signature (G1)] || [optional: PK (G2)]
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
    printf("  Sig + PK total:    %d bytes = %d bits\n", sig_bytes + pk_bytes,
           (sig_bytes + pk_bytes) * 8);
    printf("\n");

    /* ============================================================ */
    /* Step 1: Key Generation (PK exchanged out-of-band or in-band) */
    /* ============================================================ */
    printf("--- Step 1: Key Generation ---\n");

    bls_keypair_t *kp = bls_keypair_gen();
    if (!kp) {
        fprintf(stderr, "Key generation failed\n");
        bls_stego_cleanup();
        return 1;
    }

    uint8_t pk_buf[256];  /* max PK size */
    int pk_len;
    bls_pk_export(pk_buf, &pk_len, kp);
    printf("  Key pair generated.\n");
    print_hex("Public key (G2)", pk_buf, pk_len);
    printf("\n");

    /* ============================================================ */
    /* Step 2: Sign a perceptual hash                                */
    /* ============================================================ */
    printf("--- Step 2: Sign a perceptual hash ---\n");

    /* Simulate a perceptual hash (use max size for signing, truncate for payload) */
    uint8_t phash_full[23];
    for (int i = 0; i < 23; i++) phash_full[i] = (uint8_t)(0xde + i * 0x11);

    print_hex("Perceptual hash (184 bits)", phash_full, 23);

    /* Sign the full pHash */
    uint8_t sig_buf[256];  /* max sig size */
    int sig_len;
    if (bls_sign(sig_buf, &sig_len, phash_full, 23, kp) != BLS_OK) {
        fprintf(stderr, "Signing failed\n");
        bls_keypair_free(kp);
        bls_stego_cleanup();
        return 1;
    }

    print_hex("BLS signature (G1)", sig_buf, sig_len);
    printf("\n");

    /* ============================================================ */
    /* Step 3: Verify the signature                                  */
    /* ============================================================ */
    printf("--- Step 3: Verification ---\n");

    int valid = bls_verify(sig_buf, sig_len, phash_full, 23, pk_buf, pk_len);
    printf("  Verification result: %s\n", valid ? "VALID" : "INVALID");

    /* Tamper test */
    uint8_t tampered[23];
    memcpy(tampered, phash_full, 23);
    tampered[0] ^= 0x01;
    int invalid = bls_verify(sig_buf, sig_len, tampered, 23, pk_buf, pk_len);
    printf("  Tampered message:    %s (expected INVALID)\n", invalid ? "VALID (ERROR!)" : "INVALID");
    printf("\n");

    /* ============================================================ */
    /* Step 4: Stego channel payload assembly                        */
    /* ============================================================ */
    printf("--- Step 4: Stego Channel Payload ---\n");
    printf("\n");
    printf("  Payload format: [pHash] || [signature] || [optional: PK]\n");
    printf("\n");

    /* Show payload sizes for each pHash variant */
    printf("  %-10s | %-12s | %-12s | %-14s | %-14s\n",
           "pHash bits", "pHash bytes", "Sig bytes", "No PK (bits)", "With PK (bits)");
    printf("  %s\n",
           "-----------|-------------|-------------|----------------|----------------");

    for (int i = 0; i < NUM_PHASH_SIZES; i++) {
        int ph_len = PHASH_SIZES[i];
        int ph_bits = ph_len * 8;
        int no_pk_bits = (ph_len + sig_len) * 8;
        int with_pk_bits = (ph_len + sig_len + pk_len) * 8;
        printf("  %-10d | %-11d | %-11d | %-14d | %-14d\n",
               ph_bits, ph_len, sig_len, no_pk_bits, with_pk_bits);
    }
    printf("\n");

    /* Demonstrate actual payload assembly for 144-bit pHash */
    printf("  Example: 144-bit pHash payload assembly:\n");
    uint8_t payload[512];
    int payload_len;

    /* Without PK */
    bls_payload_assemble(payload, &payload_len,
                         phash_full, 18, sig_buf, sig_len, NULL, 0);
    printf("    Without PK: %d bytes = %d bits\n", payload_len, payload_len * 8);
    print_hex("  Payload (no PK)", payload, payload_len);

    /* With PK */
    bls_payload_assemble(payload, &payload_len,
                         phash_full, 18, sig_buf, sig_len, pk_buf, pk_len);
    printf("    With PK:    %d bytes = %d bits\n", payload_len, payload_len * 8);
    print_hex("  Payload (with PK)", payload, payload_len);
    printf("\n");

    /* ============================================================ */
    /* Step 5: Receiver disassembles and verifies                    */
    /* ============================================================ */
    printf("--- Step 5: Receiver - Disassemble & Verify ---\n");

    const uint8_t *rx_phash, *rx_sig, *rx_pk;
    int rx_pk_len;

    /* Sign the 144-bit (18 byte) pHash for the round-trip demo */
    uint8_t sig144[256];
    int sig144_len;
    bls_sign(sig144, &sig144_len, phash_full, 18, kp);

    /* Case A: PK received out-of-band (payload = pHash || sig) */
    bls_payload_assemble(payload, &payload_len,
                         phash_full, 18, sig144, sig144_len, NULL, 0);
    bls_payload_disassemble(payload, payload_len, 18, sig144_len,
                            &rx_phash, &rx_sig, &rx_pk, &rx_pk_len);
    /* Use out-of-band PK */
    valid = bls_verify(rx_sig, sig144_len, rx_phash, 18, pk_buf, pk_len);
    printf("  Case A (PK out-of-band): %s\n", valid ? "VALID" : "INVALID");

    /* Case B: PK in-band (payload = pHash || sig || pk) */
    bls_payload_assemble(payload, &payload_len,
                         phash_full, 18, sig144, sig144_len, pk_buf, pk_len);
    bls_payload_disassemble(payload, payload_len, 18, sig144_len,
                            &rx_phash, &rx_sig, &rx_pk, &rx_pk_len);
    valid = bls_verify(rx_sig, sig144_len, rx_phash, 18, rx_pk, rx_pk_len);
    printf("  Case B (PK in-band):     %s\n", valid ? "VALID" : "INVALID");
    printf("\n");

    /* ============================================================ */
    /* Step 6: Sign different pHash sizes                            */
    /* ============================================================ */
    printf("--- Step 6: Multiple pHash Sizes ---\n");

    for (int i = 0; i < NUM_PHASH_SIZES; i++) {
        int ph_len = PHASH_SIZES[i];
        uint8_t s[256];
        int sl;
        if (bls_sign(s, &sl, phash_full, ph_len, kp) == BLS_OK) {
            int v = bls_verify(s, sl, phash_full, ph_len, pk_buf, pk_len);
            printf("  pHash %3d bits: sign OK, verify %s, payload %d bits (no PK) / %d bits (with PK)\n",
                   ph_len * 8, v ? "OK" : "FAIL",
                   (ph_len + sl) * 8,
                   (ph_len + sl + pk_len) * 8);
        }
    }
    printf("\n");

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
    printf("    Security: %d bits (classical), 0 bits (quantum - broken by Shor)\n",
           bls_stego_security_bits());
    printf("    Message recovery: NO (pHash must be transmitted explicitly)\n");
    printf("\n");
    printf("  Payload sizes (pHash || signature):\n");
    for (int i = 0; i < NUM_PHASH_SIZES; i++) {
        int ph = PHASH_SIZES[i];
        printf("    %3d-bit pHash: %4d bits (no PK) / %4d bits (with PK)\n",
               ph * 8, (ph + sig_bytes) * 8, (ph + sig_bytes + pk_bytes) * 8);
    }
    printf("\n");
    printf("  Comparison with UOV (salt-in-digest, signature only):\n");
    printf("    UOV-80:  400 bits (recovers 144-bit hash + 16-bit salt)\n");
    printf("    UOV-100: 504 bits (recovers 184-bit hash + 16-bit salt)\n");
    printf("    %s + 144-bit pHash: %d bits (no PK)\n",
           bls_stego_curve_name(), (18 + sig_bytes) * 8);
    printf("=================================================================\n");

    bls_keypair_free(kp);
    bls_stego_cleanup();
    return 0;
}
