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

## Perceptual Hash Catalog

The hash algorithm determines what kinds of transformations remain detectable. This section catalogs all algorithms considered.

### Classical / frequency-domain hashes

| Hash | Output | Method | Strengths | Weaknesses | Library |
|---|---|---|---|---|---|
| **aHash** (average) | 8 B (64 bits) | Mean luminance threshold | Very fast, simple | Least discriminative | imagehash, OpenCV |
| **dHash** (difference) | 8 B (64 bits) | Adjacent pixel gradient direction | Fast, good near-duplicate detection | Sensitive to flips, low discriminative power | imagehash, OpenCV |
| **pHash** (perceptual, DCT) | 8 B (64 bits) | Low-frequency DCT coefficients | Robust to compression, resize, gamma | Less robust to local edits, crops | imagehash, OpenCV, pHash.org |
| **wHash** (wavelet) | 8 B (64 bits) | Haar wavelet decomposition | Similar to pHash with different frequency emphasis | Similar limitations | imagehash |
| **Marr-Hildreth** | 72 B (576 bits) | Marr-Hildreth edge operator at multiple scales | More discriminative; scale-robust | Larger output; slower | OpenCV img_hash |
| **Radial Variance** | 40 B (320 bits) | Radon transform / angular projection | Rotation-invariant | Large output; slower | OpenCV img_hash |

### Block / spatial-structure hashes

| Hash | Output | Method | Strengths | Weaknesses | Library |
|---|---|---|---|---|---|
| **BlockHash** | 16-32 B | Block mean thresholding | Better for partial modifications | Larger output | blockhash, imagehash |
| **Block Mean (OpenCV)** | 32-121 B | Block mean, two modes (sparse/dense) | Fast; configurable granularity | Mode 1 output large (121 bytes) | OpenCV img_hash |
| **Color Moment** | 42 × 8 B = 336 B | Statistical moments per color channel per block | Rotation-resistant (unique among classical hashes) | Very large output; floating-point | OpenCV img_hash |
| **Crop-resistant hash** | variable | Segmented image regions hashed independently | Survives cropping | Larger, more complex comparison | imagehash |

### Gradient / canonicalized variants

| Hash | Output | Method | Strengths | Weaknesses | Source |
|---|---|---|---|---|---|
| **dHash-FS64** | 8 B (64 bits) | Canonicalized dHash with flip-safe normalization | Perfect flip invariance; strong brightness/blur robustness | Weaker discrimination than pHash; crop/gamma/noise sensitivity remains | `perceptual-fuzzy-hash-test-vibe` |
| **dHash-FS128** | 16 B (128 bits) | Higher-resolution flip-safe dHash | Stronger detail than FS64 while preserving flip invariance | Larger output; same general failure modes | `perceptual-fuzzy-hash-test-vibe` |

### Neural / learned hashes

