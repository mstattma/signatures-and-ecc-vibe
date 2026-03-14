# Unified Stego Signature API

A scheme-agnostic signature API for bandwidth-constrained steganographic channels. A single demo program works identically with all four supported signature schemes, selected at compile time.

## Supported Schemes

| Scheme | `SCHEME=` | Signature | Salt | PK | Security | Quantum-safe | Message recovery |
|---|---|---|---|---|---|---|---|
| UOV-80 | `uov-80` | 400 bits | 16 bits (in digest) | 204,000 bits (25.5 KB) | 80 bits | Yes | Yes |
| UOV-100 | `uov-100` | 504 bits | 16 bits (in digest) | 403,200 bits (50.4 KB) | 100 bits | Yes | Yes |
| BLS BN-P158 | `bls-bn158` | 168 bits | 16 bits (in payload) | 328 bits (41 B) | ~78 bits | No | No |
| BLS12-381 | `bls12-381` | 392 bits | 16 bits (in payload) | 776 bits (97 B) | ~117-120 bits | No | No |

## Payload Format

The stego channel payload depends on the scheme and the `embed_phash` option:

**UOV** (message recovery): the pHash is recovered from the signature via the public map `P(w)`. The pHash is NOT transmitted. The `embed_phash` parameter is ignored.
```
Payload = [signature] [|| PK]
```

**BLS with `embed_phash=1`** (default): the pHash is included in the payload alongside the salt and signature.
```
Payload = [pHash || salt || signature] [|| PK]
```

**BLS with `embed_phash=0`**: the pHash is omitted from the payload. The verifier must provide it externally (e.g., from a ledger lookup). The salt is still included so the verifier can reconstruct the signed message `pHash || salt`.
```
Payload = [salt || signature] [|| PK]
```

The unified API abstracts these differences: the caller always provides a pHash to `stego_sign()`. On verification, `stego_verify()` auto-detects whether the pHash is embedded by comparing the payload length against the expected signature-only size.

## Payload Sizes (bits)

### With pHash embedded (no PK)

| pHash | UOV-80 | UOV-100 | BLS-BN158 | BLS12-381 |
|---|---|---|---|---|
| 96-bit | **400** | **504** | **280** | **504** |
| 144-bit | **400** | **504** | **328** | **552** |
| 184-bit | **N/A** [^trunc] | **504** | **368** | **592** |

### Without pHash embedded (no PK)

When the pHash is omitted, the payload is constant-size (salt + signature only). The pHash must be provided at verification time (e.g., from a ledger).

| pHash | UOV-80 | UOV-100 | BLS-BN158 | BLS12-381 |
|---|---|---|---|---|
| any | **400** (always, msg recovery) | **504** (always, msg recovery) | **184** | **408** |

[^trunc]: **UOV-80 cannot sign a 184-bit pHash.** UOV-80 recovers at most 144 bits of hash (18 bytes = `_HASH_EFFECTIVE_BYTE` with 2-byte salt). A 184-bit pHash would be truncated, making the full pHash unrecoverable by the verifier. The implementation rejects pHashes exceeding `stego_max_phash_bytes()` with an error. UOV-100 supports up to 184 bits (exact fit). For larger pHashes, use UOV-100 or BLS.

### Maximum pHash length per scheme

| Scheme | Max pHash | Reason |
|---|---|---|
| UOV-80 | **144 bits** (18 bytes) | Recoverable digest = 20 bytes - 2 byte salt = 18 bytes |
| UOV-100 | **184 bits** (23 bytes) | Recoverable digest = 25 bytes - 2 byte salt = 23 bytes |
| BLS-BN158 | Unlimited | pHash transmitted verbatim (or provided at verification) |
| BLS12-381 | Unlimited | pHash transmitted verbatim (or provided at verification) |

### With PK appended (pHash embedded)

