# BLS Signatures for Bandwidth-Constrained Steganographic Channels

This directory implements BLS digital signatures for a steganographic channel where bandwidth is extremely limited (~500-1000 usable bits). Two pairing-friendly curves are supported:

- **BN-P158** (~78-bit classical security, ~168-bit signatures)
- **BLS12-381** (~117-120 bit classical security, ~392-bit signatures)

Unlike the [UOV implementation](../UOV/), BLS signatures have **no message recovery** -- the perceptual hash (pHash) must be transmitted explicitly alongside the signature. However, BLS signatures are very compact, and the public key can optionally be appended for in-band distribution.

**Neither curve is quantum-safe** -- both are broken by Shor's algorithm.

## Overview

```
Sender                          Stego Channel              Receiver
                                (noisy, ~500 bits)
  pHash ──► BLS sign(sk, pHash)                              has: pk (out-of-band or in-band)
              │                                                    pHash (to verify)
              ▼                                                    │
         ┌──────────────────┐   ┌──────────────┐                  ▼
         │ pHash || sig     │──►│  stego embed  │──► extract pHash, sig [, pk]
         │ [|| pk optional] │   │  (image)      │    verify: BLS_verify(sig, pHash, pk) ✓
         └──────────────────┘   └──────────────┘
```

**What goes through the stego channel:** `pHash || signature` (and optionally `|| public key`).

**What goes out-of-band (if PK not in-band):** The public key (41 or 97 bytes).

## Curve Parameters (measured from RELIC)

| Parameter | BN-P158 | BLS12-381 |
|---|---|---|
| Prime field size | 158 bits | 381 bits |
| Embedding degree | 12 | 12 |
| Signature (G1 compressed) | **21 bytes = 168 bits** | **49 bytes = 392 bits** |
| Public key (G2 compressed) | **41 bytes = 328 bits** | **97 bytes = 776 bits** |
| Secret key | 20 bytes = 160 bits | 32 bytes = 256 bits |
| Sig + PK total | 62 bytes = 496 bits | 146 bytes = 1168 bits |
| Classical security | ~78 bits | ~117-120 bits [^1] |
| Quantum security | 0 (Shor) | 0 (Shor) |

[^1]: BLS12-381 was designed to target 128-bit security, but improved tower NFS algorithms (Kim-Barbulescu 2016, Barbulescu-Duquesne 2017) reduce the effective security of the pairing target group GT (GF(p^12) = 4572 bits) to approximately 117-120 bits. The RELIC library reports 128 bits as it uses the pre-NFS-update estimate.

### Why BN-158 instead of MNT-159?

No modern cryptographic library supports MNT-159 curves. BN-158 (Barreto-Naehrig with 158-bit prime, embedding degree 12) serves the same role but is actually **superior**:

| Property | MNT-159 (k=3) | MNT-159 (k=6) | BN-158 (k=12) |
|---|---|---|---|
| GT field size | 477 bits | 954 bits | 1896 bits |
| G1 compressed | ~20 bytes | ~20 bytes | 21 bytes |
| G2 compressed | ~60 bytes | ~120 bytes | 41 bytes |
| Effective security | ~40-50 bits | ~60-70 bits | **~78 bits** |
| Bottleneck | GT (trivially weak) | GT (tower NFS) | G1 (Pollard rho) |

BN-158's higher embedding degree (12 vs 3 or 6) pushes the GT field to 1896 bits, making NFS in GT much harder. The security bottleneck becomes the G1 discrete log (~78 bits via Pollard rho), which is the correct behavior.

## Payload Sizes

The stego channel payload format is: `[pHash] || [BLS signature (G1)] || [optional: PK (G2)]`

### BN-P158 (signature = 21 bytes = 168 bits)

| pHash | pHash bits | Sig bits | **Total (no PK)** | PK bits | **Total (with PK)** |
|---|---|---|---|---|---|
| 96-bit | 96 | 168 | **264 bits** | 328 | **592 bits** |
| 144-bit | 144 | 168 | **312 bits** | 328 | **640 bits** |
| 184-bit | 184 | 168 | **352 bits** | 328 | **680 bits** |

### BLS12-381 (signature = 49 bytes = 392 bits)

| pHash | pHash bits | Sig bits | **Total (no PK)** | PK bits | **Total (with PK)** |
|---|---|---|---|---|---|
| 96-bit | 96 | 392 | **488 bits** | 776 | **1264 bits** |
| 144-bit | 144 | 392 | **536 bits** | 776 | **1312 bits** |
| 184-bit | 184 | 392 | **576 bits** | 776 | **1352 bits** |

### Comparison with UOV

UOV uses salt-in-digest message recovery: the pHash is recovered from the signature, so only the signature is transmitted.

