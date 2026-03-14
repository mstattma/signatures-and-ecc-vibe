# Unified Stego Signature API

A scheme-agnostic signature API for bandwidth-constrained steganographic channels. A single demo program works identically with all four supported signature schemes, selected at compile time.

## Supported Schemes

| Scheme | `SCHEME=` | Signature | PK | Security | Quantum-safe | Message recovery |
|---|---|---|---|---|---|---|
| UOV-80 | `uov-80` | 400 bits | 204,000 bits (25.5 KB) | 80 bits | Yes | Yes |
| UOV-100 | `uov-100` | 504 bits | 403,200 bits (50.4 KB) | 100 bits | Yes | Yes |
| BLS BN-P158 | `bls-bn158` | 168 bits | 328 bits (41 B) | ~78 bits | No | No |
| BLS12-381 | `bls12-381` | 392 bits | 776 bits (97 B) | ~117-120 bits | No | No |

## Payload Format

The stego channel payload depends on the scheme:

**UOV** (message recovery): the pHash is recovered from the signature via the public map `P(w)`. The pHash is NOT transmitted.
```
Payload = [signature] [|| PK]
```

**BLS** (no message recovery): the pHash must be transmitted explicitly alongside the signature.
```
Payload = [pHash || signature] [|| PK]
```

The unified API abstracts this difference: the caller always provides a pHash to `stego_sign()` and always receives a recovered/extracted pHash from `stego_verify()`.

## Payload Sizes (bits)

### Without PK (PK exchanged out-of-band)

| pHash | UOV-80 | UOV-100 | BLS-BN158 | BLS12-381 |
|---|---|---|---|---|
| 96-bit | **400** | **504** | **264** | **488** |
| 144-bit | **400** | **504** | **312** | **536** |
| 184-bit | **400** | **504** | **352** | **576** |

UOV payloads are constant regardless of pHash size (pHash is recovered from the signature, not transmitted). BLS payloads grow with pHash size.

### With PK appended (PK in-band)

| pHash | UOV-80 | UOV-100 | BLS-BN158 | BLS12-381 |
|---|---|---|---|---|
| 96-bit | 204,400 | 403,704 | **592** | **1,264** |
| 144-bit | 204,400 | 403,704 | **640** | **1,312** |
| 184-bit | 204,400 | 403,704 | **680** | **1,352** |

UOV public keys are 25.5-50.4 KB -- in-band PK is impractical. BLS public keys are 41-97 bytes, making in-band PK feasible.

### Component breakdown

| Component | UOV-80 | UOV-100 | BLS-BN158 | BLS12-381 |
|---|---|---|---|---|
| pHash in payload | 0 (recovered) | 0 (recovered) | 96-184 bits | 96-184 bits |
| Signature | 400 bits | 504 bits | 168 bits | 392 bits |
| Public key | 204,000 bits | 403,200 bits | 328 bits | 776 bits |

### What fits in a ~500-bit stego channel?

| Configuration | Total bits | Fits in 500 bits? |
|---|---|---|
| BLS-BN158 + 96-bit pHash, no PK | **264** | Yes (236 bits spare for ECC) |
| BLS-BN158 + 144-bit pHash, no PK | **312** | Yes (188 bits spare) |
| BLS-BN158 + 184-bit pHash, no PK | **352** | Yes (148 bits spare) |
| UOV-80, any pHash, no PK | **400** | Yes (100 bits spare) |
| BLS12-381 + 96-bit pHash, no PK | **488** | Tight (12 bits spare) |
| UOV-100, any pHash, no PK | **504** | Barely (needs efficient ECC) |
| BLS12-381 + 144-bit pHash, no PK | **536** | No (needs ~1000-bit channel) |
| BLS-BN158 + 96-bit pHash + PK | **592** | No (needs ~600-bit channel) |
| BLS-BN158 + 144-bit pHash + PK | **640** | No (needs ~700-bit channel) |
| BLS-BN158 + 184-bit pHash + PK | **680** | No (needs ~700-bit channel) |
| BLS12-381 + 96-bit pHash + PK | **1,264** | No (needs ~1300-bit channel) |

### Trade-off summary

