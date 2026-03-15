# Perceptual Hash Empirical Benchmark Plan

This document defines the empirical testing plan for evaluating perceptual hash algorithms for the fuzzy-signature scheme. The goal is to measure robustness, sensitivity, collision resistance, and bit-change structure across a range of image transformations.

## Goals

1. **Robustness**: How much does each hash change under benign technical processing? Where are the safe thresholds?
2. **Sensitivity**: Can we distinguish benign from suspicious from malicious edits based on hash distance and bit-change patterns?
3. **Bit-change structure**: Do specific transforms produce deterministic or characteristic bit-flip patterns? Can we infer the type, location, and severity of modification from *which* bits changed?
4. **Independence**: Are different hash families sufficiently independent to provide additive collision resistance when combined?
5. **Collision resistance**: What is the practical effective collision resistance under realistic fuzzy tolerance?

## Phase 1: Hash Shortlist

### Primary candidates (benchmark these first)

These are the hashes most likely to be used in production, alone or combined.

| Hash | Output | Library | Why included |
|---|---|---|---|
| **pHash** (DCT) | 8 B | `imagehash` | Baseline classical hash; widely used; well-understood behavior |
| **dHash** | 8 B | `imagehash` | Second classical baseline; gradient-based; complements pHash |
| **colorHash** | 8-14 B | `imagehash` | Color distribution; independent signal from luminance hashes |
| **BlockHash** | 16-32 B | `blockhash` / `imagehash` | Spatial structure; better for localized changes |
| **DinoHash** | 12 B | `dinohash` | SOTA neural hash; adversarial-robust; primary neural candidate |
| **PDQ** | 32 B | `perception` (Thorn) | Meta's open-source hash; designed for social media scale |

### Secondary candidates (benchmark if time allows)

| Hash | Output | Library | Why |
|---|---|---|---|
| **wHash** (wavelet) | 8 B | `imagehash` | Frequency decomposition alternative to pHash |
| **Marr-Hildreth** | 72 B | OpenCV `img_hash` | Edge-based; larger output; potentially better discriminative power |
| **Radial Variance** | 40 B | OpenCV `img_hash` | Rotation-invariant; useful reference for rotation attacks |
| **crop-resistant hash** | variable | `imagehash` | Explicitly designed for crop robustness |
| **SSCD** | 64-512 B | Meta research | SOTA copy detection descriptor; useful upper bound |

### Composite candidates (test as combinations)

| Combination | Total bytes | Target scheme |
|---|---|---|
| pHash (8B) + DinoHash (11B truncated) | 19 B | UOV-80 with 1B salt |
| pHash (8B) + DinoHash (12B) + colorHash (4B) | 24 B | UOV-100 with 1B salt |
| pHash (8B) + DinoHash (12B) + colorHash (8B) | 28 B | BLS with ledger |
| DinoHash (12B) + BlockHash (8B) + colorHash (4B) | 24 B | UOV-100 alternative |

### Regional modes

For each hash, also benchmark in regional mode:

| Mode | Description |
|---|---|
| **global** | Whole image (default) |
| **grid-3x3** | 9 cells, hash per cell |
| **grid-4x4** | 16 cells, hash per cell |
| **face-ROI** | Detected face regions (where applicable) |

## Phase 2: Transform Taxonomy

Every source image is transformed by each of the following. Transforms are grouped into three severity categories.

### Category A: Benign technical changes

These represent normal processing that should NOT invalidate authenticity.

