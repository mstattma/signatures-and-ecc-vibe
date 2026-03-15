# Perceptual Hash Considerations

This document collects the **scheme-independent** perceptual hash and fuzzy-signature considerations used across the repository.

It applies to all flows where the signed or recovered value is a perceptual hash (or a commitment derived from one), regardless of whether the underlying signature scheme is UOV, BLS, or a future scheme.

## Why fuzzy signatures?

Traditional digital signatures are binary:

- the message is exactly the same -> valid
- any bit changes -> invalid

That model is a poor fit for image and video authentication through a stego channel, because the media is often modified by benign processing steps:

- stego embedding changes pixels slightly
- JPEG recompression changes coefficients
- resizing changes sampling
- color transforms and format conversion alter raw bytes
- transcoding and frame extraction alter visual data without changing semantics

If we signed a standard cryptographic hash of the image bytes, even harmless processing would invalidate the signature.

## Fuzzy signature model

Instead, the sender computes a **perceptual hash** of the media and signs that.

At verification time:

1. the receiver recovers or retrieves the sender-side perceptual hash
2. the receiver computes their own perceptual hash from the received media
3. the two hashes are compared with a similarity metric

This yields an **authenticity score** rather than a strict yes/no result.

## Generic flow

```text
Sender:   media -> pHash(media) -> sign(pHash) -> embed/transmit signature

Receiver: media' -> extract signature -> recover/retrieve sender pHash
          media' -> pHash(media')
          compare(sender_pHash, receiver_pHash) -> authenticity score
```

## Authenticity scoring

The exact scoring depends on the perceptual hash algorithm.

Common metrics:

- **Hamming distance** for binary hashes (pHash, dHash, aHash)
- **Normalized similarity**: `1 - distance / total_bits`
- **Thresholding**: e.g. `>= 90% similar` = authentic, `70-90%` = modified but related, `< 70%` = suspicious

Typical interpretation:

| Scenario | Cryptographic hash | Perceptual hash / fuzzy signature |
|---|---|---|
| JPEG recompression | Invalid | High similarity, likely authentic |
| Mild resize/crop | Invalid | Moderate to high similarity |
| Annotation / watermark overlay | Invalid | Reduced but still meaningful similarity |
| Completely different image | Invalid | Low similarity |
| Substantial manipulation | Invalid | Depends on visual impact; usually detectable |

## Perceptual hash selection

The hash algorithm determines what kinds of transformations remain detectable.

| Perceptual hash | Typical output size | Main strengths | Main weaknesses |
|---|---|---|---|
| pHash (DCT-based) | 8 bytes | Robust to compression, resize | Less robust to aggressive local edits |
| dHash | 8 bytes | Fast, good for near-duplicates | Simpler, lower discriminative power |
| aHash | 8 bytes | Very simple | Least robust |
| pHash+ / extended pHash | 16-32 bytes | More discriminative | Larger output |
| BlockHash | 16-32 bytes | Better for some local changes | Larger output |
| [DinoHash](https://github.com/proteus-photos/dinohash-perceptual-hash) | 12 bytes | Strong robustness, modern neural approach | ML dependency |
| Other neural perceptual hashes | varies | Often robust to modern transformations | May be heavier to compute/deploy |

## Size constraints by scheme

Different signature schemes impose different practical limits on the usable perceptual hash size.

### UOV

UOV with salt-in-digest recovers only a limited number of hash bytes:

| Variant | Max pHash bytes | Max pHash bits |
|---|---|---|
| UOV-80 | 18 | 144 |
| UOV-100 | 23 | 184 |

If the pHash exceeds that limit, it must either be rejected or truncated. In this repository, oversized UOV pHashes are **rejected**, not silently truncated.

### BLS

BLS does not recover the pHash from the signature, so the pHash can be:

- embedded in the payload, or
- omitted from the payload and retrieved from the ledger

This means BLS does **not** impose a practical pHash size limit in the same way UOV does.

## Truncation trade-offs

Truncating a perceptual hash reduces discriminative power.

- Shorter hashes use less payload space
- Longer hashes separate near-duplicates from unrelated images better

In this repository:

- UOV-80 cannot carry a full 184-bit pHash
- UOV-100 can carry up to 184 bits
- BLS can support larger pHashes because it does not rely on message recovery

## Scheme comparison relevance

Perceptual hash choice is one of the main reasons different schemes remain useful:

- **UOV-80**: most bandwidth-efficient post-quantum option, but max 144-bit pHash
- **UOV-100**: supports 184-bit pHash, still post-quantum, but larger payload
- **BLS-BN158**: smallest non-PQ payload when pHash is not embedded; pHash can live in the ledger
- **BLS12-381**: stronger classical security than BN158, still flexible on pHash size

## Practical recommendation

For current image use:

- if you need **post-quantum** and can accept a **144-bit pHash**, use **UOV-80**
- if you need **post-quantum** and require a **184-bit pHash**, use **UOV-100**
- if you want the **smallest stego payload** and can load pHash from the ledger, use **BLS-BN158** with `embed_phash=0`
- if you want stronger classical security than BN158, use **BLS12-381**

For video, the same considerations apply to each interval hash or to the Merkle-root commitment derived from them.