| Hash | Output | Method | Strengths | Weaknesses | Library |
|---|---|---|---|---|---|
| **[DinoHash](https://github.com/proteus-photos/dinohash-perceptual-hash)** | 12 B (96 bits) | DINOv2 ViT-S/14 with adversarial fine-tuning | SOTA robustness to crops, compression, adversarial attacks; 12% better bit accuracy than NeuralHash | Requires ONNX runtime or PyTorch; model ~85 MB | dinohash (Python, Node.js) |
| **NeuralHash** (Apple) | 12 B (96 bits) | CNN-based learned perceptual hash | Designed for CSAM detection at scale | Cancelled by Apple; known collision/evasion attacks (Struppek et al. 2022) | Not publicly released |
| **PDQ** (Facebook/Meta) | 32 B (256 bits) | DCT-based with additional robustness engineering | Designed for scale; open-source; well-tested against social media transformations | Larger than pHash; not as robust as neural hashes | perception (Thorn) |
| **PhotoDNA** (Microsoft) | 144 B (1152 bits) | Proprietary gradient-based hash | Industry standard for CSAM; robust to many transforms | Proprietary; not publicly available; very large output | Microsoft (API only) |
| **SSCD** (Meta) | 64-512 B | Self-supervised contrastive descriptor | State-of-the-art copy detection; high discriminative power | Large output; requires PyTorch model inference | Meta research |
| **ISC** (Meta, Image Similarity Challenge) | 256 B | Learned global descriptor for copy detection | Excellent on the ISC benchmark; robust | Large output; GPU inference | Meta research |

### Audio perceptual hashes (for multimedia completeness)

| Hash | Output | Method | Notes |
|---|---|---|---|
| **Chromaprint** | 4 B per frame, variable total | Chroma-based audio fingerprint | Used by AcoustID/MusicBrainz; robust to transcoding |
| **SAMAF** | variable | Sequence-to-sequence autoencoder fingerprint | Deep-learning audio hash; better than Chromaprint for transformations |

## Collision Resistance: The Security Bottleneck

**The collision resistance of the pHash is the weakest link in the entire fuzzy-signature scheme.** The digital signature (UOV, BLS) provides 78-128 bits of security, but if the pHash has only 32 bits of collision resistance (e.g., a 64-bit hash with 50% tolerance), an attacker can forge images that produce the same signed hash far more easily than breaking the signature itself.

### Birthday-bound collision resistance

For a hash with `n` effective bits (after accounting for fuzzy tolerance), the birthday-bound collision resistance is approximately `2^(n/2)`:

The last column shows the total embedded payload if pHash, BLS-BN158 signature (168 bits), 1-byte salt (8 bits), and PK (328 bits) are all embedded in the stego channel: `pHash + 168 + 8 + 328 = pHash + 504`.

| Hash output | Bits (strict) | Collision (strict) | ~10% tol. effective | Collision (~10% tol.) | pHash ‖ BN158 sig ‖ 1B salt ‖ PK (bits) |
|---|---|---|---|---|---|
| 8 B = 64 bits | 64 | 2^32 | ~58 | 2^29 | 568 |
| 12 B = 96 bits | 96 | 2^48 | ~86 | 2^43 | 600 |
| 16 B = 128 bits | 128 | 2^64 | ~115 | 2^57 | 632 |
| 20 B = 160 bits | 160 | 2^80 | ~144 | 2^72 | 664 |
| 23 B = 184 bits | 184 | 2^92 | ~166 | 2^83 | 688 |
| 25 B = 200 bits | 200 | 2^100 | ~180 | 2^90 | 704 |
| 32 B = 256 bits | 256 | 2^128 | ~230 | 2^115 | 760 |

**The tolerance penalty is the critical concern.** When we allow fuzzy matching (accepting images as "authentic" even if some hash bits differ), the effective hash space shrinks dramatically. This is because an attacker doesn't need an exact collision — they just need to land within the tolerance ball.

The actual tolerance penalty depends on the hash algorithm and the similarity threshold. This needs empirical measurement for each algorithm we consider, which is planned future work.

### Why classical 64-bit hashes are dangerous

Most classical perceptual hashes (aHash, dHash, pHash, wHash) produce only 64 bits. Even with strict exact-match comparison, that's only 2^32 collision resistance — roughly 4 billion. With fuzzy tolerance, it drops to perhaps 2^25-2^29 (millions to hundreds of millions). An attacker with moderate computational resources could find collisions in minutes to hours.

**This is why we need either larger hashes, multiple hashes, or both.**

## Salt Reconsideration

The salt was originally introduced to ensure distinct signatures when the same pHash is signed for different images. However, the security picture changes when we consider multi-hash strategies:

### The salt costs pHash capacity in UOV

With the salt-in-digest architecture, each byte of salt steals one byte from the recoverable pHash:

| Variant | With 2B salt (current) | With 1B salt (proposed) | Without salt |
|---|---|---|---|
| UOV-80 | 18 B = 144 bits | **19 B = 152 bits** | 20 B = 160 bits |
| UOV-100 | 23 B = 184 bits | **24 B = 192 bits** | 25 B = 200 bits |

Even 1 byte of gained capacity is significant — it can accommodate an additional small hash component (e.g., 1 extra byte of colorHash = 8 bits, adding ~4 bits of independent collision resistance).

### Why we still need a salt (at least 1 byte)

The original motivation for removing the salt was that near-duplicate prohibition by the ledger could replace it. However, we still need it for two reasons:

**Reason 1: Bloom filter false positives.** The ledger's cross-chain duplicate detection uses a Bloom filter, which produces false positives. Bloom filter results are **deterministic** — the same input always produces the same result. Without a salt:

- A Bloom filter false positive on a pHash is a **dead end** — the pHash is computed from the image and cannot be changed.
- The signer would have to query the authoritative per-chain resolver to distinguish a false positive from a real duplicate. This works for the local chain, but for cross-chain detection the resolver only covers one chain — the off-chain indexer is needed for a definitive cross-chain answer.
- If the indexer is unavailable and the Bloom filter says "might exist", there is no way to proceed without a salt to retry.

**Reason 2: Intentional re-signing.** When the same perceptual hash legitimately applies to multiple different images (e.g., the same scene photographed twice, or the same pHash across different crops), the salt allows registering each one separately.

With even a 1-byte (8-bit) salt providing 256 possible values:

- A Bloom filter false positive on `(pHash, salt=0)` is resolved by retrying with `salt=1`, `salt=2`, etc.
- At <1% false positive rate, the expected number of retries is <1.01 (almost never needs even one retry).
- The probability of all 256 salt values producing false positives is ~(0.01)^256 ≈ 0.

**Note**: In the normal registration flow, Bloom false positives do NOT require salt retries — they are resolved by checking the authoritative per-chain resolver index. The salt retry path is a fallback for when the indexer is unavailable and the resolver can't provide a cross-chain answer. See the [Ethereum Ledger Proposal](ethereum-ledger-proposal.md#combined-cross-chain-dedup-flow) for the full three-layer dedup flow.

**1 byte of salt is sufficient.** It costs only 8 bits of pHash capacity (vs 16 bits for the current 2-byte salt) while providing retry capability for both Bloom FP fallback and intentional re-signing.

### BLS alternative salt placement

For BLS, there is an additional option: **move the salt outside the signed payload**. Instead of `sig = BLS_sign(sk, pHash || salt)`, use:

```text
sig = BLS_sign(sk, pHash)
payload = [sig] || [salt]        (salt appended after signature, not covered by sig)
```

Or for ledger duplicate detection purposes:

```text
sig = BLS_sign(sk, pHash)
ledger_dedup_key = H(pHash || salt)
```

This means:
- The signature covers **only the pHash** — the pHash can be **any length**
- The salt is still available for duplicate detection but doesn't constrain the signed data
- The salt doesn't need to be in the stego payload at all (it can live in the ledger record)

**This is NOT possible with UOV** because UOV's message recovery (`P(w)`) produces a fixed-size output — whatever bytes are in the digest are all the verifier can recover. If the salt is not in the digest, the verifier has no way to reconstruct the full signed message to verify against.

### Summary: salt options per scheme

| Scheme | Option A (current, 2B salt) | Option B (1B salt, recommended) | Option C (BLS: salt after sig) |
|---|---|---|---|
| **UOV** | Salt in digest; pHash = M - 2 bytes | **Salt in digest; pHash = M - 1 byte** | N/A (not possible for UOV) |
| **BLS** | Salt in signed payload; pHash constrained | Salt in signed payload; pHash constrained | **Salt appended after sig or stored in ledger; pHash any length** |

**Recommendation**:
- **UOV**: Reduce salt from 2 bytes to 1 byte. This gives 19 B (UOV-80) or 24 B (UOV-100) for pHash — enough for two-hash and three-hash combinations.
- **BLS**: Use Option C (sign pHash directly, salt outside signature) to allow arbitrary-length pHashes. The 1-byte salt is appended after the signature or stored only in the ledger record.

## Strategy 1: Multi-Hash Combination

Combining multiple independently-designed perceptual hash algorithms dramatically increases collision resistance because an attacker must find a single image that collides with ALL hashes simultaneously.

### How it works

```text
pHash_combined = concat(hash_A(image), hash_B(image), hash_C(image))
```

The combined hash is signed as a single value. At verification time, each component is compared independently, and the results are aggregated.

### Collision resistance improvement

If hash A has `a` effective bits and hash B has `b` effective bits, and the two hashes are independent (capture different image properties), then:

- collision resistance of A alone: `2^(a/2)`
- collision resistance of B alone: `2^(b/2)`
- collision resistance of A AND B: `2^((a+b)/2)`

**Example**: pHash (64 bits) + DinoHash (96 bits) + ColorMoment (first 24 bits) = 184 bits total → 2^92 collision resistance (strict) — matching UOV-100 capacity.

### Independent vs correlated hashes

The security benefit of combining hashes depends on their independence. If two hashes capture the same image property, an attacker who finds a collision for one likely collides for both.

| Combination | Independence | Rationale |
|---|---|---|
| pHash + dHash | **Low** (both use luminance structure) | Both are DCT/gradient on grayscale; likely correlated |
| pHash + ColorMoment | **High** (luminance structure vs color distribution) | Different signal domains |
| pHash + DinoHash | **Medium-High** (classical frequency vs learned features) | Different methods, some overlap in what they capture |
| DinoHash + BlockHash | **Medium** (learned global vs spatial block structure) | Different granularity |
| pHash + RadialVariance | **Medium-High** (frequency vs angular projection) | Orthogonal transforms |
| Any luminance hash + Color hash | **High** (structure vs color) | Fully different signal channels |

### Recommended multi-hash combinations

The table below uses the **1-byte salt** capacity (UOV-80: 19 B, UOV-100: 24 B). BLS with salt-outside-sig has no pHash size limit.

| Combination | Total bytes | Collision bits (strict) | With ~10% tol. | Fits UOV-80 (19 B) | Fits UOV-100 (24 B) | Fits BLS (any) | Properties |
|---|---|---|---|---|---|---|---|
| DinoHash (12B) | 12 B | 2^48 | ~2^43 | Yes | Yes | Yes | Single hash, best robustness; minimal |
| pHash (8B) + colorHash (8B) | 16 B | 2^64 | ~2^57 | Yes | Yes | Yes | Luminance + color; high independence |
| pHash (8B) + DinoHash (11B) | 19 B | 2^76 | ~2^68 | **Exact fit** | Yes | Yes | Classical + neural; strong |
| DinoHash (12B) + colorHash (7B) | 19 B | 2^76 | ~2^68 | **Exact fit** | Yes | Yes | Neural + color; strong |
| pHash (8B) + DinoHash (12B) | 20 B | 2^80 | ~2^72 | No (1B over) | Yes | Yes | Classical + neural; strong |
| pHash (8B) + DinoHash (12B) + colorHash (4B) | 24 B | 2^96 | ~2^86 | No | **Exact fit** | Yes | Three signals; excellent |
| pHash (8B) + DinoHash (12B) + colorHash (8B) | 28 B | 2^112 | ~2^100 | No | No | Yes | Full three-hash; maximum |
| 3×3 grid of pHash | 72 B | 2^288 | ~2^259 | No | No | Yes | Spatial localization |

Note: DinoHash natively produces 12 bytes (96 bits). Truncating to 11 bytes loses 8 bits of discriminative power but enables the exact UOV-80 fit. Whether this truncation is acceptable depends on empirical testing.

### External comparison findings to incorporate

The parallel `perceptual-fuzzy-hash-test-vibe` repository contains useful empirical comparisons we should explicitly carry forward:

| Finding | Why it matters |
|---|---|
| **pHash + ColorHash** was the best no-ML pair (106 bits total) | Strong classical baseline; ColorHash covers pHash weaknesses on crop, flip, and color changes |
| **dHash-FS64 + ColorHash** achieved the same 95% worst-case robustness as pHash + ColorHash | Important flip-safe alternative when mirrored content matters |
| **dHash-FS64** is perfectly H/V flip invariant | Worth benchmarking directly, not just as a derived idea |
| **ColorHash** has weak discrimination on unrelated images (~88% similarity in that repo) | Reinforces that ColorHash must only be used as a complementary signal |
| **pHash** discriminates unrelated images much better than dHash-FS64 (~47% vs ~75% similarity there) | Strong reason to keep pHash as a main discriminator even if dHash-FS64 is included |
| **Best triple** there was pHash + ColorHash + DinoHash-96 (202 bits total) | Strong unconstrained candidate for BLS + ledger storage |

These results suggest that our benchmark shortlist should explicitly include:

- `dHash-FS64`
- `dHash-FS64 + ColorHash(8B)`
- `dHash-FS64 + BlockHash + ColorHash` (or the closest size-constrained variant)

### Suspicious correlation detection

A powerful side benefit of multi-hash comparison: when media is modified, the different hashes change in **different ways** depending on the type of modification. This enables richer forensic analysis:

| Modification | pHash change | ColorMoment change | DinoHash change | Interpretation |
|---|---|---|---|---|
| JPEG recompression | Small | None | Small | Benign processing |
| Color grading | None/small | **Large** | Small | Color edit, structure preserved |
| Object removal / inpainting | **Large locally** | Small | **Large** | Content manipulation |
| Resize | Small | Small | Small | Benign processing |
| Adversarial perturbation | **Large** | Small | Small (if adversarial-robust) | Possible attack against classical hashes |
| Face swap | Medium | Small | **Large** | Semantic manipulation |

If pHash shows high similarity but DinoHash shows low similarity (or vice versa), that pattern itself is suspicious and may indicate a targeted attack against one specific hash algorithm.

## Strategy 2: Spatial / Regional Hashing

Instead of a single global hash for the whole image, compute hashes for specific regions and combine them.

### Grid-based regional hashing

Divide the image into an indexed grid (e.g., 3×3 = 9 cells, or 4×4 = 16 cells) and compute a perceptual hash per cell:

```text
+---+---+---+
| H0| H1| H2|
+---+---+---+
| H3| H4| H5|
+---+---+---+
| H6| H7| H8|
+---+---+---+

regional_hash = concat(H0, H1, ..., H8)
```

**Advantages:**
- Localizes tampering to specific regions
- Increases total hash entropy (e.g., 9 cells × 8 bytes = 72 bytes for a 3×3 grid of pHash)
- An attacker must produce a collision in every cell simultaneously
- Enables partial verification (check only cells of interest)

**Disadvantages:**
- Much larger hash output (may not fit UOV; suitable for BLS with ledger)
- Sensitive to crops and geometric transforms (cell boundaries shift)
- Requires consistent cell alignment between sender and receiver

### CV / AI-guided region-of-interest (ROI) hashing

Instead of a fixed grid, use computer vision or AI to identify semantically important regions:

- **Face detection**: hash each detected face region independently
- **Object detection** (YOLO, etc.): hash regions containing detected objects
- **Saliency maps**: hash the visually most important areas
- **Segmentation** (SAM, etc.): hash each semantic segment

```text
ROI_hashes = {
  face_0: DinoHash(crop(image, face_bbox_0)),
  face_1: DinoHash(crop(image, face_bbox_1)),
  salient_region: pHash(crop(image, saliency_bbox)),
  global: pHash(image)
}
```

**Advantages:**
- Focuses hash entropy on the most important content
- Face-specific hashes detect face swaps that global hashes might miss
- Semantically meaningful verification ("was the main subject changed?")
- Can adapt to image content (more hashes for complex scenes)

**Disadvantages:**
- Requires CV/AI inference at both signing and verification time
- ROI detection must be deterministic or nearly so (same faces/objects must be detected on both sides)
- More complex protocol: sender and receiver must agree on ROI detection method
- Variable-size output (number of ROIs varies per image)

### Hybrid: grid + ROI

Use a fixed grid for baseline spatial coverage plus ROI hashes for high-value content:

```text
combined = grid_hash(3x3, pHash) || face_hashes(DinoHash) || global_color_hash
```

This provides:
- Baseline spatial tamper localization (grid)
- Semantic content verification (face/object ROIs)
- Color channel coverage (global color hash)

## Strategy 3: Additional Ideas

### Frequency-band separation

Hash the image at multiple frequency bands independently (low-pass, band-pass, high-pass). Modifications that affect different frequencies (e.g., blur removes high frequencies, sharpening adds them) show up in different hash components.

### Scale-pyramid hashing

Compute perceptual hashes at multiple resolutions of the same image (e.g., 1x, 0.5x, 0.25x). This catches manipulations that are only visible at certain scales.

### Temporal consistency (video)

For video, compare how per-interval pHashes evolve over time. A legitimate video has smooth pHash transitions between frames; an edited video may show abrupt pHash jumps that don't match motion/scene flow.

### Cross-modal hashing

For images with embedded text (screenshots, documents, memes), combine a visual perceptual hash with an OCR-derived text hash. Content manipulation that changes text but preserves visual layout (or vice versa) becomes detectable.

### Adversarial-robust hashing

DinoHash (2025) specifically trains against adversarial gradient-based attacks. Combining an adversarial-robust hash (DinoHash) with a classical hash (pHash) means an attacker must defeat two fundamentally different defenses:
- Against DinoHash: adversarial perturbation is expensive (model was trained to resist it)
- Against pHash: perturbation needed is structurally different

This "defense in depth" significantly raises the attack cost.

### Steganographic hash verification

An additional layer: embed a second, independent hash in the stego channel using a different transport mechanism or location. The verifier checks both independently. An attacker who modifies the image to collide with the primary hash likely breaks the secondary one (different algorithm, different embedding location).

### Signed edit-tolerance policy

Rather than using a single global similarity threshold, the signer could include a **policy profile** alongside the hash that declares expected tolerances:

```json
{
  "crop_tolerance": "moderate",
  "recompression_tolerance": "high",
  "color_tolerance": "low",
  "overlay_tolerance": "none"
}
```

The verifier would then apply per-transform thresholds when interpreting the similarity score. This lets the same hash infrastructure support different use cases (e.g., photojournalism with very tight tolerances vs social media sharing with loose tolerances).

The policy could be signed alongside the pHash (part of the signed digest) or stored in the ledger metadata. It does not need to fit in the stego channel.

### Provenance-side auxiliary features

Store extra non-hash signals in the ledger record (not in the stego payload) to strengthen verification without increasing embedded payload size:

| Auxiliary feature | Size | Purpose |
|---|---|---|
| Edge histogram | ~32-64 B | Structural fingerprint; detects inpainting and object removal |
| OCR text summary hash | 8-32 B | Detects text replacement in screenshots/documents/memes |
| Dominant palette hash | 8-16 B | Detects color grading and palette swaps |
| Object/label summary | variable | Detects semantic content changes (object add/remove) |
| Face embedding summary | ~32-64 B | Detects face swaps via embedding distance |

These features are computed at signing time and stored in IPFS metadata. The verifier recomputes them from the received image and compares. They complement the primary perceptual hash with additional orthogonal signals.

## Collision Resistance Budget

Given our signature scheme constraints (using 1-byte salt), here's the collision resistance budget:

| Configuration | Total hash bytes | Collision bits (strict) | Est. with ~10% tol. | Fits UOV-80 (19 B) | Fits UOV-100 (24 B) | Fits BLS (any) |
|---|---|---|---|---|---|---|
| Single pHash | 8 B | 2^32 | ~2^29 | Yes | Yes | Yes |
| Single DinoHash | 12 B | 2^48 | ~2^43 | Yes | Yes | Yes |
| pHash + colorHash | 16 B | 2^64 | ~2^57 | Yes | Yes | Yes |
| pHash (8B) + DinoHash (11B) | 19 B | 2^76 | ~2^68 | Exact fit | Yes | Yes |
| pHash + DinoHash (12B) | 20 B | 2^80 | ~2^72 | No (1B over) | Yes | Yes |
| pHash + DinoHash + colorHash (4B) | 24 B | 2^96 | ~2^86 | No | Exact fit | Yes |
| pHash + DinoHash + colorHash (8B) | 28 B | 2^112 | ~2^100 | No | No | Yes |
| 3×3 grid of pHash | 72 B | 2^288 | ~2^259 | No | No | Yes |
| Grid + ROI + multi-algorithm | variable | very high | very high | No | No | Yes |

**Key insights**:

- For **UOV-80** (19 B with 1B salt): two-hash combinations up to 19 B. The sweet spot is **pHash (8B) + DinoHash truncated (11B)** at exactly 19 B with ~2^68 collision resistance (with tolerance).
- For **UOV-100** (24 B with 1B salt): three-hash combinations fit. **pHash (8B) + DinoHash (12B) + colorHash (4B)** at 24 B gives ~2^86 collision resistance with tolerance.
- For **BLS** (unlimited pHash with salt-outside-sig): arbitrarily rich hash compositions. The pHash is stored in the ledger, not in the stego channel.
- Single 8-byte classical hashes (pHash alone) are **dangerously weak** (~2^29 with tolerance). Never use a single classical hash in production.

## Future Work: Empirical Testing

The tolerance penalties and effective collision resistance estimates above are theoretical. Before selecting a final hash configuration, we need empirical measurements. The full benchmark plan now lives in the sibling repository:

- [`/mnt/c/Users/micha/OneDrive/Dokumente/GitHub/perceptual-fuzzy-hash-test-vibe/docs/phash-benchmark-plan.md`](/mnt/c/Users/micha/OneDrive/Dokumente/GitHub/perceptual-fuzzy-hash-test-vibe/docs/phash-benchmark-plan.md)

It covers:

1. **Robustness profiling**: Distance distributions per hash under 24 benign transforms (JPEG, resize, crop, color pipeline, Stardust embed/extract)
2. **Sensitivity testing**: Response to 10 suspicious and 13 malicious transforms (overlays, inpainting, face swap, AI edits, adversarial perturbation)
3. **Bit-change structure analysis**: Per-bit flip frequency, deterministic bit subsets per transform, transform type inference from bit patterns
4. **Multi-hash independence**: Pairwise correlation between hash families, effective independent bits
5. **Composite evaluation**: Classifier accuracy for benign/suspicious/malicious using combined hash distance vectors
6. **Regional analysis**: Grid-based and ROI-based spatial hashing, per-cell sensitivity mapping
7. **Adversarial resistance**: Optimization cost to attack single vs multi-hash configurations

The benchmark harness (`phash-benchmark/`) is designed as a reproducible, incremental, parallelizable Python pipeline producing Parquet output for analysis.

## Size constraints by scheme

Different signature schemes impose different practical limits on the usable perceptual hash size. The table below reflects the **salt-free** option (see "Salt Reconsideration" above).

### UOV

UOV recovers the pHash from the signature via the public map `P(w)`. The output size is fixed at `_PUB_M_BYTE`:

| Variant | With 2B salt (current) | With 1B salt (recommended) | Without salt |
|---|---|---|---|
| UOV-80 | 18 B = 144 bits | **19 B = 152 bits** | 20 B = 160 bits |
| UOV-100 | 23 B = 184 bits | **24 B = 192 bits** | 25 B = 200 bits |

If the pHash exceeds the limit, it is **rejected** (not silently truncated).

### BLS

BLS does not recover the pHash from the signature. With salt-outside-sig (Option C), the signature covers only the pHash, which can be:

- any length (no UOV-style constraint)
- embedded in the payload, or
- omitted from the payload and retrieved from the ledger

BLS imposes **no practical pHash size limit**.

## Scheme comparison with multi-hash strategies

Using 1-byte salt for UOV (in digest) and BLS Option C (salt outside signature):

| Scheme | Max pHash (1B salt) | Best multi-hash fit | Collision (with ~10% tol.) | Stego payload |
|---|---|---|---|---|
| **UOV-80** | 19 B = 152 bits | pHash (8B) + DinoHash (11B) | ~2^68 | 400 bits (sig only) |
| **UOV-100** | 24 B = 192 bits | pHash (8B) + DinoHash (12B) + colorHash (4B) | ~2^86 | 504 bits (sig only) |
| **BLS-BN158** (no embed) | Unlimited | Any combination; stored in ledger | Depends on hash choice; can be very high | 176 bits (sig + 1B salt) |
| **BLS-BN158** (full embed) | Unlimited | pHash + sig + salt + PK all embedded | See collision table above | pHash + 504 bits |
| **BLS12-381** (no embed) | Unlimited | Any combination; stored in ledger | Depends on hash choice; can be very high | 400 bits (sig + 1B salt) |

## Practical recommendation

For production image authentication:

- **Never use a single 8-byte classical hash** (pHash, dHash, aHash alone). Collision resistance with tolerance is ~2^29 — dangerously weak.
- **Minimum**: DinoHash alone (12 B, ~2^43 with tolerance). Acceptable for low-security applications.
- **Recommended for UOV-80**: pHash (8B) + DinoHash truncated (11B) = 19 B with 1B salt, ~2^68 with tolerance.
- **Recommended for UOV-100**: pHash (8B) + DinoHash (12B) + colorHash (4B) = 24 B with 1B salt, ~2^86 with tolerance.
- **Recommended for BLS (embedded)**: pHash + DinoHash + colorHash + sig + salt + PK all in payload. See the collision table for total embedded bits per pHash size.
- **Recommended for BLS (ledger)**: pHash + DinoHash + colorHash (full 8B) + optional spatial hashes. Stored in ledger. No size constraint.
- **For maximum security**: grid-based spatial hashing + multi-algorithm + ROI. BLS with ledger only.

For video, the same considerations apply to each interval hash or to the Merkle-root commitment derived from them.
