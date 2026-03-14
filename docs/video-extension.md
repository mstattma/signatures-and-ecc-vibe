# Video Extension: Authenticating Video Content

This document extends the image authentication architecture to support video, where a single video contains a varying number of perceptual hashes (pHashes), each covering a specific time interval.

## Problem Statement

A video differs from a still image in several ways:

- A video has **N perceptual hashes**, one per time interval (e.g., one per scene, one per second, one per GOP). N can range from a handful (short clip) to thousands (feature-length).
- The stego channel capacity per frame is still **~500 bits** — far too small to embed N signatures.
- We need to authenticate the **entire video** (all intervals) with a single compact signature that fits in the stego channel.
- Individual intervals should be **independently verifiable** — a verifier who has only a clip should be able to check the pHash of that clip against the authenticated commitment.
- Tampered intervals should be detectable without re-verifying the entire video.

## Architecture: Merkle Tree of pHashes

The solution uses a **Merkle tree** to commit to all N pHashes with a single root hash. The root is signed once, and the signature is embedded in the video's stego channel. Individual pHashes are verified via Merkle proofs retrieved from the ledger.

### Overview

```
Video: [interval 0] [interval 1] [interval 2] ... [interval N-1]
         │             │             │                   │
         ▼             ▼             ▼                   ▼
       pHash_0       pHash_1       pHash_2           pHash_{N-1}
         │             │             │                   │
         └──────┬──────┘             └────────┬──────────┘
                │                             │
            H(pH_0||pH_1)               H(pH_{N-2}||pH_{N-1})
                │                             │
                └──────────────┬──────────────┘
                               │
                          Merkle Root
                               │
                               ▼
                    sign(sk, Merkle Root)
                               │
                               ▼
                         [signature]
                               │
                    ┌──────────┴──────────┐
                    ▼                     ▼
            stego_embed              ledger registration
         (in video frames)         (Merkle root + tree on IPFS)
```

### Merkle Tree Construction

Each leaf is the hash of one pHash concatenated with its interval metadata:

```
leaf_i = H(pHash_i || interval_start_i || interval_end_i || interval_index_i)
```

The interval metadata (start time, end time, index) is included in the leaf to bind each pHash to a specific position in the video. Without this, an attacker could reorder intervals.

The tree is a standard binary Merkle tree with SHA-256 hashes:

```
         root
        /    \
      h01     h23
      / \     / \
    l0   l1  l2  l3
```

For N intervals, the tree has depth `ceil(log2(N))` and N leaves. The Merkle root is a single 256-bit value.

### What Gets Signed

The signature covers the **Merkle root**, which implicitly commits to all N pHashes and their ordering:

```
signature = stego_sign(sk, merkle_root)
```

For UOV with message recovery, the Merkle root is recovered from the signature by the verifier. For BLS, the Merkle root can be embedded in the payload or looked up from the ledger.

**Payload size in the stego channel is identical to the image case** — the signature covers a single hash (the Merkle root) rather than a single pHash. No additional bandwidth is needed regardless of the number of intervals.

| Scheme | Payload bits (same as image) | Notes |
|---|---|---|
| UOV-80 | 400 | Merkle root recovered from sig; max root = 144 bits (truncated SHA-256) |
| UOV-100 | 504 | Merkle root recovered from sig; max root = 184 bits |
| BLS-BN158 (no root embedded) | 184 | Root from ledger |
| BLS-BN158 (root embedded) | 280-440 | Root = 128-256 bits appended |
| BLS12-381 (no root embedded) | 408 | Root from ledger |

**Note on UOV Merkle root truncation**: UOV-80 recovers at most 144 bits, so the Merkle root must be truncated to 144 bits (18 bytes). This provides 2^72 collision resistance (with 2-byte salt), which is acceptable for binding to a specific Merkle tree but lower than the full 256-bit SHA-256. UOV-100 recovers 184 bits (2^92 collision resistance with salt). For maximum security, BLS with a 256-bit root from the ledger provides full 2^128 collision resistance.

### Stego Embedding in Video

The signature is embedded redundantly across multiple frames for robustness against frame drops, re-encoding, and seeking:

```
Frame 0:   [signature (stego channel)]
Frame K:   [signature (stego channel)]     ← repeat every K frames
Frame 2K:  [signature (stego channel)]
...
```

**Embedding strategies:**

| Strategy | Description | Pros | Cons |
|---|---|---|---|
| **Every keyframe (I-frame)** | Embed signature in each I-frame of the video codec (H.264/H.265 IDR frames) | Survives seeking; aligned with codec structure | Keyframe interval varies (1-10 seconds) |
| **Fixed interval** | Embed every N frames (e.g., every 30 frames = 1/sec at 30fps) | Predictable; easy to extract | May not align with codec boundaries |
| **Every frame** | Embed in every frame | Maximum redundancy | Increases stego channel load; may affect quality |
| **Once (single frame)** | Embed in first frame only | Minimal impact | Lost if first frame is dropped/damaged |

**Recommendation**: Embed in **every I-frame** plus at a **minimum fixed interval** (e.g., at least once per 5 seconds). This provides redundancy, survives codec re-encoding (which preserves I-frame boundaries), and is detectable by the verifier regardless of which portion of the video is available.

### Interval Verification

A verifier who has a clip (or the full video) can verify individual intervals:

1. Extract the signature from any embedded frame
2. Recover/retrieve the Merkle root from the signature
3. Retrieve the full Merkle tree (or the relevant proof path) from the ledger/IPFS
4. Compute the pHash of the clip's corresponding interval
5. Verify the pHash against the Merkle proof for that interval

```
Verifier has: video clip covering intervals [i..j]
    │
    ▼
1. stego_extract(frame) ──► signature
2. Recover Merkle root from signature (or from ledger)
3. Fetch Merkle proofs for intervals i..j from IPFS
    │
    ▼
4. For each interval k in [i..j]:
     a. Compute pHash_k from the video frames in interval k
     b. Compute leaf_k = H(pHash_k || start_k || end_k || k)
     c. Verify Merkle proof: proof_k proves leaf_k is in the tree with this root
     d. If proof valid: interval k is authentic
     e. If proof invalid: interval k may be tampered
```

**Selective verification**: A verifier does NOT need the full video or all N pHashes. They only need the Merkle proof for the intervals they want to check. Each proof is `ceil(log2(N))` hashes = `32 * log2(N)` bytes. For a 1-hour video with 1-second intervals (N=3600), each proof is `32 * 12 = 384 bytes`.

### Tamper Detection

If an attacker modifies frames in interval k:

- The pHash of interval k will change
- The recomputed leaf will differ from the original
- The Merkle proof will fail for that interval
- All other intervals remain independently verifiable

If an attacker reorders intervals:

- The interval metadata (start, end, index) in each leaf prevents reordering
- Swapping intervals i and j produces different leaves that won't match the proofs

If an attacker inserts or removes intervals:

- The Merkle tree structure changes, producing a different root
- The signed root in the stego channel won't match

## Ledger Schema for Video

### On-chain attestation data

The video attestation extends the image schema:

```
bytes32 sigPrefix,        // First 16 bytes of the stego signature
bytes signature,          // Full stego signature
uint8 scheme,             // Signature scheme
bytes publicKey,          // Stego signing public key
bytes32 merkleRoot,       // Root of the pHash Merkle tree (replaces pHash field)
bytes2 salt,              // Salt
uint16 intervalCount,     // Number of intervals (N)
bytes32 fileHash,         // SHA-256 of the video after stego embedding
bytes32 metadataCID       // IPFS CID of metadata + full Merkle tree
```

Changes from the image schema:
- `pHash` (variable, 12-23 bytes) → `merkleRoot` (fixed, 32 bytes)
- Added `intervalCount` (2 bytes)
- `metadataCID` now points to a larger metadata object that includes the full Merkle tree

**Gas impact**: Minimal. The `merkleRoot` is 32 bytes (same as `fileHash`), and `intervalCount` adds only 2 bytes. The on-chain attestation size is roughly the same as for images.

### IPFS metadata for video