| Scheme | Payload bits | pHash bits | PK in-band? | Quantum-safe? | Security |
|---|---|---|---|---|---|
| **BN-P158 + 96-bit pHash** | **264** | 96 (transmitted) | Optional (+328) | No | ~78 bits |
| **BN-P158 + 144-bit pHash** | **312** | 144 (transmitted) | Optional (+328) | No | ~78 bits |
| **UOV-80** | **400** | 144 (recovered) | No (4.2 KB OOB) | Yes | 80 bits |
| **BN-P158 + 184-bit pHash** | **352** | 184 (transmitted) | Optional (+328) | No | ~78 bits |
| **BLS12-381 + 96-bit pHash** | **488** | 96 (transmitted) | Optional (+776) | No | ~117-120 bits |
| **UOV-100** | **504** | 184 (recovered) | No (8.1 KB OOB) | Yes | 100 bits |
| **BLS12-381 + 144-bit pHash** | **536** | 144 (transmitted) | Optional (+776) | No | ~117-120 bits |
| **BLS12-381 + 184-bit pHash** | **576** | 184 (transmitted) | Optional (+776) | No | ~117-120 bits |
| **BN-P158 + 96-bit pHash + PK** | **592** | 96 (transmitted) | **Yes** | No | ~78 bits |

Key observations:
- **BN-P158 without PK** produces the smallest payloads of any scheme (264-352 bits)
- **BN-P158 with PK in-band** (496 bits fixed overhead + pHash) fits within ~600-700 bits
- **BLS12-381 without PK** is comparable to UOV-100 in size (~488-576 bits)
- **UOV** wins on message recovery (no need to transmit pHash) and post-quantum security
- **BLS** wins on public key size (41-97 bytes vs 4-50 KB for UOV) and enables optional in-band PK

## Building

### Prerequisites

- C compiler (gcc or clang)
- CMake (for building RELIC)
- GMP library (`libgmp-dev`)

On Debian/Ubuntu:
```bash
sudo apt install build-essential cmake libgmp-dev
```

### Build and run

```bash
# BN-158 (~78-bit security, 168-bit signatures)
make stego_demo CURVE=BN-158
./stego_demo

# BLS12-381 (~117-120 bit security, 392-bit signatures)
make stego_demo CURVE=BLS12-381
./stego_demo

# Build RELIC for both curves
make build-relic
```

### Make targets

| Target | Description |
|---|---|
| `make stego_demo` | Build the stego channel demo (default: BN-158) |
| `make stego_demo CURVE=BLS12-381` | Build for BLS12-381 |
| `make stego_demo CURVE=BN-158` | Build for BN-158 |
| `make test` | Build and run the demo |
| `make build-relic` | Build RELIC for both curves |
| `make clean` | Remove all build artifacts |

## Threat Model

- **Attacker**: Active eavesdropper; adaptive chosen-message attacks possible
- **Channel**: Noisy steganographic channel with bit flips and erasures
- **Goal**: Authenticate a perceptual hash digest through the stego channel
- **Public key**: Exchanged out-of-band OR appended to payload (in-band)
- **Quantum security**: **None** -- BLS is broken by Shor's algorithm
- **One image = one message**

## Project Structure

```
BLS/
├── README.md           # This file
├── Makefile            # Build system (CURVE=BN-158 or BLS12-381)
├── bls_stego.h         # Wrapper API: keygen, sign, verify, payload assembly
├── bls_stego.c         # Implementation using RELIC's cp_bls_* API
├── stego_demo.c        # Stego channel demo (mirrors UOV/pqov/unit_tests/stego_demo.c)
└── relic/              # RELIC toolkit (git submodule)
```

## Design Notes

### Min-signature mode

This implementation uses **min-signature mode**: the BLS signature is a compressed G1 point (smaller), and the public key is a compressed G2 point (larger). This minimizes the bandwidth through the stego channel at the cost of a larger public key.

The alternative (min-pubkey mode) would put the signature in G2 and the public key in G1, roughly doubling the signature size. Since the public key can be exchanged out-of-band or at low cost, min-signature mode is preferred for this application.

### No message recovery

Unlike UOV, BLS signatures provide **no message recovery**. The verifier checks `e(sig, g2) == e(H(pHash), pk)` but cannot recover `pHash` from the signature alone. The pHash must be transmitted explicitly as part of the payload.

This means the eavesdropper can see the pHash in cleartext (it's in the payload), whereas with UOV the hash is only recoverable with the public key. This may or may not matter depending on the application.

### Salt

BLS signatures are inherently randomized through the hash-to-curve step (the mapping `H(message) -> G1` is deterministic, but different messages map to different points). Signing the same pHash twice with the same key produces the **same signature** (BLS is deterministic).

If distinct signatures per image are required (even for the same pHash), a salt or image-specific nonce should be prepended to the pHash before signing: `BLS_sign(sk, salt || pHash)`. The salt would then need to be included in the payload.

## References

- Boneh, Lynn, Shacham, "Short Signatures from the Weil Pairing" (Asiacrypt 2001)
- Barreto, Naehrig, "Pairing-Friendly Elliptic Curves of Prime Order" (SAC 2005)
- Kim, Barbulescu, "Extended Tower Number Field Sieve" (CRYPTO 2016)
- Barbulescu, Duquesne, "Updating key size estimations for pairings" (JoC 2018)
- RELIC toolkit: https://github.com/relic-toolkit/relic

## License

The BLS wrapper code (bls_stego.h, bls_stego.c, stego_demo.c) is licensed under Apache-2.0.
The RELIC toolkit is dual-licensed under Apache-2.0 or LGPL-2.1+.
