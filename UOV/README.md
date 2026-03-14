# UOV Fuzzy Signatures for Steganographic Image Authentication

This project implements compact post-quantum digital signatures based on [UOV (Oil and Vinegar)](https://www.pqov.org/) with **salt-in-digest message recovery**, designed for authenticating images through a steganographic channel embedded in the image itself.

The core idea: the sender computes a **perceptual hash** of the cover image and signs it with UOV. The signature is steganographically embedded into the image. The receiver extracts the signature, recovers the sender's perceptual hash via the UOV public map `P(w)`, and compares it against their own perceptual hash of the received image. Because perceptual hashes are designed to be robust against minor image modifications, this produces an **authenticity score** rather than a binary valid/invalid result -- a form of **fuzzy signatures**.

## Overview

```
Sender                                                    Receiver
                                                          
  image ──► pHash(image) ──► sign(sk, phash)              has: pk (out-of-band)
                              │                                 │
                              ▼                                 ▼
                         ┌──────────┐                     stego_extract(image')
                         │signature │                           │
                         │  w only  │                           ▼
                         │(400 bits)│                     P(w) = phash_sender[trunc] || salt
                         └────┬─────┘                           │
                              │                                 ▼
                              ▼                           pHash(image') = phash_receiver
                         stego_embed(image, w)                  │
                              │                                 ▼
                              ▼                           similarity(phash_sender, phash_receiver)
                         image' (with embedded sig)             │
                              │                                 ▼
                              └────── channel ──────────► authenticity score
```

**What goes through the stego channel:** Only the signature vector `w` (400 or 504 bits), embedded in the image.

**What the receiver recovers:** The sender's perceptual hash, extracted from `P(w)`. This is compared against the receiver's own perceptual hash for a similarity score.

**What goes out-of-band (one-time setup):** The public key (~4-50 KB).

## Fuzzy Signatures and Authenticity Scoring

Traditional digital signatures are binary: valid or invalid. If even a single bit of the message changes, verification fails. This is problematic for image authentication through steganographic channels because:

1. **The stego channel modifies the image**: embedding a signature into an image changes its pixel values slightly. The image the receiver sees is not bit-identical to the original.
2. **Transmission may further alter the image**: re-compression (JPEG), resizing, color space conversion, and other processing can occur between sender and receiver.

**Perceptual hashing** solves this by producing hash values that are similar (or identical) for visually similar images. By signing a perceptual hash instead of a cryptographic hash of the raw image bytes, we enable a graduated authenticity assessment:

### How it works

1. **Sender**: Computes `phash_sender = pHash(original_image)`, signs it with `ov_sign_digest()`, and embeds the signature `w` into the image using steganography.

2. **Receiver**: Extracts `w` from the received image, evaluates `P(w)` to recover `phash_sender` (the sender's perceptual hash), computes `phash_receiver = pHash(received_image)`, and calculates a similarity score.

3. **Authenticity score**: The score depends on the perceptual hash function used:
   - **Hamming distance** (for pHash, dHash, aHash): count differing bits. Lower = more authentic.
   - **Normalized similarity**: `1 - hamming_distance / total_bits`. Higher = more authentic.
   - **Threshold**: define a threshold (e.g., >90% similar = authentic).

### What this enables

| Scenario | Cryptographic hash | Perceptual hash (ours) |
|----------|-------------------|----------------------|
| Image re-compressed (JPEG quality change) | Signature INVALID | Score: ~95% (authentic) |
| Image slightly cropped or resized | Signature INVALID | Score: ~85% (likely authentic) |
| Image watermarked or annotated | Signature INVALID | Score: ~70% (modified but recognizable) |
| Completely different image | Signature INVALID | Score: ~50% (not authentic) |
| Malicious tampering (face swap, object removal) | Signature INVALID | Score: varies (detectable if significant) |

### Perceptual hash considerations

The choice of perceptual hash function determines the properties of the fuzzy signature. The hash must fit within `_HASH_EFFECTIVE_BYTE` bytes of the recovered digest:

| Perceptual hash | Typical output size | Fits in PARAM=80 (18B) | Fits in PARAM=100 (23B) | Properties |
|----------------|--------------------|-----------------------|------------------------|------------|
| pHash (DCT-based) | 8 bytes | Yes | Yes | Robust to scaling, compression |
| dHash (difference) | 8 bytes | Yes | Yes | Fast, good for near-duplicates |
| aHash (average) | 8 bytes | Yes | Yes | Simplest, least robust |
| pHash+ (extended) | 16-32 bytes | Yes (16B) / truncated | Yes | More discriminative |
| BlockHash | 16-32 bytes | Yes (16B) / truncated | Yes | Good for partial modifications |
| [DinoHash](https://github.com/proteus-photos/dinohash-perceptual-hash) | 12 bytes (96 bits) | Yes | Yes | DINOv2-based neural hash; SOTA robustness to filters, compression, crops, adversarial attacks; 12% better bit accuracy than prior art ([paper](https://arxiv.org/abs/2503.11195)) |
| NNPH (neural, other) | varies | depends | depends | Robust, requires ML model |

For hash outputs shorter than `_HASH_EFFECTIVE_BYTE`, the remaining bytes can be used for additional metadata or set to zero. For hash outputs longer than `_HASH_EFFECTIVE_BYTE`, they must be truncated (reducing discriminative power but preserving the fuzzy comparison property).

## Signature Structure and Salt-in-Digest

A UOV signature consists of a single vector `w`:

```
signature = w                 (_PUB_N_BYTE bytes, the ONLY transmitted data)

recovered digest = P(w)       (_PUB_M_BYTE bytes, recovered by the verifier)
                 = phash[0 .. _HASH_EFFECTIVE_BYTE-1] || salt[0 .. _SALT_BYTE-1]
                   ▲                                      ▲
                   perceptual hash, truncated              random nonce, embedded
                   (compared for similarity)               (NOT transmitted)
```

**How it works:**

1. **Signing**: The signer provides the perceptual hash via `ov_sign_digest()`. The first `_HASH_EFFECTIVE_BYTE` bytes are used as the hash portion of the target vector, and `_SALT_BYTE` bytes of salt are appended, forming `y = phash[truncated] || salt`. The signer finds `w` such that `P(w) = y`. The signature is just `w`.

2. **Verification**: The verifier evaluates `P(w)` to recover the full digest. The first `_HASH_EFFECTIVE_BYTE` bytes are the sender's perceptual hash. The last `_SALT_BYTE` bytes are the recovered salt (ignored for verification). The verifier compares the recovered perceptual hash against their own computation for a similarity score.

3. **Strict verification**: For cases where an exact match is needed (same perceptual hash), `ov_verify_digest()` performs a constant-time byte comparison of the hash portion.

### Salt-in-digest

The salt occupies bits inside the fixed-size digest `P(w)`, not extra bytes in the signature. This provides multi-target attack resistance at zero transmission cost, at the expense of reducing the effective hash length by `_SALT_BYTE` bytes.

### Digest layout for each parameter set

With the default 2-byte salt:

| Parameter | Digest (P(w)) | Hash portion | Salt portion | Collision resistance |
|-----------|--------------|-------------|-------------|---------------------|
| `PARAM=80` | 20 bytes = 160 bits | 18 bytes = 144 bits | 2 bytes = 16 bits | 72 bits |
| `PARAM=100` | 25 bytes = 200 bits | 23 bytes = 184 bits | 2 bytes = 16 bits | 92 bits |

### Signature sizes at different salt configurations

The signature is always `w` only (`_PUB_N_BYTE` bytes). The salt does NOT affect signature size -- it trades off collision resistance within the fixed digest.

| Salt | PARAM=80 sig | PARAM=80 collision bits | PARAM=100 sig | PARAM=100 collision bits |
|------|-------------|------------------------|--------------|------------------------|
| 0 B | 50 B = **400 bits** | 80 | 63 B = **504 bits** | 100 |
| 2 B (default) | 50 B = **400 bits** | 72 | 63 B = **504 bits** | 92 |
| 4 B | 50 B = **400 bits** | 64 | 63 B = **504 bits** | 84 |

## API

### Signing

```c
// Sign a message (internal SHAKE256 hash, random salt):
ov_sign(signature, sk, message, mlen);

// Sign a message with user-provided salt (any length, hashed to _SALT_BYTE):
ov_sign_salt(signature, sk, message, mlen, my_salt, my_salt_len);

// Sign an externally-provided digest (e.g., perceptual hash), optional salt:
ov_sign_digest(signature, sk, phash, phash_len, salt, salt_len);
```

### Verification

```c
// Verify signature against a message (internal SHAKE256 hash):
int rc = ov_verify(message, mlen, signature, pk);

// Verify signature against an externally-provided digest:
int rc = ov_verify_digest(phash, phash_len, signature, pk);
```

### Recovering the perceptual hash (for similarity comparison)

```c
// Recover the full digest from the signature using the public map:
uint8_t recovered[_PUB_M_BYTE];
ov_publicmap(recovered, pk->pk, signature);

// The first _HASH_EFFECTIVE_BYTE bytes are the sender's perceptual hash:
uint8_t *sender_phash = recovered;  // _HASH_EFFECTIVE_BYTE bytes

// Compute your own perceptual hash of the received image:
uint8_t my_phash[_HASH_EFFECTIVE_BYTE];
// ... compute perceptual hash ...

// Calculate similarity (e.g., Hamming distance for pHash):
int distance = 0;
for (int i = 0; i < _HASH_EFFECTIVE_BYTE; i++) {
    distance += __builtin_popcount(sender_phash[i] ^ my_phash[i]);
}
float similarity = 1.0f - (float)distance / (_HASH_EFFECTIVE_BYTE * 8);
// similarity ≈ 1.0 means authentic, ≈ 0.5 means unrelated
```

### Digest handling

When using `ov_sign_digest()` and `ov_verify_digest()`:
- If `digest_len > _HASH_EFFECTIVE_BYTE`: **signing is rejected** (returns `-2`). This prevents silent truncation of the perceptual hash, which would make the full pHash unrecoverable by the verifier.
- If `digest_len == _HASH_EFFECTIVE_BYTE`: the digest bytes are used directly (verbatim, optimal for fuzzy comparison)
- If `digest_len < _HASH_EFFECTIVE_BYTE`: the digest is hashed with SHAKE256 to expand it to the required length

For fuzzy signature use, it is important to use `digest_len == _HASH_EFFECTIVE_BYTE` so that the perceptual hash bytes are embedded verbatim and can be recovered and compared for similarity. If the digest is shorter and gets expanded by SHAKE256, the similarity comparison property is lost.

**Maximum pHash lengths:**
- `PARAM=80`: max 18 bytes = 144 bits (a 184-bit pHash is rejected)
- `PARAM=100`: max 23 bytes = 184 bits (exact fit for 184-bit pHashes)

### User-provided salt

The salt can be provided by the caller (any length, hashed down to `_SALT_BYTE` bytes):

- **Deterministic signing**: same digest + same salt = same signature
- **Application-specific nonces**: timestamp, counter, image metadata
- **NULL or length 0**: random salt is generated

## Parameter Sets

All signature sizes are `_PUB_N_BYTE` bytes (just `w`), independent of salt configuration.

### Custom Parameter Sets (for stego channels)

| Parameter | `PARAM=80` | `PARAM=100` |
|-----------|-----------|------------|
| **Security** | 80 bits | 100 bits |
| **Field** | GF(256) | GF(256) |
| **v (vinegar vars)** | 30 | 38 |
| **o (oil vars)** | 20 | 25 |
| **n = v + o** | 50 | 63 |
| **Signature size** | 50 bytes = **400 bits** | 63 bytes = **504 bits** |
| **Digest from P(w)** | 20 bytes = 160 bits | 25 bytes = 200 bits |
| **Perceptual hash capacity** | 18 bytes = 144 bits | 23 bytes = 184 bits |
| **Collision resistance** | 72 bits (with default 2B salt) | 92 bits (with default 2B salt) |
| **Public key (classic)** | 25,500 bytes | 50,400 bytes |
| **Public key (compressed)** | 4,216 bytes | 8,141 bytes |
| **Params define** | `_OV256_50_20` | `_OV256_63_25` |

These parameters were selected using the included security estimator (`uov_security_estimator.py`) to minimize signature size while achieving the target security level against all known attacks (direct algebraic, Kipnis-Shamir, intersection/reconciliation, and collision forgery).

### Standard NIST Parameter Sets

| Parameter | NIST Level | `PARAM=` | Field | (n, o) | Signature | Public Key (classic) |
|-----------|-----------|----------|-------|--------|-----------|---------------------|
| uov-Is | 1 | `1` (default) | GF(16) | (160, 64) | 80 B = 640 bits | 412,160 B |
| uov-Ip | 1 | `3` | GF(256) | (112, 44) | 112 B = 896 bits | 278,432 B |
| uov-III | 3 | `4` | GF(256) | (184, 72) | 184 B = 1472 bits | 1,225,440 B |
| uov-V | 5 | `5` | GF(256) | (244, 96) | 244 B = 1952 bits | 2,869,440 B |

Note: These are smaller than upstream because the salt is now inside the digest, not appended. Use `SALT=0` to disable salt entirely.

## Building

### Prerequisites

- C compiler (gcc or clang)
- OpenSSL development library (`libssl-dev` / `openssl`)
- Python 3 (for the security estimator only)

On Debian/Ubuntu:
```bash
sudo apt install build-essential libssl-dev
```

### Build the stego demo

```bash
cd pqov

# 80-bit security (400-bit signature, default 2-byte salt-in-digest)
make clean && make stego_demo PARAM=80

# 100-bit security (504-bit signature)
make clean && make stego_demo PARAM=100

# Run it
./stego_demo
```

### Configuring the salt length

The salt length defaults to 2 bytes (embedded in the digest). Override it with `SALT=`:

```bash
# No salt -- full collision resistance, deterministic signing
make clean && make stego_demo PARAM=80 SALT=0

# 4-byte salt -- more multi-target protection, less collision resistance
make clean && make stego_demo PARAM=80 SALT=4

# Signature size is always the same (400 bits for PARAM=80) regardless of SALT
```

The `SALT=` parameter works with all targets (`stego_demo`, `test`, `sign_api-test`, etc.) and all `PARAM=` values.

### Build the standard test suite

```bash
cd pqov

# Run sign/verify tests with custom 80-bit params (500 iterations)
make clean && make test PARAM=80

# Run sign/verify tests with custom 100-bit params
make clean && make test PARAM=100

# Run with standard NIST Level 1 (GF(16), default)
make clean && make test

# Run with NIST Level 1 GF(256) variant
make clean && make test PARAM=3
```

### Build with key compression variants

By default, the `_OV_CLASSIC` variant is used (uncompressed public key). You can select other variants:

```bash
# Classic (uncompressed PK, uncompressed SK)
make stego_demo PARAM=80 VARIANT=1

# PKC (compressed PK, uncompressed SK)
make stego_demo PARAM=80 VARIANT=2

# PKC+SKC (compressed PK, compressed SK)
make stego_demo PARAM=80 VARIANT=3
```

### All make targets

| Target | Description |
|--------|-------------|
| `make all` | Build all executables |
| `make stego_demo` | Build the message recovery demo |
| `make sign_api-test` | Build the sign/verify test harness |
| `make test` | Build and run sign/verify tests (500 iterations) |
| `make sign_api-benchmark` | Build the benchmark |
| `make clean` | Remove all build artifacts |

### Build parameter reference

| `PARAM=` | Params | Security | Signature |
|----------|--------|----------|-----------|
| `80` | GF(256), n=50, o=20 | ~80 bits | 400 bits |
| `100` | GF(256), n=63, o=25 | ~100 bits | 504 bits |
| `1` (default) | GF(16), n=160, o=64 | ~128 bits (NIST L1) | 640 bits |
| `3` | GF(256), n=112, o=44 | ~176 bits (NIST L1) | 896 bits |
| `4` | GF(256), n=184, o=72 | ~256 bits (NIST L3) | 1472 bits |
| `5` | GF(256), n=244, o=96 | ~384 bits (NIST L5) | 1952 bits |

| `SALT=` | Salt in digest | Collision resistance (PARAM=80) | Notes |
|---------|---------------|-------------------------------|-------|
| `0` | 0 bytes | 80 bits | No salt, full hash, deterministic |
| (omitted) | 2 bytes = 16 bits | 72 bits | Default, multi-target protected |
| `4` | 4 bytes = 32 bits | 64 bits | Stronger multi-target protection |

| `VARIANT=` | Key format | Notes |
|------------|-----------|-------|
| `1` (default) | Classic (uncompressed PK + SK) | Fastest verification |
| `2` | PKC (compressed PK, uncompressed SK) | Smaller public key |
| `3` | PKC+SKC (compressed PK + SK) | Smallest keys, slower sign |

## How It Works: Message Recovery

UOV has an inherent message recovery property that most descriptions overlook:

1. **Signing**: The signer provides a perceptual hash (or any digest) and builds a target `y = phash[truncated] || salt`, then finds a signature vector `w` such that `P(w) = y`, where `P` is the public multivariate quadratic map.

2. **Recovery**: The verifier evaluates `P(w)` to recover `y`. The first `_HASH_EFFECTIVE_BYTE` bytes are the sender's perceptual hash, recovered verbatim. The last `_SALT_BYTE` bytes are the salt.

3. **Comparison**: The verifier computes their own perceptual hash of the received image and compares it against the recovered hash for similarity. For strict verification, `ov_verify_digest()` checks for exact byte equality.

4. **Why this is bandwidth-optimal**: The digest `P(w)` has a fixed size of `o * log2(q)` bits regardless of what we put in it. By putting the salt inside those bits rather than appending it to the signature, we get multi-target protection for free -- without increasing the transmitted data.

The relevant code path:

```c
// Signing (ov.c):
// Build target: y = phash[0..effective-1] || salt
memcpy(y, phash, _HASH_EFFECTIVE_BYTE);
memcpy(y + _HASH_EFFECTIVE_BYTE, salt, _SALT_BYTE);
// Find w such that P(w) = y via _ov_sign_target()

// Recovery + comparison:
ov_publicmap(recovered, pk->pk, signature);     // recover phash || salt
// Compare recovered[0.._HASH_EFFECTIVE_BYTE-1] against own perceptual hash
```

## Security Estimator

The Python script `uov_security_estimator.py` estimates UOV security against all known attacks:

- **Direct algebraic attack** (Groebner basis / XL algorithm)
- **Kipnis-Shamir attack** (MinRank / oil subspace recovery)
- **Intersection / reconciliation attack** (Beullens 2021)
- **Collision forgery** (birthday bound on `q^(o/2)`)

```bash
python3 uov_security_estimator.py
```

The custom parameter sets were chosen as the smallest signatures where all attack complexities exceed the target:

| Attack | PARAM=80 complexity | PARAM=100 complexity |
|--------|--------------------|--------------------|
| Direct algebraic | >>80 bits | >>100 bits |
| Kipnis-Shamir | >>80 bits | >>100 bits |
| Intersection | >80 bits | >100 bits |
| Collision forgery | 80.0 bits (SALT=0) / 72.0 bits (SALT=2) | 100.0 bits / 92.0 bits |
| **Bottleneck** | Collision forgery | Collision forgery |

The bottleneck is collision forgery at `2^(effective_hash_bits / 2)`. With the default 2-byte salt, this is `2^72` for PARAM=80 and `2^92` for PARAM=100. The salt-in-digest trades some collision resistance for multi-target attack protection without increasing signature size.

## Stego Channel Architecture (Planned)

The full pipeline (outer ECC not yet implemented):

```
Sender:
  image ──► pHash(image) ──► ov_sign_digest(phash) ──► [w]
                                                          │
                                                          ▼
                                                     [outer RS-ECC]
                                                          │
                                                          ▼
                                                     [interleaver]
                                                          │
                                                          ▼
                                                     stego_embed(image, payload)
                                                          │
                                                          ▼
                                                     image' (with embedded sig)

Receiver:
  image' ──► stego_extract ──► [deinterleave] ──► [RS-ECC decode]
                                                        │
                                                        ▼
                                                   [w]
                                                        │
                                                        ▼
                                                   P(w) = phash_sender || salt
                                                        │
                                                        ├──► phash_receiver = pHash(image')
                                                        │
                                                        ▼
                                                   similarity(phash_sender, phash_receiver)
                                                        │
                                                        ▼
                                                   authenticity score
```

## Project Structure

```
.
├── README.md                     # This file
├── uov_security_estimator.py     # Python security estimator for UOV parameters
└── pqov/                         # UOV reference implementation (modified)
    ├── Makefile                  # Modified: added PARAM=80/100, SALT=, stego_demo target
    ├── src/
    │   ├── params.h              # Modified: custom params, salt-in-digest, _HASH_EFFECTIVE_BYTE
    │   ├── ov.c                  # Modified: _ov_sign_target(), ov_sign_salt/digest(), ov_verify_digest()
    │   ├── ov.h                  # Modified: added all new API declarations
    │   ├── sign.c                # NIST API wrappers
    │   └── ...
    └── unit_tests/
        ├── sign_api-test.c       # Standard sign/verify test (500 iterations)
        └── stego_demo.c          # Message recovery demo with external digest support
```

### Summary of modifications from upstream pqov

The following modifications were made to the [upstream pqov reference implementation](https://github.com/pqov/pqov) to support compact signatures for steganographic channels:

1. **Custom parameter sets** (`PARAM=80`, `PARAM=100`): Reduced-size UOV parameters (GF(256), n=50/o=20 and n=63/o=25) providing 80-bit and 100-bit security respectively, selected via `uov_security_estimator.py` as the smallest signatures meeting the security targets.

2. **Salt-in-digest architecture**: The salt is embedded inside the recovered digest `P(w)` rather than appended to the signature. This reduces transmitted payload from `n + salt_bytes` to just `n` bytes at the cost of `salt_bytes * 4` bits of collision resistance. Configurable via `_SALT_BYTE` (default 2 bytes = 16 bits).

3. **Signature = w only**: `OV_SIGNATUREBYTES = _PUB_N_BYTE` (no appended salt). The signature is the raw solution vector `w`. The salt is recovered from `P(w)` by the verifier.

4. **External digest signing** (`ov_sign_digest()`): New API for signing an externally-provided perceptual hash (or any digest) directly, without internal SHAKE256 hashing. This preserves the verbatim bytes in the recovered digest, enabling fuzzy/similarity comparison.

5. **Oversized digest rejection**: `ov_sign_digest()` returns `-2` if the provided digest exceeds `_HASH_EFFECTIVE_BYTE` bytes, preventing silent truncation that would make the full pHash unrecoverable.

6. **User-provided salt** (`ov_sign_salt()`, `ov_sign_digest()` with salt parameter): Allows deterministic signing with a caller-specified salt (hashed to `_SALT_BYTE` bytes). Enables reproducible signatures for testing and application-specific nonces.

7. **Digest verification** (`ov_verify_digest()`): New API for verifying a signature against an externally-provided digest, bypassing internal SHAKE256 hashing.

8. **Message recovery via `ov_publicmap()`**: While not new code (the function existed upstream), the salt-in-digest architecture makes it useful: the verifier evaluates `P(w)` to recover the sender's perceptual hash verbatim from the first `_HASH_EFFECTIVE_BYTE` bytes.

### Files modified from upstream pqov

| File | Changes |
|------|---------|
| `pqov/src/params.h` | Added `_OV256_50_20` (80-bit) and `_OV256_63_25` (100-bit) parameter definitions; salt-in-digest architecture (`OV_SIGNATUREBYTES = _PUB_N_BYTE`, `_HASH_EFFECTIVE_BYTE`); configurable `_SALT_BYTE` default 2 |
| `pqov/src/ov.c` | Rewrote signing/verify for salt-in-digest; refactored into `_ov_sign_target()` core; added `ov_sign_salt()`, `ov_sign_digest()`, `ov_verify_digest()`; **rejects oversized digests** (returns `-2` if `digest_len > _HASH_EFFECTIVE_BYTE`) to prevent silent truncation |
| `pqov/src/ov.h` | Added `ov_sign_salt()`, `ov_sign_digest()`, `ov_verify_digest()` declarations |
| `pqov/Makefile` | Added `PARAM=80`, `PARAM=100`, `SALT=` build variables; added `stego_demo` target |

### Files added

| File | Description |
|------|-------------|
| `uov_security_estimator.py` | Estimates UOV security against all known attacks, searches for optimal parameters |
| `pqov/unit_tests/stego_demo.c` | Demonstrates salt-in-digest message recovery, external digest signing, user-provided salt |

## Threat Model

- **Attacker**: Adaptive chosen-message attacker (EUF-CMA). The attacker can request signatures on digests of their choice and then attempts to forge a valid signature on a new digest. This is the standard security model for digital signatures.
- **Channel**: Noisy steganographic channel embedded in images, with bit flips and erasures
- **Goal**: Authenticate a perceptual image hash through the stego channel, enabling similarity-based verification
- **Security target**: Existential unforgeability under adaptive chosen-message attack (EUF-CMA)
- **Public key**: Exchanged out-of-band (up to ~1 MB acceptable)
- **One image = one signature**

### Security of fuzzy verification

The UOV signature itself provides standard EUF-CMA security: an attacker cannot forge a signature on a new digest without the secret key. The "fuzzy" aspect is entirely in the verification policy:

- **Strict verification** (`ov_verify_digest()`): checks that the recovered perceptual hash exactly matches the expected one. This has full cryptographic guarantees.
- **Similarity verification** (application layer): the recovered perceptual hash is compared for similarity against a freshly computed hash. The similarity threshold is a policy decision, not a cryptographic property. A higher threshold is more secure (fewer false positives) but less tolerant of image modifications.

An attacker who does not possess the secret key cannot produce a signature that recovers to any chosen perceptual hash -- this is guaranteed by UOV's unforgeability. The attacker could, however, present a different image that happens to have a similar perceptual hash (a perceptual hash collision). The resistance to this depends on the perceptual hash function, not on UOV.

### Salt and multi-target security under CMA

The salt embedded in the digest provides multi-target attack resistance. Under CMA, the attacker can request signatures on chosen digests. Without a salt, the attacker can pre-compute collisions across all digests they intend to query -- with `2^k` queries, collision resistance drops by `k` bits. The salt forces each signature to target a unique digest value, preventing this batching.

With `SALT=0` (no salt), signing is fully deterministic and the full `_PUB_M_BYTE * 8 / 2` bits of collision resistance apply to single-target attacks. Multi-target attacks reduce this by `log2(num_queries)` bits.

With the default `SALT=2` (16-bit salt), each query targets a unique digest and multi-target batching requires `2^16` queries on the same digest with different salts (which doesn't happen since the signer chooses the salt randomly). The trade-off is that base collision resistance is reduced by `_SALT_BYTE * 8 / 2` bits.

## References

- [PQOV: Post-Quantum Oil and Vinegar](https://www.pqov.org/) -- NIST PQC Additional Signatures Round 2 candidate
- [pqov/pqov](https://github.com/pqov/pqov) -- Reference implementation (upstream)
- Kipnis & Shamir, "Cryptanalysis of the Oil & Vinegar Signature Scheme" (1998)
- Beullens, "Improved Cryptanalysis of UOV and Rainbow" (Eurocrypt 2021)
- Beullens et al., "Oil and Vinegar: Modern Parameters and Implementations" (2023)
- Zauner, "Implementation and Benchmarking of Perceptual Image Hash Functions" (2010) -- pHash reference

## License

The UOV reference implementation (`pqov/`) is licensed under CC0 or Apache 2.0.
The additions in this repository (security estimator, stego demo, parameter modifications) follow the same dual license.
