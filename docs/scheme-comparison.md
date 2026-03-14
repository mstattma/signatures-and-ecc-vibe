# Signature Scheme Comparison for Steganographic Channels

This document summarizes the research into compact digital signature schemes suitable for a bandwidth-constrained steganographic channel. The goal is to authenticate a perceptual hash digest transmitted through a noisy stego channel embedded in images.

## Channel Constraints and Threat Model

- **Usable payload**: ~500 bits (after outer ECC), up to ~1000 bits stretch goal
- **One image = one message** (but the same perceptual hash may apply to multiple images)
- **Threat model**: Active eavesdropper; adaptive chosen-message attacks are possible
- **Public key exchange**: Out-of-band (~1 MB acceptable)
- **Channel noise**: Bit flips and erasures (BER to be measured)
- **Salt**: 2-byte (16-bit) random salt embedded *inside* the recovered digest (salt-in-digest technique), ensuring distinct signatures across images sharing the same perceptual hash and protecting against adaptive chosen-message attacks, at zero bandwidth cost

## Comprehensive Scheme Survey

All 14 NIST Additional Digital Signatures Round 2 candidates were evaluated, along with BLS (classical), GeMSS (NIST Round 3 alternate), and CFS (academic). Only schemes with signatures <=1000 bits are viable. The full survey results at NIST Security Level I (~128-bit classical):

| Scheme | Family | Sig (bytes) | Sig (bits) | PK (bytes) | Quantum-safe | NIST Status |
|---|---|---|---|---|---|---|
| CFS (m=16,t=9) | Code-based | 18 | 144 | ~1 MB | Broken (57-bit) | Academic |
| GeMSS-128 | Multivariate (HFEv-) | 33 | 264 | 352 KB | **Broken** (72-bit) | R3 alt, eliminated |
| UOV (classic) | Multivariate (OV) | 96 | 768 | 278 KB | Yes | R2 onramp |
| SNOVA (37,17,16,2) | Multivariate | 124 | 992 | 9.8 KB | Yes | R2 onramp |
| SQIsign L1 | Isogeny | 148 | 1,184 | 65 B | Yes | R2 onramp |
| QR-UOV | Multivariate | ~162 | ~1,296 | ~24 KB | Yes | R2 onramp |
| SNOVA (25,8,16,3) | Multivariate | 165 | 1,320 | 2.3 KB | Yes | R2 onramp |
| MAYO_two | Multivariate | 186 | 1,488 | 4.9 KB | Yes | R2 onramp |
| SNOVA (24,5,16,4) | Multivariate | 248 | 1,984 | 1.0 KB | Yes | R2 onramp |
| MAYO_one | Multivariate | 454 | 3,632 | 1.4 KB | Yes | R2 onramp |
| Hawk-512 | Lattice | 555 | 4,440 | 1.0 KB | Yes | R2 onramp |
| MQOM (gf2, short) | MPC-in-head | 2,820 | 22,560 | 52 B | Yes | R2 onramp |
| Mirath-1b (short) | MPC-in-head | 2,990 | 23,920 | 57 B | Yes | R2 onramp |
| RYDE-1 (short) | MPC-in-head | 3,115 | 24,920 | 69 B | Yes | R2 onramp |
| CROSS-RSDPG-128 | Code-based | ~3,100 | ~24,800 | ~55 B | Yes | R2 onramp |
| PERK-1 (short) | MPC-in-head | 3,563 | 28,504 | 102 B | Yes | R2 onramp |
| SDitH-L1 (short) | MPC-in-head | 3,705 | 29,640 | 70 B | Yes | R2 onramp |
| FAEST (EM-128s) | Symmetric | 3,906 | 31,248 | 32 B | Yes | R2 onramp |
| FAEST (128s) | Symmetric | 4,506 | 36,048 | 32 B | Yes | R2 onramp |
| LESS L1 | Code-based | ~5,072 | ~40,576 | ~100 B | Yes | R2 onramp |

**Conclusion**: Only UOV-family multivariate schemes (UOV, SNOVA, MAYO) and SQIsign produce signatures compact enough for the ~500-1000 bit stego channel. MPC-in-head and code-based schemes have tiny public keys but signatures far too large (20+ KB).

## Shortlist for Stego Channel

