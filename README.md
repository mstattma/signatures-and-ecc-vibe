# UOV Signatures for Bandwidth-Constrained Steganographic Channels

This project implements compact post-quantum digital signatures based on [UOV (Oil and Vinegar)](https://www.pqov.org/) with **message recovery**, designed for steganographic channels where bandwidth is extremely limited (~500-1000 usable bits).

The key insight: UOV signatures have **inherent message recovery** -- the verifier can recover the hash digest directly from the signature by evaluating the public map `P(s)`, eliminating the need to transmit the digest separately. Only the signature travels through the stego channel.

## Overview

```
Sender                          Stego Channel              Receiver
                                (noisy, ~500-1000 bits)
  message ──► sign(sk, msg)                                  has: pk (out-of-band)
              │                                                    message (own copy)
              ▼                                                    │
         ┌──────────┐         ┌──────────────┐                    ▼
         │signature │ ──────► │  stego embed  │ ──────► P(s) = H(msg||salt)
         │(432 bits)│         │  (image)      │         recovered digest!
         └──────────┘         └──────────────┘         verify: P(s) == H(msg||salt) ✓
              │
              ▼
         NO digest transmitted
         (recovered from signature)
```

**What goes through the stego channel:** Only the signature (432 or 536 bits with default 4-byte salt).

**What goes out-of-band (one-time setup):** The public key (~4-50 KB).

## Signature Structure

A UOV signature consists of two concatenated parts:

```
signature = w || salt
            │      │
            │      └─ random nonce, _SALT_BYTE bytes (default: 4 bytes = 32 bits)
            │
            └─ signature vector, _PUB_N_BYTE bytes (n field elements)
               the verifier evaluates P(w) to recover h = H(message || salt)
```

**The signature vector `w`** is the core cryptographic payload. It is a vector of `n` elements in GF(q) that satisfies `P(w) = H(message || salt)`, where `P` is the public multivariate quadratic map. The verifier recovers the hash digest by evaluating `P(w)` -- this is the message recovery property.

**The salt** is a random nonce generated during signing and appended to the signature in the clear. The verifier extracts the salt from the end of the signature and uses it to compute `H(message || salt)` for comparison against `P(w)`. The salt serves two purposes:

1. **Derandomized signing**: The salt feeds into the deterministic derivation of vinegar variable candidates via `H(message || salt || sk_seed || ctr)`. A fresh salt ensures a fresh target hash `h`, which helps when the internal linear system happens to be singular for a given set of vinegar values.

2. **Multi-target attack resistance**: Without a salt, an attacker observing multiple signatures for different messages could batch collision-finding across all of them. The salt forces each signature to target a unique hash value, making multi-target forgery no cheaper than single-target.

The salt does **not** affect core UOV security (direct algebraic attacks, Kipnis-Shamir, intersection attacks). It only impacts multi-target collision resistance. For a passive eavesdropper seeing `2^k` signatures, the effective collision resistance is reduced by `k` bits. With a 4-byte (32-bit) salt, an attacker would need to observe `2^32` distinct signatures before gaining any advantage -- far beyond typical stego channel usage.

**Total signature size** = `n` field element bytes + salt bytes:

| Parameter set | `w` (vector) | salt (default) | **Total** |
|--------------|-------------|---------------|-----------|
| `PARAM=80` | 50 bytes = 400 bits | 4 bytes = 32 bits | **54 bytes = 432 bits** |
| `PARAM=100` | 63 bytes = 504 bits | 4 bytes = 32 bits | **67 bytes = 536 bits** |

The salt length is configurable at build time via `SALT=` (see [Building](#building)). Increasing the salt increases the signature by the same amount:

| Salt length | PARAM=80 signature | PARAM=100 signature |
|------------|-------------------|-------------------|
| 0 bytes | 50 B = **400 bits** | 63 B = **504 bits** |
| 4 bytes (default) | 54 B = **432 bits** | 67 B = **536 bits** |
| 8 bytes | 58 B = 464 bits | 71 B = 568 bits |
| 16 bytes (upstream default) | 66 B = 528 bits | 79 B = 632 bits |

**`SALT=0` note:** With no salt, the hash target is `H(message)` rather than `H(message || salt)`. This means signing the same message twice produces the same signature (fully deterministic signing), and an attacker observing multiple signatures can attempt multi-target collision attacks across all distinct messages. This is acceptable when the number of signed messages is small (a few thousand or less) and the use case does not require non-repudiation of distinct signing events. For the stego channel use case with a passive eavesdropper and low message volume, `SALT=0` is a reasonable choice to minimize bandwidth.

## Parameter Sets

This project provides two custom reduced-security parameter sets optimized for minimal signature size, plus access to all standard NIST parameter sets. All signature sizes below use the default 4-byte salt.

### Custom Parameter Sets (for stego channels)

| Parameter | `PARAM=80` | `PARAM=100` |
|-----------|-----------|------------|
| **Security** | 80 bits | 100 bits |
| **Field** | GF(256) | GF(256) |
| **v (vinegar vars)** | 30 | 38 |
| **o (oil vars)** | 20 | 25 |
| **n = v + o** | 50 | 63 |
| **Signature size** | 54 bytes = **432 bits** | 67 bytes = **536 bits** |
| **Recovered hash** | 160 bits (80-bit collision resistance) | 200 bits (100-bit collision resistance) |
| **Public key (classic)** | 25,500 bytes | 50,400 bytes |
| **Public key (compressed)** | 4,216 bytes | 8,141 bytes |
| **Params define** | `_OV256_50_20` | `_OV256_63_25` |

These parameters were selected using the included security estimator (`uov_security_estimator.py`) to minimize signature size while achieving the target security level against all known attacks (direct algebraic, Kipnis-Shamir, intersection/reconciliation, and collision forgery).

### Standard NIST Parameter Sets

The standard NIST parameter sets are also available. Note that the upstream UOV implementation uses a 16-byte salt; with the default 4-byte salt in this project, the NIST signatures are 12 bytes smaller than upstream. Use `SALT=16` to match the upstream signature sizes exactly.

| Parameter | NIST Level | `PARAM=` | Field | (n, o) | Signature (4B salt) | Signature (16B salt, upstream) | Public Key (classic) |
|-----------|-----------|----------|-------|--------|-------------------|-------------------------------|---------------------|
| uov-Is | 1 | `1` (default) | GF(16) | (160, 64) | 84 B = 672 bits | 96 B = 768 bits | 412,160 B |
| uov-Ip | 1 | `3` | GF(256) | (112, 44) | 116 B = 928 bits | 128 B = 1024 bits | 278,432 B |
| uov-III | 3 | `4` | GF(256) | (184, 72) | 188 B = 1504 bits | 200 B = 1600 bits | 1,225,440 B |
| uov-V | 5 | `5` | GF(256) | (244, 96) | 248 B = 1984 bits | 260 B = 2080 bits | 2,869,440 B |

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

# 80-bit security (432-bit signature with default 4-byte salt)
make clean && make stego_demo PARAM=80

# 100-bit security (536-bit signature with default 4-byte salt)
make clean && make stego_demo PARAM=100

# Standard NIST Level 1 (672-bit signature with default 4-byte salt)
make clean && make stego_demo PARAM=1

# Run it
./stego_demo
```

### Configuring the salt length

The salt length defaults to 4 bytes. Override it with the `SALT=` parameter:

```bash
# No salt -- minimal signature, deterministic signing (400-bit signature for PARAM=80)
make clean && make stego_demo PARAM=80 SALT=0

# Use 8-byte salt (464-bit signature for PARAM=80)
make clean && make stego_demo PARAM=80 SALT=8

# Use 16-byte salt to match upstream UOV (528-bit signature for PARAM=80)
make clean && make stego_demo PARAM=80 SALT=16
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

# Run with upstream-compatible 16-byte salt
make clean && make test PARAM=80 SALT=16
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

All signature sizes below use the default 4-byte salt. Add `(SALT - 4)` bytes for other salt lengths.

| `PARAM=` | Params | Security | Signature |
|----------|--------|----------|-----------|
| `80` | GF(256), n=50, o=20 | ~80 bits | 432 bits |
| `100` | GF(256), n=63, o=25 | ~100 bits | 536 bits |
| `1` (default) | GF(16), n=160, o=64 | ~128 bits (NIST L1) | 672 bits |
| `3` | GF(256), n=112, o=44 | ~176 bits (NIST L1) | 928 bits |
| `4` | GF(256), n=184, o=72 | ~256 bits (NIST L3) | 1504 bits |
| `5` | GF(256), n=244, o=96 | ~384 bits (NIST L5) | 1984 bits |

| `SALT=` | Salt length | Notes |
|---------|-------------|-------|
| `0` | 0 bytes | No salt, deterministic signing, minimal size |
| (omitted) | 4 bytes = 32 bits | Default, suitable for low-volume stego channels |
| `8` | 8 bytes = 64 bits | Good for moderate signature volumes |
| `16` | 16 bytes = 128 bits | Matches upstream UOV / NIST submission |

| `VARIANT=` | Key format | Notes |
|------------|-----------|-------|
| `1` (default) | Classic (uncompressed PK + SK) | Fastest verification |
| `2` | PKC (compressed PK, uncompressed SK) | Smaller public key |
| `3` | PKC+SKC (compressed PK + SK) | Smallest keys, slower sign |

## Message Recovery: How It Works

UOV has an inherent message recovery property that most descriptions overlook:

1. **Signing**: The signer computes `h = H(message || salt)`, then finds a signature vector `w` such that `P(w) = h`, where `P` is the public multivariate quadratic map. The signature is `w || salt`.

2. **Verification**: The verifier splits the signature into `w` and `salt`, evaluates `P(w)` to recover `h`, then computes `H(message || salt)` independently and checks that they match.

3. **The key observation**: The verifier does not need `h` to be transmitted -- it is **recovered** from the signature by evaluating `P(w)`. This means:
   - The signature alone is sufficient for the stego channel payload
   - The recovered `h` is `o * log2(q)` bits (160 bits for `PARAM=80`, 200 bits for `PARAM=100`)
   - Collision resistance equals half the hash length (80 or 100 bits respectively)

The relevant code path in the UOV reference implementation:

```c
// In ov.c, ov_verify():
ov_publicmap(digest_ck, pk->pk, signature);  // Recover h = P(w)
// Then compare digest_ck against H(message || salt)
```

The `stego_demo.c` program demonstrates this explicitly by:
1. Generating a key pair
2. Signing a message
3. Recovering the digest from the signature using `ov_publicmap()`
4. Independently computing `H(message || salt)` and confirming they match

## Security Estimator

The Python script `uov_security_estimator.py` estimates UOV security against all known attacks:

- **Direct algebraic attack** (Groebner basis / XL algorithm)
- **Kipnis-Shamir attack** (MinRank / oil subspace recovery)
- **Intersection / reconciliation attack** (Beullens 2021)
- **Collision forgery** (birthday bound on `q^(o/2)`)

```bash
python3 uov_security_estimator.py
```

This outputs:
- Security estimates for all NIST reference parameter sets
- A search for minimal-signature parameters at 80-bit and 100-bit security
- Detailed attack complexity breakdowns

The custom parameter sets were chosen as the smallest signatures where all attack complexities exceed the target:

| Attack | PARAM=80 complexity | PARAM=100 complexity |
|--------|--------------------|--------------------|
| Direct algebraic | >>80 bits | >>100 bits |
| Kipnis-Shamir | >>80 bits | >>100 bits |
| Intersection | >80 bits | >100 bits |
| Collision forgery | 80.0 bits | 100.0 bits |
| **Bottleneck** | Collision forgery | Collision forgery |

The bottleneck in both cases is collision forgery (birthday bound), which is `q^(o/2) = 256^(o/2)` = `2^(4*o)`. For o=20 this is `2^80`, for o=25 this is `2^100`.

## Stego Channel Architecture (Planned)

The full pipeline (outer ECC not yet implemented):

```
Sender:
  message ──► UOV sign ──► [w || salt]
                                │
                                ▼
                           [outer RS-ECC]  (adds redundancy for bit flips/erasures)
                                │
                                ▼
                           [interleaver]   (spreads burst errors)
                                │
                                ▼
                           [stego embed]   (hide in cover image)

Receiver:
  stego image ──► [stego extract] ──► [deinterleave] ──► [RS-ECC decode]
                                                              │
                                                              ▼
                                                         [w || salt]
                                                              │
                                                              ▼
                                                         P(w) = recovered hash
                                                         verify vs H(msg||salt) ✓
```

## Project Structure

```
.
├── README.md                     # This file
├── uov_security_estimator.py     # Python security estimator for UOV parameters
└── pqov/                         # UOV reference implementation (modified)
    ├── Makefile                  # Modified: added PARAM=80/100, SALT=, stego_demo target
    ├── src/
    │   ├── params.h              # Modified: added custom params, configurable salt (default 4B)
    │   ├── ov.c                  # Core sign/verify (ov_publicmap = message recovery)
    │   ├── ov.h                  # API declarations
    │   ├── sign.c                # NIST API wrappers
    │   └── ...
    └── unit_tests/
        ├── sign_api-test.c       # Standard sign/verify test (500 iterations)
        └── stego_demo.c          # Message recovery demo for stego channel
```

### Files modified from upstream pqov

| File | Changes |
|------|---------|
| `pqov/src/params.h` | Added `_OV256_50_20` (80-bit) and `_OV256_63_25` (100-bit) parameter definitions; updated preprocessor guard; changed `_SALT_BYTE` default from 16 to 4 and made it overridable via `-D_SALT_BYTE=N` |
| `pqov/Makefile` | Added `PARAM=80` and `PARAM=100` to parameter selection; added `SALT=` build variable; added `stego_demo` build target |

### Files added

| File | Description |
|------|-------------|
| `uov_security_estimator.py` | Estimates UOV security against all known attacks, searches for optimal parameters |
| `pqov/unit_tests/stego_demo.c` | Demonstrates message recovery: sign, recover digest via `ov_publicmap()`, verify match |

## Threat Model

- **Attacker**: Passive eavesdropper (observes images but does not modify them)
- **Channel**: Noisy steganographic channel with bit flips and erasures
- **Goal**: Authenticate a hash digest (e.g., image fingerprint) through the stego channel
- **Public key**: Exchanged out-of-band (up to ~1 MB acceptable)
- **One image = one message**

## References

- [PQOV: Post-Quantum Oil and Vinegar](https://www.pqov.org/) -- NIST PQC Additional Signatures Round 2 candidate
- [pqov/pqov](https://github.com/pqov/pqov) -- Reference implementation (upstream)
- Kipnis & Shamir, "Cryptanalysis of the Oil & Vinegar Signature Scheme" (1998)
- Beullens, "Improved Cryptanalysis of UOV and Rainbow" (Eurocrypt 2021)
- Beullens et al., "Oil and Vinegar: Modern Parameters and Implementations" (2023)

## License

The UOV reference implementation (`pqov/`) is licensed under CC0 or Apache 2.0.
The additions in this repository (security estimator, stego demo, parameter modifications) follow the same dual license.