| ID | Transform | Parameters | Rationale |
|---|---|---|---|
| A01 | JPEG recompress | quality: 95, 85, 70, 50, 30 | Most common lossy format |
| A02 | WebP recompress | quality: 85, 50, 30 | Increasingly common web format |
| A03 | AVIF recompress | quality: 85, 50, 30 | Modern codec |
| A04 | PNG round-trip | lossless | Format conversion only |
| A05 | Downscale | 75%, 50%, 25% of original | Common sharing resize |
| A06 | Upscale | 150%, 200% of downscaled | Re-enlargement after share |
| A07 | Aspect-preserving resize | to 1920px wide, to 1080px wide, to 640px wide | Standard web sizes |
| A08 | Center square crop | largest inscribed square | Instagram-style |
| A09 | Portrait crop (4:5) | center, top-biased | Instagram portrait |
| A10 | Landscape crop (16:9) | center | YouTube thumbnail |
| A11 | Slight off-center crop | 5% offset from center, square | Minor reframing |
| A12 | sRGB ↔ Display-P3 | round-trip conversion | Color space mismatch |
| A13 | YUV420 round-trip | RGB→YUV420→RGB | Codec color subsampling |
| A14 | YUV444 round-trip | RGB→YUV444→RGB | Higher-quality subsampling |
| A15 | HDR→SDR tone map | Reinhard or ACES | HDR content viewed on SDR display |
| A16 | Limited↔full range | 16-235 ↔ 0-255 | Broadcast vs computer levels |
| A17 | White balance shift | ±500K, ±1000K | Camera WB error |
| A18 | Gamma shift | γ=1.8, γ=2.4 (from 2.2) | Display gamma mismatch |
| A19 | Mild sharpen | unsharp mask radius 1, amount 50% | Standard post-processing |
| A20 | Mild blur | Gaussian σ=1.0 | Slight defocus or anti-alias |
| A21 | Denoise | bilateral or NLMeans, mild | Camera noise reduction |
| A22 | Contrast adjust | ±10% | Auto-levels style adjustment |
| A23 | Saturation adjust | ±15% | Color boost/fade |
| A24 | Stardust embed + extract | default strength | Our actual stego transport |

### Category B: Suspicious but probably benign modifications

Obvious visible edits that are not necessarily malicious.

| ID | Transform | Parameters | Rationale |
|---|---|---|---|
| B01 | Sticker/emoji overlay | 1 small sticker, random position | Social media reaction |
| B02 | Multiple sticker overlay | 3-5 stickers | Heavy decoration |
| B03 | Arrow annotation | single arrow pointing at area | Screenshot annotation |
| B04 | Circle/box annotation | single highlight circle or box | Emphasis marking |
| B05 | Line overlay | single drawn line across image | Minimal drawing |
| B06 | Blur bar / mosaic redaction | 10% of image area | Privacy redaction |
| B07 | Watermark overlay | semi-transparent text diagonally | Copyright watermark |
| B08 | Caption/text overlay | small text banner at bottom | Social media caption |
| B09 | Frame/border | solid or decorative border, 5% | Photo framing |
| B10 | Meme text | large text top+bottom | Meme creation |

### Category C: Malicious modifications

Stealthy changes intended to alter meaning while preserving appearance.

| ID | Transform | Parameters | Rationale |
|---|---|---|---|
| C01 | Object removal (inpainting) | remove one prominent object | Content manipulation |
| C02 | Object insertion | paste foreign object, blend edges | Evidence fabrication |
| C03 | Face swap | replace face with different person | Identity manipulation |
| C04 | Text replacement | change text in screenshot/sign/document | Information manipulation |
| C05 | Logo/label replacement | swap brand/label | Product fraud |
| C06 | Color change (semantic) | change color of a key object (e.g., car, shirt) | Subtle identity change |
| C07 | Texture replacement | change surface texture of object | Material manipulation |
| C08 | Generative fill / outpainting | extend or fill area with AI | Content fabrication |
| C09 | AI re-creation (partial) | regenerate a region with stable diffusion inpainting | Subtle AI edit |
| C10 | AI re-creation (full) | img2img the entire image at high fidelity | Full AI recreation |
| C11 | Adversarial perturbation (pHash) | optimize to change pHash while minimizing visual change | Targeted hash attack |
| C12 | Adversarial perturbation (DinoHash) | optimize to change DinoHash while minimizing visual change | Targeted neural attack |
| C13 | Adversarial perturbation (both) | optimize to change pHash AND DinoHash | Multi-hash attack |

### Category D: Unrelated images (collision baseline)

| ID | Source | Purpose |
|---|---|---|
| D01 | Random image from dataset | Baseline distance distribution for unrelated pairs |
| D02 | Same scene, different content | Hard negative: structural similarity |
| D03 | Same template, different text | Hard negative: meme/screenshot templates |
| D04 | Same object, different instance | Hard negative: product/face similarity |

## Phase 3: Source Datasets

