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

## Salt Reconsideration

The salt was originally introduced to ensure distinct signatures when the same pHash is signed for different images. However, the security picture changes when we consider multi-hash strategies:

### The salt costs pHash capacity in UOV

With the salt-in-digest architecture, each byte of salt steals one byte from the recoverable pHash:

| Variant | With 2B salt | Without salt | Capacity gained |
|---|---|---|---|
| UOV-80 | 18 B = 144 bits | **20 B = 160 bits** | +16 bits (+2 bytes) |
| UOV-100 | 23 B = 184 bits | **25 B = 200 bits** | +16 bits (+2 bytes) |

Those extra 2 bytes are significant — they can accommodate an additional small hash component (e.g., colorHash truncated to 2 bytes = 16 bits, adding ~8 bits of independent collision resistance).

### Near-duplicate prohibition replaces the salt

The salt's original purpose was to prevent identical signatures for the same pHash. But the ledger's `(pHash, salt)` duplicate detection already prevents re-registration. If we instead enforce **near-duplicate prohibition** — rejecting any new image whose pHash is identical or nearly identical to an already-registered pHash — the salt becomes unnecessary:

- **Same image, different salt**: previously allowed (different signatures). Now: rejected by near-duplicate detection.
- **Same image, no salt**: produces the same signature. Rejected by duplicate detection on the pHash alone.
- **Different image, same pHash (collision)**: still rejected by duplicate detection — which is correct behavior.

This shifts the duplicate detection key from `(pHash, salt)` to just `pHash` (or a fuzzy match on the pHash).

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

| Scheme | Option A (current) | Option B (salt-free) | Option C (BLS: salt after sig) |
|---|---|---|---|
| **UOV** | Salt in digest; pHash = M - salt bytes | No salt; pHash = full M bytes; deduplicate on pHash alone | N/A (not possible) |
| **BLS** | Salt in signed payload; pHash length constrained | No salt; sign pHash directly | Salt appended after sig or stored in ledger; pHash any length |

**Recommendation**: Remove the salt from UOV to maximize pHash capacity. For BLS, use Option C (sign pHash directly, salt outside signature) to allow arbitrary-length pHashes.

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

The table below uses the **salt-free** capacity (UOV-80: 20 B, UOV-100: 25 B). With 2B salt the capacity is 2 bytes less. BLS with salt-outside-sig has no pHash size limit.

| Combination | Total bytes | Collision bits (strict) | With ~10% tol. | Fits UOV-80 (20 B) | Fits UOV-100 (25 B) | Properties |
|---|---|---|---|---|---|---|
| DinoHash (12B) | 12 B | 2^48 | ~2^43 | Yes | Yes | Single hash, best robustness; minimal |
| pHash (8B) + colorHash (8B) | 16 B | 2^64 | ~2^57 | Yes | Yes | Luminance + color; high independence |
| pHash (8B) + DinoHash (12B) | 20 B | 2^80 | ~2^72 | **Exact fit** | Yes | Classical + neural; strong |
| DinoHash (12B) + colorHash (8B) | 20 B | 2^80 | ~2^72 | **Exact fit** | Yes | Neural + color; strong |
| pHash (8B) + DinoHash (12B) + colorHash (4B) | 24 B | 2^96 | ~2^86 | No | Yes (24 < 25) | Three signals; excellent |
| pHash (8B) + DinoHash (12B) + colorHash (5B) | 25 B | 2^100 | ~2^90 | No | **Exact fit** | Three signals; excellent |
| pHash (8B) + DinoHash (12B) + colorHash (8B) | 28 B | 2^112 | ~2^100 | No | No (BLS only) | Full three-hash; maximum |
| 3×3 grid of pHash | 72 B | 2^288 | ~2^259 | No | No (BLS only) | Spatial localization |

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

Given our signature scheme constraints (using salt-free capacity), here's the collision resistance budget:

| Configuration | Total hash bytes | Collision bits (strict) | Est. with ~10% tol. | Fits UOV-80 (20 B) | Fits UOV-100 (25 B) | Fits BLS (any) |
|---|---|---|---|---|---|---|
| Single pHash | 8 B | 2^32 | ~2^29 | Yes | Yes | Yes |
| Single DinoHash | 12 B | 2^48 | ~2^43 | Yes | Yes | Yes |
| pHash + colorHash | 16 B | 2^64 | ~2^57 | Yes | Yes | Yes |
| pHash + DinoHash | 20 B | 2^80 | ~2^72 | Exact fit | Yes | Yes |
| DinoHash + colorHash | 20 B | 2^80 | ~2^72 | Exact fit | Yes | Yes |
| pHash + DinoHash + colorHash (5B) | 25 B | 2^100 | ~2^90 | No | Exact fit | Yes |
| pHash + DinoHash + colorHash (8B) | 28 B | 2^112 | ~2^100 | No | No | Yes |
| 3×3 grid of pHash | 72 B | 2^288 | ~2^259 | No | No | Yes |
| Grid + ROI + multi-algorithm | variable | very high | very high | No | No | Yes |