```json
{
  "name": "VID_20240315_142530.mp4",
  "description": "Authenticated video",
  "image": "ipfs://Qm.../thumbnail.avif",
  "properties": {
    "type": "video",
    "file_size": 152483921,
    "file_name": "VID_20240315_142530.mp4",
    "mime_type": "video/mp4",
    "duration_seconds": 127.5,
    "dimensions": "1920x1080",
    "fps": 30,
    "codec": "H.265",
    "exif": { ... },
    "stego": {
      "scheme": "uov-80",
      "signature_hex": "a1b2c3...",
      "merkle_root_hex": "deadbeef...",
      "salt_hex": "f0a1",
      "embed_interval_frames": 30,
      "embed_strategy": "keyframe+fixed"
    },
    "intervals": [
      {
        "index": 0,
        "start_ms": 0,
        "end_ms": 1000,
        "phash_hex": "deef001122...",
        "merkle_proof": ["ab12...", "cd34...", "ef56...", ...]
      },
      {
        "index": 1,
        "start_ms": 1000,
        "end_ms": 2000,
        "phash_hex": "1122334455...",
        "merkle_proof": ["ff00...", "aa11...", "bb22...", ...]
      }
    ]
  }
}
```

### IPFS storage cost for video metadata

The Merkle proofs dominate the metadata size for videos:

| Video duration | Interval | N intervals | Proof depth | Per-interval metadata | Total metadata |
|---|---|---|---|---|---|
| 30 seconds | 1 sec | 30 | 5 | ~200 B | ~6 KB |
| 5 minutes | 1 sec | 300 | 9 | ~340 B | ~100 KB |
| 30 minutes | 1 sec | 1,800 | 11 | ~400 B | ~720 KB |
| 1 hour | 1 sec | 3,600 | 12 | ~430 B | ~1.5 MB |
| 1 hour | 5 sec | 720 | 10 | ~370 B | ~260 KB |
| 2 hours | 1 sec | 7,200 | 13 | ~460 B | ~3.3 MB |
| 2 hours | 5 sec | 1,440 | 11 | ~400 B | ~576 KB |

Per-interval metadata includes: pHash (23 B), interval timestamps (8 B), index (2 B), Merkle proof (`depth * 32 B`), plus JSON overhead.

**Recommendation**: Use **5-second intervals** for most video content. This reduces N by 5x while still detecting tampering at a reasonable granularity. For high-security applications (forensic video), use **1-second intervals** or finer.

### Storage costs for video

| Video type | Interval | Metadata size | Storacha (free 5 GB) | Filebase (free 5 GB) |
|---|---|---|---|---|
| Short clip (30s, 1s intervals) | 1 sec | ~6 KB | Free | Free |
| Medium (5 min, 5s intervals) | 5 sec | ~20 KB | Free | Free |
| Long (1 hr, 5s intervals) | 5 sec | ~260 KB | Free | Free |
| Feature (2 hr, 5s intervals) | 5 sec | ~576 KB | Free | Free |
| Feature (2 hr, 1s intervals) | 1 sec | ~3.3 MB | Free | Free |

Even for feature-length video at 1-second intervals, the metadata is only ~3.3 MB — well within free tiers.

## Duplicate Detection for Video

The resolver's `(pHash, salt)` uniqueness constraint doesn't directly apply to video, since videos have N pHashes rather than one. Instead, duplicate detection for video uses the **Merkle root**:

```
Uniqueness key = keccak256(merkleRoot || salt)
```

This ensures:
- The same video (same pHashes in same order) with the same salt can't be registered twice
- The same video with a different salt CAN be registered (different signing session)
- A video with even one modified interval produces a different Merkle root

The resolver contract handles both image and video attestations via the same `(commitment, salt)` pattern — for images, the commitment is the pHash; for videos, it's the Merkle root.

## Extended Signing Flow for Video