| Dataset | Content type | Size (suggested) | Purpose |
|---|---|---|---|
| **COCO** (subset) | Natural photographs | ~500 images | General photographic content |
| **OpenImages** (subset) | Diverse photographs | ~500 images | Broad visual diversity |
| **Screenshot corpus** | UI/text-heavy images | ~100 images | Text replacement attacks |
| **Face dataset** | Portraits, groups | ~100 images | Face swap attacks |
| **Meme templates** | Template + filled versions | ~50 pairs | Hard negatives for text changes |
| **Own camera photos** | Real device photos | ~50 images | Realistic camera pipeline |

Total: ~1300 source images × ~50 transforms = ~65,000 image pairs.

## Phase 4: Output Schema

For every (source image, transform, hash algorithm, region mode) tuple, record one row:

```json
{
  "image_id": "coco_000123",
  "variant_id": "coco_000123__A01_q50",
  "transform_id": "A01",
  "transform_family": "benign",
  "transform_params": {"codec": "jpeg", "quality": 50},
  "roi_mode": "global",
  "hash_name": "pHash",
  "hash_bytes": 8,
  "hash_original_hex": "f8e0c0c0e0f0f8ff",
  "hash_variant_hex": "f8e0c0c0e0f0f0ff",
  "hamming_distance": 1,
  "normalized_similarity": 0.984,
  "xor_mask_hex": "0000000000000800",
  "changed_bit_positions": [11],
  "total_bits": 64
}
```

For composite hashes, store one row per component hash.

For regional modes, store one row per region with additional fields:

```json
{
  "roi_mode": "grid-3x3",
  "roi_index": 4,
  "roi_bounds": {"x": 213, "y": 120, "w": 213, "h": 120}
}
```

Storage format: **Parquet** or **JSONL** (compressed). Estimated size: ~65K pairs × 6 hashes × 4 region modes ≈ 1.5M rows × ~300 bytes ≈ ~450 MB uncompressed, ~50 MB compressed.

## Phase 5: Analysis Plan

### 5.1 Per-hash robustness profile

For each hash:
- plot Hamming distance distribution per transform family (A/B/C/D)
- compute mean, p95, p99 distances for benign transforms
- determine threshold T where FRR (false reject rate for benign) < 1%
- compute FAR (false accept rate for malicious) at that T

### 5.2 Bit-change structure analysis

For each hash and transform:
- accumulate XOR masks across all images for the same transform
- compute per-bit flip frequency (how often does bit N flip for transform X?)
- identify deterministic or near-deterministic bit subsets per transform
- cluster bit-change patterns (e.g., "JPEG artifacts always flip bits 3, 17, 42")
- build a per-transform bit-signature fingerprint

Questions to answer:
- can we infer the transform type from the bit-change pattern alone?
- can we distinguish "JPEG Q50" from "JPEG Q30" by pattern?
- do crops produce spatially structured bit patterns?
- do color changes exclusively flip color-related bits (in hashes that separate color)?

### 5.3 Multi-hash independence

For each pair of hashes (A, B):
- compute distance correlation across all transforms: `corr(d_A, d_B)`
- compute per-bit correlation between hash A bit N and hash B bit M
- estimate effective independent bits: `effective_combined = bits_A + bits_B * (1 - correlation)`

Expected findings:
- pHash + dHash: high correlation (both luminance-structural)
- pHash + colorHash: low correlation (different signal domains)
- pHash + DinoHash: moderate correlation (some shared structure sensitivity)
- DinoHash + colorHash: low correlation

### 5.4 Composite evaluation

For the recommended combinations:
- compute joint distance vectors
- train a simple classifier (logistic regression or random forest) on:
  - benign vs malicious (binary)
  - benign vs suspicious vs malicious (3-class)
  - transform type identification (multi-class)
- compare composite classifier accuracy vs single-hash accuracy
- measure collision resistance: how many unrelated pairs fall within the benign threshold for ALL hashes simultaneously?

### 5.5 Regional analysis

For grid and ROI modes:
- which cells change most under each transform?
- can localized edits (C01-C09) be detected by per-cell distance spike?
- how stable are cell boundaries under crops?
- for face ROI: how consistent is face detection across transforms?

### 5.6 Adversarial resistance

For transforms C11-C13:
- how many optimization steps needed to flip N bits of pHash? of DinoHash?
- does attacking pHash leave DinoHash unchanged (and vice versa)?
- how visually detectable is the adversarial perturbation?
- multi-hash attack cost vs single-hash attack cost