**Key insights**:

- For **UOV-80** (20 B without salt): two-hash combinations up to 20 B. The sweet spot is **pHash + DinoHash** or **DinoHash + colorHash** at exactly 20 B with ~2^72 collision resistance (with tolerance).
- For **UOV-100** (25 B without salt): three-hash combinations fit. **pHash + DinoHash + colorHash (5B)** at 25 B gives ~2^90 collision resistance with tolerance.
- For **BLS** (unlimited pHash with salt-outside-sig): arbitrarily rich hash compositions. The pHash is stored in the ledger, not in the stego channel.
- Single 8-byte classical hashes (pHash alone) are **dangerously weak** (~2^29 with tolerance). Never use a single classical hash in production.

## Future Work: Empirical Testing

The tolerance penalties and effective collision resistance estimates above are theoretical. Before selecting a final hash configuration, we need empirical measurements:

1. **Tolerance measurement**: For each hash algorithm, measure the distribution of Hamming distances between same-image pairs under common transformations (JPEG, resize, Stardust embedding, etc.).
2. **Collision search**: For each hash at its effective bit size, attempt to find collisions using gradient-based attacks (for neural hashes) or brute-force search (for classical hashes).
3. **Cross-correlation**: Measure the statistical independence between different hash algorithms on the same dataset. Are pHash and DinoHash bits actually independent?
4. **Attack simulation**: Given a multi-hash configuration, attempt to produce an image that collides on all components simultaneously.
5. **ROI determinism**: Measure how consistently CV/AI models detect the same ROIs across sender/receiver under common transformations.

## Size constraints by scheme

Different signature schemes impose different practical limits on the usable perceptual hash size. The table below reflects the **salt-free** option (see "Salt Reconsideration" above).

### UOV

UOV recovers the pHash from the signature via the public map `P(w)`. The output size is fixed at `_PUB_M_BYTE`:

| Variant | With 2B salt (current) | Without salt (recommended) |
|---|---|---|
| UOV-80 | 18 B = 144 bits | **20 B = 160 bits** |
| UOV-100 | 23 B = 184 bits | **25 B = 200 bits** |

If the pHash exceeds the limit, it is **rejected** (not silently truncated).

### BLS

BLS does not recover the pHash from the signature. With salt-outside-sig (Option C), the signature covers only the pHash, which can be:

- any length (no UOV-style constraint)
- embedded in the payload, or
- omitted from the payload and retrieved from the ledger

BLS imposes **no practical pHash size limit**.

## Scheme comparison with multi-hash strategies

| Scheme | Max pHash (salt-free) | Best multi-hash fit | Collision resistance (with ~10% tolerance) | Stego payload |
|---|---|---|---|---|
| **UOV-80** | 20 B = 160 bits | pHash (8B) + DinoHash (12B) | ~2^72 | 400 bits (sig only) |
| **UOV-100** | 25 B = 200 bits | pHash (8B) + DinoHash (12B) + colorHash (5B) | ~2^90 | 504 bits (sig only) |
| **BLS-BN158** (no embed) | Unlimited | Any combination; stored in ledger | Depends on hash choice; can be very high | 168 bits (sig only) |
| **BLS12-381** (no embed) | Unlimited | Any combination; stored in ledger | Depends on hash choice; can be very high | 392 bits (sig only) |

## Practical recommendation

For production image authentication:

- **Never use a single 8-byte classical hash** (pHash, dHash, aHash alone). Collision resistance with tolerance is ~2^29 — dangerously weak.
- **Minimum**: DinoHash alone (12 B, ~2^43 with tolerance). Acceptable for low-security applications.
- **Recommended for UOV-80**: pHash (8B) + DinoHash (12B) = 20 B, ~2^72 with tolerance. Remove salt to fit.
- **Recommended for UOV-100**: pHash (8B) + DinoHash (12B) + colorHash (5B) = 25 B, ~2^90 with tolerance. Remove salt to fit.
- **Recommended for BLS**: pHash + DinoHash + colorHash (full 8B) + optional spatial hashes. Store in ledger. No size constraint.
- **For maximum security**: grid-based spatial hashing + multi-algorithm + ROI. BLS with ledger only.

For video, the same considerations apply to each interval hash or to the Merkle-root commitment derived from them.