The table below includes only schemes that could plausibly fit within the channel budget, plus custom reduced-security UOV parameter sets optimized for minimal signature size. UOV signature sizes use the **salt-in-digest** technique (see below), where the salt is embedded inside the recovered digest at zero bandwidth cost.

| Scheme | Sig bits | PK compressed | Sig + PK (bits) | Digest recovery? | Classical security (bits) | Quantum security (bits) | Status | Risk |
|---|---|---|---|---|---|---|---|---|
| ~~GeMSS-128~~ | ~~264~~ | ~~N/A~~ | ~~N/A~~ | ~~Partial~~ | ~~72 (broken)~~ | ~~N/A~~ | **BROKEN** | Fatal: Support-Minors MinRank key recovery |
| **BLS (MNT-159)** | ~160 | ~20 B | **~320** | No | ~60-70 | 0 (Shor) | Non-standard | Quantum-broken; very low classical security |
| **BLS12-381** | 384 | 48 B | **768** | No | ~117-120 | 0 (Shor) | Standardized | Quantum-broken; ~8-11 bits below 128 target |
| **UOV-80** (custom) | **400** | 4.2 KB | **34,128** | **Yes** (144 bits hash + 16 bits salt) | 80 | ~40-48 (est.) | Custom params | Non-standard; 80-bit may be thin for active attacker |
| **UOV-100** (custom) | **504** | 8.1 KB | **65,312** | **Yes** (184 bits hash + 16 bits salt) | 100 | ~50-60 (est.) | Custom params | Non-standard; good security margin |
| **UOV** (NIST L1) | 768 [^1] | ~43-66 KB | **~345K-529K** | **Yes** (~256 bits) | 128 | ~64-96 (uncertain) | NIST R2 | Low risk; well-studied 25+ year history |
| **SNOVA** (37,17,2) | 992 | ~5 KB (est.) | **~41,000** | Yes | 153 | ~equivalent | NIST R2 | Medium: l=2 parameter set under cryptanalytic scrutiny |
| **SQIsign** L1 | 1,184 | 65 B | **1,704** | No | 128 | 128 | NIST R2 | Medium: novel isogeny math; complex implementation |

The "Sig + PK" column shows the total bits if the public key (compressed where available) is transmitted alongside the signature rather than exchanged out-of-band. BLS and SQIsign have dramatically smaller totals due to their tiny public keys. For UOV, out-of-band PK exchange is essential.

[^1]: UOV NIST L1 uses the standard reference implementation with salt appended to the signature (768 + salt bits). With the salt-in-digest modification applied, the signature would be 768 bits (96 bytes for GF(16) variant) or 896 bits (112 bytes for GF(256) variant) with no appended salt.

### Custom UOV Parameters

| Parameter | UOV-80 | UOV-100 |
|---|---|---|
| Field | GF(256) | GF(256) |
| Vinegar variables (v) | 30 | 38 |
| Oil variables (o) | 20 | 25 |
| Total variables (n = v + o) | 50 | 63 |
| **Signature (= w only)** | **50 bytes = 400 bits** | **63 bytes = 504 bits** |
| Recovered digest P(w) | 20 bytes = 160 bits | 25 bytes = 200 bits |
| Salt (inside digest) | 2 bytes = 16 bits (default) | 2 bytes = 16 bits (default) |
| Effective hash (digest - salt) | 18 bytes = 144 bits | 23 bytes = 184 bits |
| Collision resistance | 2^72 (with 2B salt) / 2^80 (no salt) | 2^92 (with 2B salt) / 2^100 (no salt) |
| Preprocessor define | `_OV256_50_20` | `_OV256_63_25` |

These parameters were selected using `uov_security_estimator.py` to minimize signature size while achieving the target security level against all known attacks (direct algebraic, Kipnis-Shamir, intersection/reconciliation, and collision forgery). Without salt, the collision forgery bound (birthday attack on `q^(o/2) = 256^(o/2) = 2^(4o)`) is the bottleneck. With the default 2-byte salt, the effective hash is reduced by 2 bytes, lowering collision resistance by 8 bits.

### Salt-in-Digest Technique

The standard UOV signature format appends the salt to the signature vector: `signature = w || salt`. This costs `_SALT_BYTE` bytes of channel bandwidth per signature.

