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
         │(528 bits)│         │  (image)      │         recovered digest!
         └──────────┘         └──────────────┘         verify: P(s) == H(msg||salt) ✓
              │
              ▼
         NO digest transmitted
         (recovered from signature)
```

**What goes through the stego channel:** Only the signature (528 or 632 bits).

**What goes out-of-band (one-time setup):** The public key (~4-50 KB).

## Parameter Sets

This project provides two custom reduced-security parameter sets optimized for minimal signature size, plus access to all standard NIST parameter sets.

### Custom Parameter Sets (for stego channels)

| Parameter | `PARAM=80` | `PARAM=100` |
|-----------|-----------|------------|
| **Security** | 80 bits | 100 bits |
| **Field** | GF(256) | GF(256) |
| **v (vinegar vars)** | 30 | 38 |
| **o (oil vars)** | 20 | 25 |
| **n = v + o** | 50 | 63 |
| **Signature size** | 66 bytes = **528 bits** | 79 bytes = **632 bits** |
| **Recovered hash** | 160 bits (80-bit collision resistance) | 200 bits (100-bit collision resistance) |
| **Public key (classic)** | 25,500 bytes | 50,400 bytes |
| **Public key (compressed)** | 4,216 bytes | 8,141 bytes |
| **Params define** | `_OV256_50_20` | `_OV256_63_25` |

These parameters were selected using the included security estimator (`uov_security_estimator.py`) to minimize signature size while achieving the target security level against all known attacks (direct algebraic, Kipnis-Shamir, intersection/reconciliation, and collision forgery).

### Standard NIST Parameter Sets

| Parameter | NIST Level | `PARAM=` | Field | (n, o) | Signature | Public Key (classic) |
|-----------|-----------|----------|-------|--------|-----------|---------------------|
| uov-Is | 1 | `1` (default) | GF(16) | (160, 64) | 96 B = 768 bits | 412,160 B |
| uov-Ip | 3 | `3` | GF(256) | (112, 44) | 128 B = 1024 bits | 278,432 B |
| uov-III | 3 | `4` | GF(256) | (184, 72) | 200 B = 1600 bits | 1,225,440 B |
| uov-V | 5 | `5` | GF(256) | (244, 96) | 260 B = 2080 bits | 2,869,440 B |

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

# 80-bit security (528-bit signature) -- fits in ~500-bit channel
make clean && make stego_demo PARAM=80

# 100-bit security (632-bit signature) -- fits in ~1000-bit channel
make clean && make stego_demo PARAM=100

# Standard NIST Level 1 (768-bit signature)
make clean && make stego_demo PARAM=1

# Run it
./stego_demo
```

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

### PARAM reference

| `PARAM=` | Params | Security | Signature |
|----------|--------|----------|-----------|
| `80` | GF(256), n=50, o=20 | ~80 bits | 528 bits |
| `100` | GF(256), n=63, o=25 | ~100 bits | 632 bits |
| `1` (default) | GF(16), n=160, o=64 | ~128 bits (NIST L1) | 768 bits |
| `3` | GF(256), n=112, o=44 | ~176 bits (NIST L1) | 1024 bits |
| `4` | GF(256), n=184, o=72 | ~256 bits (NIST L3) | 1600 bits |
| `5` | GF(256), n=244, o=96 | ~384 bits (NIST L5) | 2080 bits |

## Message Recovery: How It Works

UOV has an inherent message recovery property that most descriptions overlook:

1. **Signing**: The signer computes `h = H(message || salt)`, then finds a signature vector `s` such that `P(s) = h`, where `P` is the public multivariate quadratic map.

2. **Verification**: The verifier evaluates `P(s)` on the received signature to recover `h`, then checks that `h == H(message || salt)`.

3. **The key observation**: The verifier does not need `h` to be transmitted -- it is **recovered** from the signature by evaluating `P(s)`. This means:
   - The signature alone is sufficient for the stego channel payload
   - The recovered `h` is `o * log2(q)` bits (160 bits for `PARAM=80`, 200 bits for `PARAM=100`)
   - Collision resistance equals half the hash length (80 or 100 bits respectively)

The relevant code path in the UOV reference implementation:

```c
// In ov.c, ov_verify():
ov_publicmap(digest_ck, pk->pk, signature);  // Recover h = P(s)
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
  message ──► UOV sign ──► [signature bytes]
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
                                                         [signature bytes]
                                                              │
                                                              ▼
                                                         P(s) = recovered hash
                                                         verify vs H(msg||salt) ✓
```

## Project Structure

```
.
├── README.md                     # This file
├── uov_security_estimator.py     # Python security estimator for UOV parameters
└── pqov/                         # UOV reference implementation (modified)
    ├── Makefile                  # Modified: added PARAM=80, PARAM=100, stego_demo target
    ├── src/
    │   ├── params.h              # Modified: added _OV256_50_20 and _OV256_63_25
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
| `pqov/src/params.h` | Added `_OV256_50_20` (80-bit) and `_OV256_63_25` (100-bit) parameter definitions; updated the preprocessor guard to include new defines |
| `pqov/Makefile` | Added `PARAM=80` and `PARAM=100` to the parameter selection logic; added `stego_demo` build target |

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