## Phase 6: Benchmark Harness Architecture

```
phash-benchmark/
├── README.md
├── requirements.txt           # Python deps: imagehash, dinohash, pillow, opencv, etc.
├── config.yaml                # Dataset paths, hash list, transform list, output paths
├── datasets/
│   ├── download.py            # Download/prepare COCO, OpenImages subsets
│   └── README.md              # Dataset preparation instructions
├── transforms/
│   ├── benign.py              # Category A transforms
│   ├── suspicious.py          # Category B transforms
│   ├── malicious.py           # Category C transforms (requires AI tools)
│   └── common.py              # Shared image I/O, transform application
├── hashes/
│   ├── classical.py           # pHash, dHash, colorHash, BlockHash, wHash
│   ├── neural.py              # DinoHash, PDQ
│   ├── regional.py            # Grid and ROI wrappers
│   └── composite.py           # Multi-hash concatenation and comparison
├── pipeline/
│   ├── generate_variants.py   # Apply all transforms to all source images
│   ├── compute_hashes.py      # Hash all originals + variants
│   ├── compute_distances.py   # Compare original vs variant hashes, write output rows
│   └── run_all.py             # Orchestrate the full pipeline
├── analysis/
│   ├── robustness.py          # Per-hash distance distributions, threshold selection
│   ├── bit_structure.py       # Per-bit flip analysis, transform fingerprints
│   ├── independence.py        # Cross-hash correlation analysis
│   ├── composite.py           # Multi-hash classifier training and evaluation
│   ├── regional.py            # Per-cell analysis
│   └── report.py              # Generate summary tables and plots
├── output/
│   ├── variants/              # Transformed images (large, gitignored)
│   ├── hashes.parquet         # All hash values
│   ├── distances.parquet      # All distance measurements
│   └── reports/               # Generated analysis reports
└── Makefile                   # make generate, make hash, make analyze, make report
```

### Key design principles

- **Reproducible**: all transforms are deterministic (seeded where randomness is needed)
- **Incremental**: can add new hashes or transforms without rerunning everything
- **Parallelizable**: generate_variants and compute_hashes are embarrassingly parallel
- **Separation**: transform generation, hashing, distance computation, and analysis are independent steps

### Dependencies

```
# Core
pillow
numpy
imagehash          # pHash, dHash, aHash, wHash, colorHash, BlockHash, crop-resistant
opencv-python      # img_hash module (Marr-Hildreth, RadialVariance, BlockMean, ColorMoment)

# Neural
dinohash           # DinoHash (needs ONNX runtime or PyTorch)
perception         # PDQ (Thorn)

# Transform tools
ffmpeg-python      # For codec re-encoding transforms
# Or call ffmpeg CLI directly

# AI edits (Category C, optional)
diffusers          # Stable Diffusion inpainting for C08-C10
insightface        # Face swap for C03 (or similar)
# These are heavy; can be deferred to later phases

# Analysis
pandas
pyarrow            # Parquet I/O
matplotlib
scikit-learn       # Simple classifiers for composite evaluation
```

### Execution order

```bash
# 1. Prepare datasets
python datasets/download.py

# 2. Generate all transformed variants
python pipeline/generate_variants.py --config config.yaml

# 3. Compute hashes for all originals + variants
python pipeline/compute_hashes.py --config config.yaml

# 4. Compute distances
python pipeline/compute_distances.py --config config.yaml

# 5. Run analysis
python analysis/robustness.py
python analysis/bit_structure.py
python analysis/independence.py
python analysis/composite.py
python analysis/regional.py
python analysis/report.py
```

Or simply:

```bash
make all
```

## Deliverables

1. **Distance distribution plots** per hash per transform family
2. **Threshold recommendations** per hash (FRR < 1% for benign)
3. **Bit-change fingerprints** per transform (which bits flip deterministically)
4. **Independence matrix** between hash pairs
5. **Composite classifier** accuracy for benign/suspicious/malicious
6. **Regional sensitivity maps** showing which grid cells respond to which edits
7. **Collision resistance estimates** with measured (not theoretical) tolerance
8. **Final hash combination recommendation** for UOV-80, UOV-100, and BLS configurations