```
1. Analyze video ──► extract N intervals with time boundaries
                       │
                       ▼
2. For each interval i:
     pHash_i = perceptual_hash(frames in interval i)
     leaf_i = H(pHash_i || start_i || end_i || i)
                       │
                       ▼
3. Build Merkle tree from [leaf_0, leaf_1, ..., leaf_{N-1}]
     merkle_root = root of tree
                       │
                       ▼
4. Sign the Merkle root:
     signature = stego_sign(sk, merkle_root, salt)
                       │
               ┌───────┴────────┐
               │                │
               ▼                ▼
5a. Embed signature       5b. Register on ledger:
    in video frames           - merkleRoot, signature, PK, fileHash
    (every I-frame            - Upload to IPFS: full Merkle tree,
     + fixed interval)          all pHashes, all proofs, metadata
               │
               ▼
6. Produce authenticated video file
```

## Extended Verification Flow for Video

```
1. Receive video (full or clip)
      │
      ▼
2. stego_extract(frame) ──► signature
      │
      ▼
3. Recover/retrieve Merkle root from signature
      │
      ▼
4. Lookup signature on ledger ──► attestation
      │                            │
      │                            ├── merkleRoot (verify matches recovered root)
      │                            ├── publicKey
      │                            ├── intervalCount
      │                            ├── fileHash
      │                            └── metadataCID ──► IPFS ──► full tree + proofs
      │
      ▼
5. For each interval in the received video:
     a. Compute pHash from video frames
     b. Fetch Merkle proof from IPFS metadata
     c. Verify: proof(pHash, interval_metadata) → root matches signed root?
     d. Result: AUTHENTIC or TAMPERED per interval
      │
      ▼
6. Summary:
     - Intervals [0-45]: AUTHENTIC (all proofs valid)
     - Interval [46]: TAMPERED (pHash mismatch)
     - Intervals [47-120]: AUTHENTIC
```

## Comparison: Image vs Video

| Property | Image | Video |
|---|---|---|
| pHashes per file | 1 | N (one per interval) |
| What gets signed | pHash (or H(pHash) for UOV) | Merkle root of N pHashes |
| Stego payload size | 184-504 bits (unchanged) | 184-504 bits (unchanged) |
| Stego embedding | Once per image | Repeated across multiple frames |
| On-chain attestation size | ~137-266 bytes | ~145-274 bytes (+34 bytes for merkleRoot + intervalCount) |
| On-chain gas cost | Same | Same (negligible size difference) |
| IPFS metadata size | ~3-45 KB (with thumbnail) | ~6 KB - 3.3 MB (depends on N and interval) |
| Ledger lookup | By signature prefix | By signature prefix (same) |
| Duplicate detection key | `keccak256(pHash \|\| salt)` | `keccak256(merkleRoot \|\| salt)` |
| Selective verification | N/A (single pHash) | Yes — verify any subset of intervals via Merkle proofs |
| Tamper localization | Binary (authentic or not) | Per-interval (which intervals are tampered) |

## API Extension

The unified API (`stego_sig.h`) would be extended with video-specific functions:

```c
/* Build a Merkle tree from N interval pHashes */
int stego_merkle_build(const uint8_t *phashes, const uint64_t *starts_ms,
                       const uint64_t *ends_ms, int n_intervals,
                       int phash_len, uint8_t *root_out, int *root_len,
                       uint8_t *tree_out, int *tree_len);

/* Get a Merkle proof for interval i */
int stego_merkle_proof(const uint8_t *tree, int tree_len,
                       int interval_index,
                       uint8_t *proof_out, int *proof_len);

/* Verify a single interval against a Merkle root */
int stego_merkle_verify(const uint8_t *root, int root_len,
                        const uint8_t *phash, int phash_len,
                        uint64_t start_ms, uint64_t end_ms, int interval_index,
                        const uint8_t *proof, int proof_len);
```

The signing and verification flows remain the same — `stego_sign()` signs the Merkle root as if it were a pHash, and `stego_verify()` recovers/verifies the Merkle root. The Merkle-specific logic is a layer above the signature layer.

## Live Streaming: Incremental Merkle Tree with Periodic Signing

For pre-recorded video, the full Merkle tree is built after all intervals are known, the root is signed once, and the signature is embedded before publishing. For **live streaming**, the video isn't finished when embedding begins — intervals arrive over time.

### Incremental Merkle Tree