| pHash | UOV-80 | UOV-100 | BLS-BN158 | BLS12-381 |
|---|---|---|---|---|
| 96-bit | 204,400 | 403,704 | **608** | **1,280** |
| 144-bit | 204,400 | 403,704 | **656** | **1,328** |
| 184-bit | N/A [^trunc] | 403,704 | **696** | **1,368** |

### With PK appended (pHash NOT embedded)

| pHash | UOV-80 | UOV-100 | BLS-BN158 | BLS12-381 |
|---|---|---|---|---|
| any | 204,400 | 403,704 | **512** | **1,184** |

### Component breakdown

| Component | UOV-80 | UOV-100 | BLS-BN158 | BLS12-381 |
|---|---|---|---|---|
| pHash in payload | 0 (recovered) | 0 (recovered) | 0-184 bits (optional) | 0-184 bits (optional) |
| Salt in payload | 0 (in digest) | 0 (in digest) | 16 bits | 16 bits |
| Signature | 400 bits | 504 bits | 168 bits | 392 bits |
| Public key | 204,000 bits | 403,200 bits | 328 bits | 776 bits |

### What fits in a ~500-bit stego channel?

| Configuration | Total bits | Fits in 500 bits? |
|---|---|---|
| BLS-BN158, no pHash, no PK | **184** | Yes (316 bits spare for ECC) |
| BLS-BN158 + 96-bit pHash, no PK | **280** | Yes (220 bits spare) |
| BLS-BN158 + 144-bit pHash, no PK | **328** | Yes (172 bits spare) |
| BLS-BN158 + 184-bit pHash, no PK | **368** | Yes (132 bits spare) |
| UOV-80, pHash <= 144 bits, no PK | **400** | Yes (100 bits spare) |
| BLS12-381, no pHash, no PK | **408** | Yes (92 bits spare) |
| BLS12-381 + 96-bit pHash, no PK | **504** | Barely |
| UOV-100, pHash <= 184 bits, no PK | **504** | Barely |
| BLS-BN158, no pHash + PK | **512** | No (needs ~600 bits) |
| BLS12-381 + 144-bit pHash, no PK | **552** | No (needs ~600 bits) |

### Trade-off summary

| Property | UOV-80 | UOV-100 | BLS-BN158 | BLS12-381 |
|---|---|---|---|---|
| Smallest payload (no PK) | 400 bits | 504 bits | **184 bits** (no pHash) | 408 bits (no pHash) |
| Smallest with pHash | 400 bits | 504 bits | **280 bits** (96b pHash) | 504 bits (96b pHash) |
| Max pHash | **144 bits** | **184 bits** | Unlimited | Unlimited |
| PK in-band feasible? | No (25.5 KB) | No (50.4 KB) | **Yes** (41 B) | Marginal (97 B) |
| pHash in payload? | Never (recovered) | Never (recovered) | Optional | Optional |
| Post-quantum? | **Yes** | **Yes** | No | No |
| Quantum attack? | Safe | Safe | Broken (Shor) | Broken (Shor) |
| pHash visible to eavesdropper? | No (only with PK) | No (only with PK) | Only if embedded | Only if embedded |
| Deterministic salt? | Yes (user-provided) | Yes (user-provided) | Yes (user-provided) | Yes (user-provided) |

## API Reference

### `stego_sig.h`

