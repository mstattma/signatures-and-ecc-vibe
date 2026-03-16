#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "stego_sig.h"

static int hex_nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int hex_to_bytes(const char *hex, uint8_t *out, int max_out) {
    int len = (int)strlen(hex);
    if (len >= 2 && hex[0] == '0' && (hex[1] == 'x' || hex[1] == 'X')) {
        hex += 2;
        len -= 2;
    }
    if ((len % 2) != 0 || len / 2 > max_out) return -1;
    for (int i = 0; i < len / 2; i++) {
        int hi = hex_nibble(hex[i * 2]);
        int lo = hex_nibble(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0) return -1;
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return len / 2;
}

static void bytes_to_hex(const uint8_t *in, int in_len, char *out) {
    static const char *hex = "0123456789abcdef";
    for (int i = 0; i < in_len; i++) {
        out[i * 2] = hex[in[i] >> 4];
        out[i * 2 + 1] = hex[in[i] & 0x0f];
    }
    out[in_len * 2] = '\0';
}

static int write_bin(const char *path, const uint8_t *buf, int len) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    int ok = fwrite(buf, 1, len, f) == (size_t)len ? 0 : -1;
    fclose(f);
    return ok;
}

static int read_bin(const char *path, uint8_t *buf, int max_len) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    int len = (int)fread(buf, 1, max_len, f);
    fclose(f);
    return len;
}

static int cmd_generate(const char *phash_hex, const char *payload_path,
                        const char *pk_path, const char *phash_path,
                        const char *sk_import_path) {
    uint8_t phash[STEGO_MAX_PHASH_BYTES];
    int phash_len = hex_to_bytes(phash_hex, phash, sizeof(phash));
    if (phash_len <= 0) {
        fprintf(stderr, "Invalid pHash hex\n");
        return 1;
    }

    if (stego_init() != STEGO_OK) {
        fprintf(stderr, "stego_init failed\n");
        return 1;
    }

    uint8_t *pk = malloc(stego_pk_bytes());
    uint8_t *sk = malloc(stego_sk_bytes());
    uint8_t payload[STEGO_MAX_PAYLOAD_BYTES];
    int pk_len = 0, sk_len = 0, payload_len = 0;

    if (sk_import_path) {
        /* Import existing key pair from files */
        sk_len = read_bin(sk_import_path, sk, stego_sk_bytes());
        if (sk_len <= 0) {
            fprintf(stderr, "failed reading SK from %s\n", sk_import_path);
            free(pk); free(sk);
            return 1;
        }
        /* If a PK file already exists at pk_path, read it; otherwise derive later */
        pk_len = read_bin(pk_path, pk, stego_pk_bytes());
        if (pk_len <= 0) {
            fprintf(stderr, "failed reading PK from %s (needed when importing SK)\n", pk_path);
            free(pk); free(sk);
            return 1;
        }
        fprintf(stderr, "Imported existing key pair (SK=%d bytes, PK=%d bytes)\n", sk_len, pk_len);
    } else {
        int rc = stego_keygen(pk, &pk_len, sk, &sk_len);
        if (rc != STEGO_OK) {
            fprintf(stderr, "stego_keygen failed\n");
            free(pk); free(sk);
            return 1;
        }
    }

    int rc = stego_sign(payload, &payload_len, phash, phash_len, sk, sk_len,
                    NULL, 0, 0, 0, NULL, 0);
    if (rc != STEGO_OK) {
        fprintf(stderr, "stego_sign failed\n");
        free(pk); free(sk);
        return 1;
    }

    if (write_bin(payload_path, payload, payload_len) != 0 ||
        write_bin(pk_path, pk, pk_len) != 0 ||
        write_bin(phash_path, phash, phash_len) != 0) {
        fprintf(stderr, "failed writing output files\n");
        free(pk); free(sk);
        return 1;
    }

    char payload_hex[STEGO_MAX_PAYLOAD_BYTES * 2 + 1];
    bytes_to_hex(payload, payload_len, payload_hex);
    printf("scheme=%s\n", stego_scheme_name());
    printf("phash_bytes=%d\n", phash_len);
    printf("payload_bytes=%d\n", payload_len);
    printf("payload_hex=%s\n", payload_hex);
    printf("pk_bytes=%d\n", pk_len);

    free(pk);
    free(sk);
    stego_cleanup();
    return 0;
}