An incremental (append-only) Merkle tree allows leaves to be added one at a time, with the root updated after each insertion. The tree maintains a **frontier** — one hash per tree level representing the "right edge" of the tree so far:

```
After 4 leaves:         After 5 leaves:         Frontier state (5 leaves):
      root                    ?
     /    \                 /   \                 depth 2: h(0-3)  (complete subtree)
   h01    h23             h01   h23               depth 0: l4      (pending leaf)
   / \    / \            / \    / \
  l0 l1  l2 l3         l0 l1  l2 l3  l4
```

The frontier requires only `ceil(log2(N))` hashes of state (~416 bytes for N up to 8192 intervals). The current root can be computed at any time by hashing the frontier elements together.

### Periodic Signing with Configurable Maximum Delay

Rather than signing after every interval (expensive) or only at the end (no real-time verification), we sign the **intermediate Merkle root** every M intervals, with a configurable maximum delay `max_delay_ms`:

```
M = configurable batch size (e.g., 60 intervals)
max_delay_ms = maximum wall-clock time between signatures (e.g., 60000 ms = 1 minute)

For each new interval:
  1. Compute pHash of interval
  2. Append leaf to incremental Merkle tree
  3. If (intervals_since_last_sign >= M) OR (time_since_last_sign >= max_delay_ms):
       a. Compute current intermediate root R_k
       b. Sign R_k: sig_k = stego_sign(sk, R_k)
       c. Embed sig_k in upcoming video frames
       d. Store (R_k, sig_k, interval_range, frontier_state) as checkpoint
       e. Reset counters
```

With variable-length intervals (e.g., scene-based), the `max_delay_ms` ensures signatures happen at a bounded wall-clock rate even if intervals are long.

### Checkpoint Chain

Each signed checkpoint references the previous one, forming a chain:

```
Checkpoint 0: { root_0, sig_0, intervals: [0..M-1], prev: null }
        │
        ▼
Checkpoint 1: { root_1, sig_1, intervals: [0..2M-1], prev: root_0 }
        │
        ▼
Checkpoint K: { root_K, sig_K, intervals: [0..N-1], prev: root_{K-1} }  ← final
```

Each intermediate root `root_k` commits to ALL intervals `[0..k*M-1]` seen so far (not just the latest batch), because the incremental tree is append-only. This means:

- A verifier with checkpoint K can verify ANY interval from 0 to K*M-1
- Earlier checkpoints are subsumed by later ones
- The final checkpoint (after stream ends) is equivalent to the full Merkle tree root

### Cost Impact

**The key insight: only the final checkpoint needs on-chain registration.** Intermediate checkpoints are embedded in the video and stored on IPFS, providing real-time verification during streaming without incurring on-chain gas.

#### On-chain cost: same as pre-recorded video

| Component | Cost | Frequency |
|---|---|---|
| Final root registration (hybrid) | ~$0.07 | Once, after stream ends |
| Final root registration (on-chain) | ~$0.13-0.17 | Once, after stream ends |
| Key registration | ~$0.12-0.14 | Once per key rotation (not per video) |

**Total on-chain cost per video: ~$0.07 (hybrid) or ~$0.13-0.17 (fully on-chain)** — identical to pre-recorded video and still images.

#### Off-chain storage cost

The streaming variant stores additional data: checkpoint signatures and frontier states.

| Component | Size per checkpoint | Notes |
|---|---|---|
| Intermediate signature | 21-63 bytes | Same as the main signature |
| Intermediate root | 32 bytes | SHA-256 |
| Interval range | 8 bytes | Start and end interval indices |
| Frontier state | ~416 bytes (max) | `log2(N)` hashes |
| **Total per checkpoint** | **~480-520 bytes** | |

For a 1-hour stream at 5-second intervals (N=720) with M=60 (sign every 5 minutes):

| Parameter | Value |
|---|---|
| Intervals | 720 |
| Checkpoints | 12 |
| Checkpoint data | 12 × ~500 B = ~6 KB |
| Full metadata (all proofs) | ~260 KB (same as pre-recorded) |
| Checkpoint overhead | **~2.3% of total metadata** |

