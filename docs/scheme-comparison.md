# Signature Scheme Comparison for Steganographic Channels

This document summarizes the research into compact digital signature schemes suitable for a bandwidth-constrained steganographic channel. The goal is to authenticate a perceptual hash digest transmitted through a noisy stego channel embedded in images.

## Channel Constraints and Threat Model

- **Usable payload**: ~500 bits (after outer ECC), up to ~1000 bits stretch goal
- **One image = one message** (but the same perceptual hash may apply to multiple images)
- **Threat model**: Active eavesdropper; adaptive chosen-message attacks are possible
- **Public key exchange**: Out-of-band (~1 MB acceptable)
- **Channel noise**: Bit flips and erasures (BER to be measured)
- **Salt**: 4-byte (32-bit) random salt included in signatures to ensure distinct signatures across images sharing the same perceptual hash, and to protect against adaptive chosen-message attacks

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

The table below includes only schemes that could plausibly fit within the channel budget, plus custom reduced-security UOV parameter sets optimized for minimal signature size. Signature sizes include a 4-byte (32-bit) salt where applicable.

| Scheme | Sig bits | PK classic | PK compressed | Digest recovery? | Classical security (bits) | Quantum security (bits) | Status | Risk |
|---|---|---|---|---|---|---|---|---|
| ~~GeMSS-128~~ | ~~264~~ | ~~352 KB~~ | ~~N/A~~ | ~~Partial~~ | ~~72 (broken)~~ | ~~N/A~~ | **BROKEN** | Fatal: Support-Minors MinRank key recovery |
| **BLS (MNT-159)** | ~160 | ~40 B | ~20 B | No | ~60-70 | 0 (Shor) | Non-standard | Quantum-broken; very low classical security |
| **BLS12-381** | 384 | 96 B | 48 B | No | ~117-120 | 0 (Shor) | Standardized | Quantum-broken; ~8-11 bits below 128 target |
| **UOV-80** (custom) | **432** [^1] | 25.5 KB | 4.2 KB | **Yes** (160 bits) | 80 | ~40-48 (est.) | Custom params | Non-standard; 80-bit may be thin for active attacker |
| **UOV-100** (custom) | **536** [^1] | 50.4 KB | 8.1 KB | **Yes** (200 bits) | 100 | ~50-60 (est.) | Custom params | Non-standard; good security margin |
| **UOV** (NIST L1) | 800 [^1] | 278 KB | ~43-66 KB | **Yes** (~256 bits) | 128 | ~64-96 (uncertain) | NIST R2 | Low risk; well-studied 25+ year history |
| **SNOVA** (37,17,2) | 992 | 9.8 KB | ~5 KB (est.) | Yes | 153 | ~equivalent | NIST R2 | Medium: l=2 parameter set under cryptanalytic scrutiny |
| **SQIsign** L1 | 1,184 | 65 B | 65 B | No | 128 | 128 | NIST R2 | Medium: novel isogeny math; complex implementation |

[^1]: Signature sizes include a 4-byte (32-bit) salt appended to the raw signature vector. Without salt: UOV-80 = 400 bits (50 bytes), UOV-100 = 504 bits (63 bytes), UOV NIST L1 = 768 bits (96 bytes). The salt ensures distinct signatures when the same perceptual hash is signed for multiple images and provides protection against adaptive chosen-message attacks. The reference implementation default is 16-byte salt; 4 bytes is sufficient for this application given the non-repeated-key-message pairs.

### Custom UOV Parameters

| Parameter | UOV-80 | UOV-100 |
|---|---|---|
| Field | GF(256) | GF(256) |
| Vinegar variables (v) | 30 | 38 |
| Oil variables (o) | 20 | 25 |
| Total variables (n = v + o) | 50 | 63 |
| Raw signature (n bytes) | 400 bits | 504 bits |
| Salt | 32 bits (4 bytes) | 32 bits (4 bytes) |
| **Total signature** | **432 bits** | **536 bits** |
| Recovered digest (o bytes) | 160 bits | 200 bits |
| Collision resistance of recovered digest | 2^80 | 2^100 |
| Preprocessor define | `_OV256_50_20` | `_OV256_63_25` |

