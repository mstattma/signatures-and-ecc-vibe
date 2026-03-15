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

| Hash output | Effective bits (strict match) | Collision resistance (strict) | With ~10% tolerance | Collision resistance (~10% tol.) |
|---|---|---|---|---|
| 8 B = 64 bits | 64 | 2^32 | ~58 effective | 2^29 |
| 12 B = 96 bits | 96 | 2^48 | ~86 effective | 2^43 |
| 16 B = 128 bits | 128 | 2^64 | ~115 effective | 2^57 |
| 23 B = 184 bits | 184 | 2^92 | ~166 effective | 2^83 |
| 32 B = 256 bits | 256 | 2^128 | ~230 effective | 2^115 |

**The tolerance penalty is the critical concern.** When we allow fuzzy matching (accepting images as "authentic" even if some hash bits differ), the effective hash space shrinks dramatically. This is because an attacker doesn't need an exact collision — they just need to land within the tolerance ball.

The actual tolerance penalty depends on the hash algorithm and the similarity threshold. This needs empirical measurement for each algorithm we consider, which is planned future work.

### Why classical 64-bit hashes are dangerous

Most classical perceptual hashes (aHash, dHash, pHash, wHash) produce only 64 bits. Even with strict exact-match comparison, that's only 2^32 collision resistance — roughly 4 billion. With fuzzy tolerance, it drops to perhaps 2^25-2^29 (millions to hundreds of millions). An attacker with moderate computational resources could find collisions in minutes to hours.

**This is why we need either larger hashes, multiple hashes, or both.**

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

| Combination | Total bytes | Effective collision resistance | Fits UOV-80 (18 B) | Fits UOV-100 (23 B) | Properties |
|---|---|---|---|---|---|
| pHash (8B) + colorHash (8B) | 16 B | ~2^57 (with tolerance) | Yes | Yes | Luminance structure + color distribution |
| pHash (8B) + DinoHash (12B) | 20 B | ~2^72 (with tolerance) | No | Yes (truncated) | Classical + neural; good independence |
| DinoHash (12B) + colorHash (8B) | 20 B | ~2^72 | No | Yes (truncated) | Neural robustness + color channel |
| pHash (8B) + BlockHash (8B) + colorHash (8B) | 24 B | ~2^86 | No | No (BLS only) | Three independent signals |
| DinoHash (12B) | 12 B | ~2^43 (with tolerance) | Yes | Yes | Single hash, best robustness |

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

## Collision Resistance Budget

Given our signature scheme constraints, here's the collision resistance budget for different configurations:

| Configuration | Total hash bytes | Effective collision bits (est. with ~10% tolerance) | Fits in stego channel? |
|---|---|---|---|
| Single pHash | 8 B | ~29 | UOV: yes; dangerously low collision resistance |
| Single DinoHash | 12 B | ~43 | UOV: yes; acceptable for moderate security |
| pHash + colorHash | 16 B | ~57 | UOV-80: yes; reasonable |
| DinoHash + colorHash (truncated) | 18 B | ~65 | UOV-80: exact fit; good |
| pHash + DinoHash | 20 B | ~72 | UOV-100 (truncated 3B): good |
| DinoHash + colorHash | 20 B | ~72 | UOV-100 (truncated 3B): good |
| pHash + DinoHash + colorHash | 28 B | ~100 | BLS with ledger: excellent |
| 3×3 grid of pHash | 72 B | ~200+ | BLS with ledger: very strong |
| Grid + ROI + multi-algorithm | variable | very high | BLS with ledger only |

**Key insight**: For UOV (where the hash must fit in the stego payload), multi-hash combinations of 2-3 small hashes are the practical ceiling. For BLS with ledger-stored pHash, much richer hash compositions are possible because the hash doesn't need to fit in the stego channel.

## Future Work: Empirical Testing

The tolerance penalties and effective collision resistance estimates above are theoretical. Before selecting a final hash configuration, we need empirical measurements:

1. **Tolerance measurement**: For each hash algorithm, measure the distribution of Hamming distances between same-image pairs under common transformations (JPEG, resize, Stardust embedding, etc.).
2. **Collision search**: For each hash at its effective bit size, attempt to find collisions using gradient-based attacks (for neural hashes) or brute-force search (for classical hashes).
3. **Cross-correlation**: Measure the statistical independence between different hash algorithms on the same dataset. Are pHash and DinoHash bits actually independent?
4. **Attack simulation**: Given a multi-hash configuration, attempt to produce an image that collides on all components simultaneously.
5. **ROI determinism**: Measure how consistently CV/AI models detect the same ROIs across sender/receiver under common transformations.

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