| Property | UOV-80 | UOV-100 | BLS-BN158 | BLS12-381 |
|---|---|---|---|---|
| Smallest payload (no PK) | 400 bits | 504 bits | **264 bits** (96b pHash) | 488 bits (96b pHash) |
| PK in-band feasible? | No (25.5 KB) | No (50.4 KB) | **Yes** (41 B) | Marginal (97 B) |
| pHash transmitted? | No (recovered) | No (recovered) | Yes | Yes |
| Post-quantum? | **Yes** | **Yes** | No | No |
| Quantum attack? | Safe | Safe | Broken (Shor) | Broken (Shor) |
| pHash visible to eavesdropper? | No (only with PK) | No (only with PK) | Yes (in payload) | Yes (in payload) |
| Deterministic salt? | Yes (user-provided) | Yes (user-provided) | N/A (inherently deterministic) | N/A |

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
int stego_payload_bytes(int phash_len, int append_pk);  // total payload size

/* Lifecycle */
int stego_init(void);                     // initialize backend
void stego_cleanup(void);                 // clean up

/* Key generation */
int stego_keygen(uint8_t *pk, int *pk_len, uint8_t *sk, int *sk_len);

/* Sign: pHash + sk + optional salt + optional PK append → payload */
int stego_sign(uint8_t *payload, int *payload_len,
               const uint8_t *phash, int phash_len,
               const uint8_t *sk, int sk_len,
               const uint8_t *salt, int salt_len,   // UOV only; NULL for random
               int append_pk,
               const uint8_t *pk, int pk_len);

/* Verify: payload + phash_len + optional external PK → recovered pHash */
int stego_verify(const uint8_t *payload, int payload_len,
                 int phash_len,
                 uint8_t *recovered_phash, int *recovered_len,
                 const uint8_t *ext_pk, int ext_pk_len);  // NULL to use embedded PK
```

### Sign flow

```c
stego_init();

uint8_t *pk = malloc(stego_pk_bytes());
uint8_t *sk = malloc(stego_sk_bytes());
int pk_len, sk_len;
stego_keygen(pk, &pk_len, sk, &sk_len);

uint8_t payload[512];
int payload_len;
stego_sign(payload, &payload_len,
           phash, phash_len,       // perceptual hash to authenticate
           sk, sk_len,
           NULL, 0,                // random salt (UOV) / ignored (BLS)
           0,                      // don't append PK
           NULL, 0);

// payload is now ready for stego embedding
// payload_len * 8 = total bits to transmit
```

### Verify flow

```c
// Receiver has: payload (from stego extraction), pk (out-of-band or embedded)
uint8_t recovered_phash[32];
int recovered_len;
int rc = stego_verify(payload, payload_len,
                      phash_len,             // known pHash length
                      recovered_phash, &recovered_len,
                      pk, pk_len);           // external PK (or NULL for embedded)

if (rc == STEGO_OK) {
    // For BLS: recovered_phash contains the raw pHash from the payload
    // For UOV: recovered_phash contains H(pHash)[truncated] (the hash portion
    //          of the recovered digest, not the raw pHash)
    // In both cases: compare against your own expected value
}
```

### Verification semantics

- **BLS**: `stego_verify()` returns `STEGO_OK` if the BLS signature is mathematically valid, `STEGO_ERR_VERIFY` if invalid. The recovered pHash is the raw bytes extracted from the payload.

- **UOV**: `stego_verify()` always returns `STEGO_OK` (the public map P(w) can always be evaluated). The recovered bytes are `H(pHash)[truncated]` -- the hash portion of the salt-in-digest. The caller must compare this against their own `H(pHash)` to determine authenticity. A tampered signature will produce a different recovered hash.

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
cd unified

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
unified/
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
- **BLS**: The pHash is prepended to the payload in cleartext. The verifier extracts it and checks the BLS signature.

The API hides this: `stego_sign()` always takes a pHash and produces a payload; `stego_verify()` always returns a recovered/extracted pHash. The caller doesn't need to know how recovery works.

### Salt handling

- **UOV**: Salt is embedded in the recovered digest (salt-in-digest technique). Configurable via the `salt`/`salt_len` parameters. NULL = random salt. User-provided salt enables deterministic signatures.
- **BLS**: Salt parameters are ignored. BLS signatures are inherently deterministic (same message + same key = same signature). To get distinct signatures for the same pHash, prepend a nonce to the pHash before signing.