These parameters were selected using `uov_security_estimator.py` to minimize signature size while achieving the target security level against all known attacks (direct algebraic, Kipnis-Shamir, intersection/reconciliation, and collision forgery). The collision forgery bound (birthday attack on `q^(o/2) = 256^(o/2) = 2^(4o)`) is the bottleneck in both cases.

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

- **Signing**: Compute `h = H(message || salt)`, find signature vector `s` such that `P(s) = h`
- **Verification**: Evaluate `P(s)` on the signature to **recover** `h`, then check `h == H(message || salt)`
- **Key insight**: The verifier does not need `h` to be transmitted -- it is recovered from the signature

This means only the signature (+ salt) travels through the stego channel. The recovered digest size equals `o * 8` bits (for GF(256)), providing collision resistance of `2^(4o)` bits.

Other multivariate schemes (SNOVA, MAYO, QR-UOV) also support message recovery via the same mechanism: the verifier evaluates the public map on the signature. Fiat-Shamir-based schemes (SQIsign, Hawk, FAEST, etc.) and pairing-based schemes (BLS) do **not** support message recovery.

### Truncating signatures is not possible

Digital signatures cannot be truncated without destroying them. The signature is a mathematical object (solution vector, curve point, isogeny path) that must be complete for verification. Reducing signature size requires choosing different parameters at a lower security level, not truncating the output.

### Salt considerations

The UOV reference implementation uses a 16-byte (128-bit) salt by default. For this application, a 4-byte (32-bit) salt is sufficient because:

- The salt's primary purpose is to ensure distinct signatures when the same perceptual hash is signed for multiple images, and to provide security under adaptive chosen-message attacks
- 4 bytes provides 2^32 distinct salt values, adequate for the expected number of signatures per key
- Reducing from 16 to 4 bytes saves 96 bits of channel bandwidth
- The `_SALT_BYTE` constant in `params.h` can be adjusted accordingly

## Stego Channel Architecture

```
Sender:
  perceptual_hash -----> UOV sign (sk, hash, salt)
                              |
                              v
                         [signature || salt]   (432 or 536 bits)
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
                    [signature || salt]
                         |
                         v
                    P(s) = recovered hash       (digest recovered from signature)
                    verify: P(s) == H(msg||salt)
```

## Recommendations

1. **UOV-100 at 536 bits** is the recommended primary option: 100-bit security provides adequate margin against active attackers, the signature fits within the ~500-bit budget (tight but feasible with efficient outer ECC), and message recovery eliminates the need to transmit the digest separately.

2. **UOV-80 at 432 bits** is the fallback if ECC overhead is too high for UOV-100: leaves ~68 bits for outer ECC within a 500-bit budget. However, 80-bit security is marginal against well-resourced active attackers.

3. **BLS12-381 at 384 bits** is viable if post-quantum security is not required and the digest can be transmitted separately or is already known to the receiver. Compact, standardized, well-understood.

4. **UOV NIST L1 at 800 bits** is the conservative choice if the channel can accommodate ~1000 bits: standardized parameters, 128-bit security, 25+ year cryptanalytic history.

### Next steps

1. **Measure the stego channel BER/erasure rate** to determine outer ECC requirements
2. **Reduce `_SALT_BYTE` to 4** in the implementation
3. **Design outer Reed-Solomon ECC** layer sized for measured channel conditions
4. **Implement interleaver** for burst error protection
5. **Prototype the full pipeline**: sign -> ECC encode -> interleave -> stego embed -> extract -> deinterleave -> ECC decode -> verify

## References

- Baena et al., "Improving Support-Minors rank attacks: applications to GeMSS and Rainbow" (CRYPTO 2022, ePrint 2021/1677)
- Barbulescu & Duquesne, "Updating key size estimations for pairings" (JoC 2018, ePrint 2017/334)
- Beullens, "Improved Cryptanalysis of UOV and Rainbow" (Eurocrypt 2021)
- Beullens et al., "Oil and Vinegar: Modern Parameters and Implementations" (2023)
- NIST IR 8413-upd1, "Status Report on the Third Round of the NIST Post-Quantum Cryptography Standardization Process" (2022)
- NCC Group, "Zcash Cryptography and Code Review" (2019) -- BLS12-381 security analysis
- NIST Additional Digital Signatures Round 2: https://csrc.nist.gov/Projects/pqc-dig-sig/round-2-additional-signatures