static int cmd_keygen(const char *pk_path, const char *sk_path) {
    if (stego_init() != STEGO_OK) {
        fprintf(stderr, "stego_init failed\n");
        return 1;
    }
    uint8_t *pk = malloc(stego_pk_bytes());
    uint8_t *sk = malloc(stego_sk_bytes());
    int pk_len = 0, sk_len = 0;
    int rc = stego_keygen(pk, &pk_len, sk, &sk_len);
    if (rc != STEGO_OK) {
        fprintf(stderr, "stego_keygen failed\n");
        free(pk); free(sk);
        return 1;
    }
    if (write_bin(pk_path, pk, pk_len) != 0 ||
        write_bin(sk_path, sk, sk_len) != 0) {
        fprintf(stderr, "failed writing key files\n");
        free(pk); free(sk);
        return 1;
    }
    printf("scheme=%s\n", stego_scheme_name());
    printf("pk_bytes=%d\n", pk_len);
    printf("sk_bytes=%d\n", sk_len);
    free(pk);
    free(sk);
    stego_cleanup();
    return 0;
}

static int cmd_verify(const char *payload_path, const char *pk_path, const char *phash_path) {
    uint8_t payload[STEGO_MAX_PAYLOAD_BYTES];
    uint8_t *pk;
    uint8_t phash[STEGO_MAX_PHASH_BYTES];
    uint8_t recovered[STEGO_MAX_PHASH_BYTES];
    if (stego_init() != STEGO_OK) {
        fprintf(stderr, "stego_init failed\n");
        return 1;
    }
    pk = malloc(stego_pk_bytes());
    int payload_len = read_bin(payload_path, payload, sizeof(payload));
    int pk_len = read_bin(pk_path, pk, stego_pk_bytes());
    int phash_len = read_bin(phash_path, phash, sizeof(phash));
    int recovered_len = 0;
    if (payload_len <= 0 || pk_len <= 0 || phash_len <= 0) {
        fprintf(stderr, "failed reading input files\n");
        return 1;
    }
    int rc = stego_verify(payload, payload_len, phash_len, recovered, &recovered_len,
                          pk, pk_len, phash, phash_len);
    char payload_hex[STEGO_MAX_PAYLOAD_BYTES * 2 + 1];
    bytes_to_hex(payload, payload_len, payload_hex);
    printf("payload_hex=%s\n", payload_hex);
    printf("verify_rc=%d\n", rc);
    printf("verify_status=%s\n", rc == STEGO_OK ? "VALID" : "INVALID");
    if (rc == STEGO_OK) {
        char recovered_hex[STEGO_MAX_PHASH_BYTES * 2 + 1];
        bytes_to_hex(recovered, recovered_len, recovered_hex);
        printf("recovered_phash_hex=%s\n", recovered_hex);
    }
    free(pk);
    stego_cleanup();
    return rc == STEGO_OK ? 0 : 2;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr,
            "Usage:\n"
            "  %s generate <phash_hex> <payload.bin> <pk.bin> <phash.bin> [--sk <sk.bin>]\n"
            "  %s verify <payload.bin> <pk.bin> <phash.bin>\n"
            "  %s keygen <pk.bin> <sk.bin>\n",
            argv[0], argv[0], argv[0]);
        return 1;
    }
    if (strcmp(argv[1], "generate") == 0) {
        if (argc < 6) return 1;
        const char *sk_import = NULL;
        if (argc >= 8 && strcmp(argv[6], "--sk") == 0) {
            sk_import = argv[7];
        }
        return cmd_generate(argv[2], argv[3], argv[4], argv[5], sk_import);
    }
    if (strcmp(argv[1], "verify") == 0) {
        if (argc != 5) return 1;
        return cmd_verify(argv[2], argv[3], argv[4]);
    }
    if (strcmp(argv[1], "keygen") == 0) {
        if (argc != 4) return 1;
        return cmd_keygen(argv[2], argv[3]);
    }
    fprintf(stderr, "Unknown command\n");
    return 1;
}
