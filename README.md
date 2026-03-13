# UOV Signatures for Bandwidth-Constrained Steganographic Channels

This project implements compact post-quantum digital signatures based on [UOV (Oil and Vinegar)](https://www.pqov.org/) with **salt-in-digest message recovery**, designed for steganographic channels where bandwidth is extremely limited (~400-500 usable bits).

The key insight: UOV signatures have **inherent message recovery** -- the verifier can recover the hash digest directly from the signature by evaluating the public map `P(w)`. We exploit this by embedding a random salt inside the recovered digest, so the salt never needs to be transmitted. The signature is just `w` -- nothing else.

## Overview

```
Sender                          Stego Channel              Receiver
                                (noisy, ~400-500 bits)
  message ──► sign(sk, msg)                                  has: pk (out-of-band)
              │                                                    message (own copy)
              ▼                                                    │
         ┌──────────┐         ┌──────────────┐                    ▼
         │signature │ ──────► │  stego embed  │ ──────► P(w) = H(msg)[trunc] || salt
         │  w only  │         │  (image)      │         ▲                       ▲
         │(400 bits)│         └──────────────┘         hash verified        salt recovered
         └──────────┘                                  against H(msg)      (not transmitted!)
```

**What goes through the stego channel:** Only the signature vector `w` (400 or 504 bits).

**What goes out-of-band (one-time setup):** The public key (~4-50 KB).

## Signature Structure and Salt-in-Digest

A UOV signature consists of a single vector `w`:

```
signature = w                 (_PUB_N_BYTE bytes, the ONLY transmitted data)

recovered digest = P(w)       (_PUB_M_BYTE bytes, recovered by the verifier)
                 = H(msg)[0 .. _HASH_EFFECTIVE_BYTE-1] || salt[0 .. _SALT_BYTE-1]
                   ▲                                       ▲
                   hash of message, truncated               random nonce, embedded
                   (verified against H(msg))                (NOT transmitted)
```

**How it works:**

1. **Signing**: The signer computes `H(message)` truncated to `_HASH_EFFECTIVE_BYTE = _PUB_M_BYTE - _SALT_BYTE` bytes, appends `_SALT_BYTE` bytes of random salt, forming the target vector `y`. Then it finds `w` such that `P(w) = y`. The signature is just `w`.

2. **Verification**: The verifier evaluates `P(w)` to recover the full digest. It computes `H(message)` truncated to `_HASH_EFFECTIVE_BYTE` bytes and compares against the first `_HASH_EFFECTIVE_BYTE` bytes of the recovered digest. The last `_SALT_BYTE` bytes are the salt -- they are recovered but not checked (any value is valid).

3. **Salt benefits without bandwidth cost**: The salt occupies bits inside the fixed-size digest `P(w)`, not extra bytes in the signature. This provides multi-target attack resistance at zero transmission cost, at the expense of reducing the effective hash length (and thus collision resistance) by `_SALT_BYTE` bytes.

### User-provided salt

The `ov_sign_salt()` function accepts an optional user-provided salt of any length. If provided, it is hashed down to `_SALT_BYTE` bytes and embedded in the digest. This enables:

- **Deterministic signing**: same message + same salt = same signature
- **Application-specific nonces**: the caller can provide a timestamp, counter, or other context
- **Arbitrary salt length**: the salt is hashed, so any length works

```c
// Random salt (default):
ov_sign(signature, sk, message, mlen);

// User-provided salt (any length):
ov_sign_salt(signature, sk, message, mlen, my_salt, my_salt_len);
```

If `user_salt` is NULL or `user_salt_len` is 0, a random salt is generated.

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

The signature size is always the same regardless of salt. More salt = more multi-target protection but less collision resistance.

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

## Message Recovery: How It Works

UOV has an inherent message recovery property that most descriptions overlook:

1. **Signing**: The signer builds a target `y = H(message)[truncated] || salt`, then finds a signature vector `w` such that `P(w) = y`, where `P` is the public multivariate quadratic map.

2. **Verification**: The verifier evaluates `P(w)` to recover `y`, computes `H(message)` truncated to `_HASH_EFFECTIVE_BYTE` bytes, and checks that it matches the first part of the recovered `y`. The last `_SALT_BYTE` bytes are ignored (any salt value is accepted).

3. **Why this is bandwidth-optimal**: The digest `P(w)` has a fixed size of `o * log2(q)` bits regardless of what we put in it. By putting the salt inside those bits rather than appending it to the signature, we get multi-target protection for free -- without increasing the transmitted data.

The relevant code path:

```c
// Signing (ov.c):
// Build target: y = H(msg)[0..effective-1] || salt
hash_final_digest(y, _HASH_EFFECTIVE_BYTE, &hctx_msg);
memcpy(y + _HASH_EFFECTIVE_BYTE, salt, _SALT_BYTE);
// Find w such that P(w) = y ... signature is just w.

// Verification (ov.c):
// Recover y = P(w), check hash portion matches H(msg)
ov_publicmap(digest_ck, pk->pk, signature);
hash_final_digest(correct, _HASH_EFFECTIVE_BYTE, &hctx);
// Compare correct[0.._HASH_EFFECTIVE_BYTE-1] against digest_ck[0.._HASH_EFFECTIVE_BYTE-1]
```

The `stego_demo.c` program demonstrates this explicitly, including signing with user-provided salts.

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
  message ──► UOV sign ──► [w]
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
                                                         [w]
                                                              │
                                                              ▼
                                                         P(w) = H(msg)[trunc] || salt
                                                         verify hash portion vs H(msg) ✓
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
    │   ├── ov.c                  # Modified: salt-in-digest signing/verify, ov_sign_salt()
    │   ├── ov.h                  # Modified: added ov_sign_salt() declaration
    │   ├── sign.c                # NIST API wrappers
    │   └── ...
    └── unit_tests/
        ├── sign_api-test.c       # Standard sign/verify test (500 iterations)
        └── stego_demo.c          # Salt-in-digest message recovery demo
```

### Files modified from upstream pqov

| File | Changes |
|------|---------|
| `pqov/src/params.h` | Added `_OV256_50_20` (80-bit) and `_OV256_63_25` (100-bit) parameter definitions; salt-in-digest architecture (`OV_SIGNATUREBYTES = _PUB_N_BYTE`, `_HASH_EFFECTIVE_BYTE`); configurable `_SALT_BYTE` default 2 |
| `pqov/src/ov.c` | Rewrote `ov_sign` / `_ov_verify` for salt-in-digest: target is `H(msg)[trunc] \|\| salt`, verification checks only hash portion; added `ov_sign_salt()` for user-provided salt |
| `pqov/src/ov.h` | Added `ov_sign_salt()` declaration |
| `pqov/Makefile` | Added `PARAM=80`, `PARAM=100`, `SALT=` build variables; added `stego_demo` target |

### Files added

| File | Description |
|------|-------------|
| `uov_security_estimator.py` | Estimates UOV security against all known attacks, searches for optimal parameters |
| `pqov/unit_tests/stego_demo.c` | Demonstrates salt-in-digest message recovery, user-provided salt, deterministic signing |

## Threat Model

- **Attacker**: Adaptive chosen-message attacker (EUF-CMA). The attacker can request signatures on messages of their choice and then attempts to forge a valid signature on a new message. This is the standard security model for digital signatures.
- **Channel**: Noisy steganographic channel with bit flips and erasures
- **Goal**: Authenticate a hash digest (e.g., image fingerprint) through the stego channel
- **Security target**: Existential unforgeability under adaptive chosen-message attack (EUF-CMA)
- **Public key**: Exchanged out-of-band (up to ~1 MB acceptable)
- **One image = one message**

### Salt and multi-target security under CMA

The salt embedded in the digest provides multi-target attack resistance. Under CMA, the attacker can request signatures on chosen messages. Without a salt, the attacker can pre-compute collisions across all messages they intend to query -- with `2^k` queries, collision resistance drops by `k` bits. The salt forces each signature to target a unique digest value, preventing this batching.

With `SALT=0` (no salt), signing is fully deterministic and the full `_PUB_M_BYTE * 8 / 2` bits of collision resistance apply to single-target attacks. Multi-target attacks reduce this by `log2(num_queries)` bits.

With the default `SALT=2` (16-bit salt), each query targets a unique digest and multi-target batching requires `2^16` queries on the same message with different salts (which doesn't happen since the signer chooses the salt randomly). The trade-off is that base collision resistance is reduced by `_SALT_BYTE * 8 / 2` bits.

## References

- [PQOV: Post-Quantum Oil and Vinegar](https://www.pqov.org/) -- NIST PQC Additional Signatures Round 2 candidate
- [pqov/pqov](https://github.com/pqov/pqov) -- Reference implementation (upstream)
- Kipnis & Shamir, "Cryptanalysis of the Oil & Vinegar Signature Scheme" (1998)
- Beullens, "Improved Cryptanalysis of UOV and Rainbow" (Eurocrypt 2021)
- Beullens et al., "Oil and Vinegar: Modern Parameters and Implementations" (2023)

## License

The UOV reference implementation (`pqov/`) is licensed under CC0 or Apache 2.0.
The additions in this repository (security estimator, stego demo, parameter modifications) follow the same dual license.