Our implementation uses **salt-in-digest**: the salt is embedded inside the recovered digest `P(w)` rather than appended to the signature. The digest layout becomes:

```
P(w) = H(message)[0 .. effective-1] || salt[0 .. _SALT_BYTE-1]
       \_________________________/     \____________________/
        truncated hash of message       random nonce (embedded)
        (verified by receiver)          (recovered, not transmitted)
```

**How it works:**

1. **Signing**: Compute `H(message)` truncated to `o - _SALT_BYTE` bytes, append `_SALT_BYTE` bytes of random salt, forming the target `y`. Find `w` such that `P(w) = y`. The signature is just `w`.
2. **Verification**: Evaluate `P(w)` to recover the full digest. Compare the first `o - _SALT_BYTE` bytes against `H(message)` truncated to the same length. The last `_SALT_BYTE` bytes are the salt (recovered but not checked).

**Benefits:**
- Signature size is always `n` bytes regardless of salt configuration
- Salt provides multi-target attack resistance and signature uniqueness at **zero bandwidth cost**
- Trade-off: each byte of salt reduces effective hash (and collision resistance) by 8 bits

**Signature sizes at different salt configurations** (signature size is always the same):

| Salt | UOV-80 sig | UOV-80 collision bits | UOV-100 sig | UOV-100 collision bits |
|------|-----------|----------------------|------------|---------------------|
| 0 B | 400 bits | 80 | 504 bits | 100 |
| 2 B (default) | 400 bits | 72 | 504 bits | 92 |
| 4 B | 400 bits | 64 | 504 bits | 84 |

## Key Findings

### GeMSS is broken

GeMSS (HFEv- construction) was eliminated from NIST standardization after a catastrophic key-recovery attack:

- **Tao, Petzoldt, Ding (CRYPTO 2021)**: New MinRank modeling polynomial in vinegar variables
- **Baena, Briaud, Cabarcas, Perlner, Smith-Tone, Verbel (CRYPTO 2022)**: Improved Support-Minors attack reduces GeMSS-128 security to **2^72** (target was 2^128)

The attack fundamentally undermines the vinegar and minus modifiers that HFEv- relies on for security. All repair strategies (increasing parameters, adding projection) result in impractical signing times (~2^14x slowdown). NIST's assessment: "the vinegar and minus modifiers fail to provide any substantial security benefit in an HFEv- construction."

**This attack does NOT affect UOV** -- UOV is a fundamentally different construction (no big field, no HFE polynomial).

### BLS security is lower than the naive estimate

BLS12-381 was designed to target 128-bit classical security, but improved Number Field Sieve (NFS) algorithms for the pairing target group GT reduce this:

- **Kim-Barbulescu (2016)**: Tower NFS variant more efficient for extension fields GF(p^k)
- **Barbulescu-Duquesne (2017)**: Updated key size estimates confirm BLS12-381's 4572-bit target field (381 x 12) falls short of 128-bit NFS security
- **NCC Group report**: Actual security estimated at **~117-120 bits**

For BLS on MNT-159 curves (embedding degree 6), the 960-bit target field GF(p^6) provides only **~60-70 bits** of tower NFS security -- firmly in the "breakable by well-funded attacker" range.

Both BLS variants are completely broken by Shor's algorithm on a quantum computer.

### UOV has inherent message recovery

UOV signatures have a message recovery property that most descriptions overlook:

- **Signing**: Find signature vector `w` such that `P(w) = target_digest`
- **Verification**: Evaluate `P(w)` on the signature to **recover** the target digest, then verify it
- **Key insight**: The verifier does not need the digest to be transmitted -- it is recovered from the signature

This means only the signature vector `w` travels through the stego channel. The recovered digest size equals `o * 8` bits (for GF(256)). Combined with the salt-in-digest technique (see above), the salt is also recovered for free, so the signature is just `w` with nothing appended.

Other multivariate schemes (SNOVA, MAYO, QR-UOV) also support message recovery via the same mechanism: the verifier evaluates the public map on the signature. Fiat-Shamir-based schemes (SQIsign, Hawk, FAEST, etc.) and pairing-based schemes (BLS) do **not** support message recovery.

### Truncating signatures is not possible