```c
/* Scheme information */
const char *stego_scheme_name(void);      // e.g. "UOV (OV(256,50,20)-classic)"
int stego_security_bits(void);            // classical security level
int stego_has_message_recovery(void);     // 1 for UOV, 0 for BLS
int stego_is_post_quantum(void);          // 1 for UOV, 0 for BLS

/* Size queries */
int stego_sig_bytes(void);                // signature size (compressed)
int stego_pk_bytes(void);                 // public key size (compressed)
int stego_sk_bytes(void);                 // secret key size
int stego_max_phash_bytes(void);          // max pHash before truncation
int stego_payload_bytes(int phash_len, int embed_phash, int append_pk);

/* Lifecycle */
int stego_init(void);
void stego_cleanup(void);

/* Key generation */
int stego_keygen(uint8_t *pk, int *pk_len, uint8_t *sk, int *sk_len);

/* Sign */
int stego_sign(uint8_t *payload, int *payload_len,
               const uint8_t *phash, int phash_len,
               const uint8_t *sk, int sk_len,
               const uint8_t *salt, int salt_len,   // NULL for random
               int embed_phash,                      // BLS: 1=include pHash, 0=omit
               int append_pk,
               const uint8_t *pk, int pk_len);

/* Verify */
int stego_verify(const uint8_t *payload, int payload_len,
                 int phash_len,
                 uint8_t *recovered_phash, int *recovered_len,
                 const uint8_t *ext_pk, int ext_pk_len,
                 const uint8_t *expected_phash, int expected_phash_len);
```

### Return codes

| Code | Value | Meaning |
|---|---|---|
| `STEGO_OK` | 0 | Success / signature valid |
| `STEGO_ERR` | -1 | General error |
| `STEGO_ERR_VERIFY` | -2 | Signature verification failed |
| `STEGO_ERR_NO_PHASH` | -3 | pHash needed but not available (BLS without embed, no expected_phash) |

### Sign flow

```c
stego_init();

uint8_t *pk = malloc(stego_pk_bytes());
uint8_t *sk = malloc(stego_sk_bytes());
int pk_len, sk_len;
stego_keygen(pk, &pk_len, sk, &sk_len);

uint8_t payload[512];
int payload_len;

// Option A: embed pHash in payload (BLS default)
stego_sign(payload, &payload_len,
           phash, phash_len, sk, sk_len,
           NULL, 0,    // random salt
           1,          // embed pHash in payload
           0, NULL, 0);

// Option B: omit pHash from payload (BLS, pHash from ledger)
stego_sign(payload, &payload_len,
           phash, phash_len, sk, sk_len,
           NULL, 0,
           0,          // do NOT embed pHash
           0, NULL, 0);
// payload is now salt || sig only -- smaller!
```

### Verify flow

```c
// Case 1: pHash embedded in payload (or UOV message recovery)
uint8_t recovered_phash[32];
int recovered_len;
int rc = stego_verify(payload, payload_len, phash_len,
                      recovered_phash, &recovered_len,
                      pk, pk_len,       // external PK (or NULL for embedded)
                      NULL, 0);         // no expected_phash needed

// Case 2: pHash NOT embedded (BLS without embed_phash)
// Verifier must provide the expected pHash (e.g., from ledger lookup)
rc = stego_verify(payload, payload_len, phash_len,
                  recovered_phash, &recovered_len,
                  pk, pk_len,
                  expected_phash, expected_phash_len);

// Case 3: pHash NOT embedded AND no expected_phash provided
rc = stego_verify(payload, payload_len, phash_len,
                  recovered_phash, &recovered_len,
                  pk, pk_len,
                  NULL, 0);
// rc == STEGO_ERR_NO_PHASH -- cannot verify without pHash
```

### Verification semantics

- **BLS with embedded pHash**: `stego_verify()` extracts the pHash from the payload, reconstructs `pHash || salt`, and verifies the BLS signature. Returns `STEGO_OK` if valid.

- **BLS without embedded pHash**: `stego_verify()` detects the missing pHash by checking `payload_len < phash_len + salt + sig`. If `expected_phash` is provided, it reconstructs the signed message and verifies. If not provided, returns `STEGO_ERR_NO_PHASH`.

- **UOV**: `stego_verify()` always returns `STEGO_OK` (the public map P(w) can always be evaluated). The recovered bytes are `H(pHash)[truncated]` -- the hash portion of the salt-in-digest. The `embed_phash` and `expected_phash` parameters are ignored. The caller must compare the recovered hash against their own `H(pHash)` to determine authenticity. A tampered signature will produce a different recovered hash.