The checkpoint overhead is negligible.

#### Cost comparison by signing frequency

For a 1-hour stream at 5-second intervals (N=720):

| Signing interval | Max delay | Checkpoints | IPFS overhead | On-chain cost |
|---|---|---|---|---|
| Every interval (M=1) | 5 sec | 720 | ~360 KB | $0.07 (final only) |
| Every 1 min (M=12) | 1 min | 60 | ~30 KB | $0.07 (final only) |
| Every 5 min (M=60) | 5 min | 12 | ~6 KB | $0.07 (final only) |
| Every 10 min (M=120) | 10 min | 6 | ~3 KB | $0.07 (final only) |
| End of stream only | N/A | 1 | ~500 B | $0.07 |

**Signing frequency has zero impact on on-chain cost** (only the final root is registered). It only affects IPFS storage (negligible) and real-time verification latency (how soon a live viewer can verify).

### Real-Time Verification During Streaming

A viewer watching a live stream can verify authenticity in near-real-time:

```
1. Extract latest embedded signature from video frame
2. Recover intermediate root R_k
3. Fetch checkpoint metadata from IPFS or streaming sideband
4. Compute pHash of recent intervals
5. Verify against R_k using Merkle proofs from checkpoint
```

The verification latency is bounded by `max_delay_ms` — the viewer knows the content is authentic up to the most recent signed checkpoint. Any tampering between the last checkpoint and the current playback position is not yet detectable, but will be caught at the next checkpoint.

### Post-Stream Finalization

After the stream ends:

```
1. Compute final Merkle root (from the incremental tree)
2. Sign the final root
3. Register on the ledger (one attestation)
4. Upload full metadata to IPFS:
   - All pHashes with interval metadata
   - Full Merkle tree (all proofs)
   - All checkpoint signatures (for historical verification)
5. Revoke intermediate checkpoints (optional — they're subsumed by the final root)
```

The final root subsumes all intermediate checkpoints. A verifier who encounters the video post-stream uses the final root and full Merkle tree, identical to the pre-recorded video flow.

### Streaming API Extension

```c
/* Incremental Merkle tree state (opaque) */
typedef struct stego_merkle_inc stego_merkle_inc_t;

/* Create a new incremental Merkle tree */
stego_merkle_inc_t *stego_merkle_inc_new(int max_depth, int phash_len);

/* Append a new interval leaf */
int stego_merkle_inc_append(stego_merkle_inc_t *tree,
                            const uint8_t *phash, int phash_len,
                            uint64_t start_ms, uint64_t end_ms);

/* Get the current intermediate root */
int stego_merkle_inc_root(const stego_merkle_inc_t *tree,
                          uint8_t *root_out, int *root_len);

/* Get a Merkle proof for any interval added so far */
int stego_merkle_inc_proof(const stego_merkle_inc_t *tree,
                           int interval_index,
                           uint8_t *proof_out, int *proof_len);

/* Finalize: compute final root, export full tree */
int stego_merkle_inc_finalize(stego_merkle_inc_t *tree,
                              uint8_t *root_out, int *root_len,
                              uint8_t *tree_out, int *tree_len);

/* Free */
void stego_merkle_inc_free(stego_merkle_inc_t *tree);
```

## Open Questions

1. **Interval granularity**: What is the right default interval length? 1 second provides fine-grained tamper detection but large metadata. 5 seconds is a good balance. Scene-based intervals (one pHash per scene change) would be most efficient but require scene detection.

2. **pHash algorithm for video**: Should each interval use the same perceptual hash as still images (e.g., pHash of the middle frame), or a video-specific hash (e.g., average pHash across all frames in the interval, or a motion-aware hash)?

3. **Audio authentication**: The current design only covers the visual track. Audio could be authenticated similarly — perceptual hash of audio segments, included as additional leaves in the Merkle tree.

4. **Re-encoding resilience**: Video re-encoding (transcoding) changes pixel values in every frame, which will change pHashes. The perceptual hash must be robust to common re-encoding parameters (bitrate changes, codec changes). This needs empirical testing per pHash algorithm.