Digital signatures cannot be truncated without destroying them. The signature is a mathematical object (solution vector, curve point, isogeny path) that must be complete for verification. Reducing signature size requires choosing different parameters at a lower security level, not truncating the output.

### Salt considerations

The upstream UOV reference implementation appends a 16-byte (128-bit) salt to the signature, costing 128 bits of channel bandwidth. Our implementation uses two optimizations:

1. **Reduced salt size**: 2 bytes (16 bits) is sufficient for this application. The salt ensures distinct signatures when the same perceptual hash is signed for multiple images and provides protection under adaptive chosen-message attacks (EUF-CMA). 2 bytes provides 2^16 distinct salt values per message.

2. **Salt-in-digest**: The salt is embedded inside the recovered digest `P(w)` rather than appended to the signature. This eliminates the bandwidth cost entirely -- the signature is just `w` (n bytes) regardless of salt size. The trade-off is reduced collision resistance: each byte of salt reduces the effective hash by 8 bits.

The combined effect: UOV-80 achieves **400-bit signatures** (vs. 528 bits with the naive 4-byte appended salt, or 528 bits with the upstream 16-byte salt), while still providing salt-based protection against adaptive attacks.

## Stego Channel Architecture

```
Sender:
  perceptual_hash -----> UOV sign (sk, hash)
                              |
                              v
                         [w only]              (400 or 504 bits, salt is inside P(w))
                              |
                              v
                         [outer RS-ECC]        (adds redundancy for bit flips/erasures)
                              |
                              v
                         [interleaver]          (spreads burst errors)
                              |
                              v
                         [stego embed]          (hide in cover image)

Receiver:
  stego image -----> [stego extract]
                         |
                         v
                    [deinterleave]
                         |
                         v
                    [RS-ECC decode]
                         |
                         v
                    [w]
                         |
                         v
                    P(w) = H(msg)[trunc] || salt   (hash + salt recovered from signature)
                    verify: H(msg)[trunc] == first bytes of P(w)
```

## Recommendations

1. **UOV-100 at 504 bits** is the recommended primary option: 100-bit security provides adequate margin against active attackers, the signature fits within the ~500-bit budget (tight but feasible with efficient outer ECC), message recovery eliminates the need to transmit the digest separately, and the salt-in-digest technique provides adaptive chosen-message security at zero bandwidth cost. Collision resistance is 92 bits (with default 2-byte salt).

2. **UOV-80 at 400 bits** is the fallback if ECC overhead is too high for UOV-100: leaves ~100 bits for outer ECC within a 500-bit budget. However, 80-bit security is marginal against well-resourced active attackers. Collision resistance is 72 bits (with default 2-byte salt).

3. **BLS12-381 at 384 bits** is viable if post-quantum security is not required and the digest can be transmitted separately or is already known to the receiver. Compact, standardized, well-understood. No message recovery.

4. **UOV NIST L1 at 768 bits** (with salt-in-digest) is the conservative choice if the channel can accommodate ~1000 bits: standardized parameters, 128-bit security, 25+ year cryptanalytic history.

### Next steps

1. **Measure the stego channel BER/erasure rate** to determine outer ECC requirements
2. **Design outer Reed-Solomon ECC** layer sized for measured channel conditions
3. **Implement interleaver** for burst error protection
4. **Prototype the full pipeline**: sign -> ECC encode -> interleave -> stego embed -> extract -> deinterleave -> ECC decode -> verify

## References

- Baena et al., "Improving Support-Minors rank attacks: applications to GeMSS and Rainbow" (CRYPTO 2022, ePrint 2021/1677)
- Barbulescu & Duquesne, "Updating key size estimations for pairings" (JoC 2018, ePrint 2017/334)
- Beullens, "Improved Cryptanalysis of UOV and Rainbow" (Eurocrypt 2021)
- Beullens et al., "Oil and Vinegar: Modern Parameters and Implementations" (2023)
- NIST IR 8413-upd1, "Status Report on the Third Round of the NIST Post-Quantum Cryptography Standardization Process" (2022)
- NCC Group, "Zcash Cryptography and Code Review" (2019) -- BLS12-381 security analysis
- NIST Additional Digital Signatures Round 2: https://csrc.nist.gov/Projects/pqc-dig-sig/round-2-additional-signatures