### Auto-detection of embedded pHash

For BLS, `stego_verify()` determines whether the pHash is embedded by examining the payload length:
- `payload_len >= phash_len + salt + sig_bytes` → pHash is embedded
- `payload_len >= salt + sig_bytes` but `< phash_len + salt + sig_bytes` → no pHash
- `payload_len < salt + sig_bytes` → error (payload too short)

This works because `phash_len > 0` is always true, so the two cases are distinguishable.

## Building

### Prerequisites

- C compiler (gcc or clang)
- For UOV: OpenSSL (`libssl-dev`)
- For BLS: CMake, GMP (`cmake`, `libgmp-dev`)

```bash
# Debian/Ubuntu
sudo apt install build-essential cmake libgmp-dev libssl-dev
```

### Build and run

```bash
cd unified-api

# UOV (post-quantum)
make test SCHEME=uov-80
make test SCHEME=uov-100

# BLS (classical)
make test SCHEME=bls-bn158
make test SCHEME=bls12-381
```

### Make targets

| Target | Description |
|---|---|
| `make stego_demo SCHEME=...` | Build the unified demo |
| `make test SCHEME=...` | Build and run |
| `make clean` | Remove build artifacts |

## Project Structure

```
unified-api/
├── README.md            # This file
├── Makefile             # Build system (SCHEME= selects backend)
├── stego_sig.h          # Scheme-agnostic API
├── stego_sig_uov.c      # UOV backend (links against ../UOV/pqov/)
├── stego_sig_bls.c      # BLS backend (links against ../BLS/relic)
└── stego_demo.c         # Unified demo (works with all schemes)
```

## Design Notes

### Compile-time scheme selection

The signature scheme is selected at compile time via `SCHEME=`. This is a hard constraint because:
- UOV parameters (`_OV256_50_20` vs `_OV256_63_25`) are compile-time `#define`s that control struct sizes
- RELIC's curve (`FP_PRIME=158` vs `FP_PRIME=381`) is set at CMake configure time

A single binary cannot support multiple schemes simultaneously. However, the API is identical for all schemes, so switching is just a recompile.

### Message recovery abstraction

The most significant difference between UOV and BLS is message recovery:
- **UOV**: The pHash is not in the payload. The verifier recovers `H(pHash)[truncated]` from the signature by evaluating the public multivariate map `P(w)`.
- **BLS**: The pHash is optionally included in the payload. If omitted, the verifier must provide it externally.

The API hides this: `stego_sign()` always takes a pHash and produces a payload; `stego_verify()` always returns a recovered/extracted pHash (or uses the expected one). The caller doesn't need to know how recovery works.

### Salt handling

- **UOV**: Salt is embedded in the recovered digest (salt-in-digest technique). Configurable via the `salt`/`salt_len` parameters. NULL = random salt. User-provided salt enables deterministic signatures. The salt costs zero bandwidth (it's inside the fixed-size digest).
- **BLS**: Salt (2 bytes = 16 bits) is always included in the payload and is part of the signed message (`pHash || salt`). Configurable via `salt`/`salt_len` parameters. NULL = random salt. User-provided salt enables deterministic signatures. The salt ensures that signing the same pHash for different images produces different signatures.

### Optional pHash embedding (BLS)

For BLS, the `embed_phash` parameter controls whether the pHash is included in the payload:
- `embed_phash=1` (default): Payload = `[pHash || salt || signature]`. Self-contained — the verifier has everything needed.
- `embed_phash=0`: Payload = `[salt || signature]`. Smaller payload, but the verifier must obtain the pHash from another source (e.g., a ledger lookup by signature prefix).

This is useful when a ledger stores the pHash alongside the signature record. The verifier extracts the signature from the image, looks it up on the ledger, retrieves the pHash, and verifies.
