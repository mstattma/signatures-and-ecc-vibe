# Ethereum Ledger Proposal for Image Authentication

This document proposes an implementation of the verification ledger described in [README.md](../README.md#ledger-backed-verification-planned) using the Ethereum ecosystem, with a focus on minimizing gas costs.

## Executive Summary

We propose using the **Ethereum Attestation Service (EAS)** on an L2 rollup (Base or Optimism) as the ledger backend. EAS provides a general-purpose attestation framework already deployed across multiple chains, with schema registration, on-chain/off-chain attestation modes, and indexing infrastructure. By storing only a compact on-chain attestation and linking to IPFS for bulk data (thumbnails, EXIF), we can register an image authentication record for approximately **$0.01-0.05 USD** on L2.

## Architecture Options Evaluated

### Option A: Custom smart contract (rejected)

A bespoke Solidity contract with `registerImage()` and `lookupBySignature()` functions.

- **Pros**: Maximum control over storage layout and gas optimization
- **Cons**: Must build indexing, UI, and tooling from scratch; no ecosystem interop; audit cost

### Option B: ERC-721 Soulbound Token (considered)

Each signed image minted as a non-transferable NFT ([ERC-5192](https://eips.ethereum.org/EIPS/eip-5192)). Metadata stored via `tokenURI` pointing to IPFS.

- **Pros**: Leverages NFT infrastructure (marketplaces, explorers, wallets); ERC-721 is universally supported
- **Cons**: ERC-721 minting is expensive (~90,000+ gas); soulbound semantics add complexity; `tokenId` doesn't naturally map to signature-based lookup; transferability restrictions require extra logic

### Option C: Ethereum Attestation Service (recommended)

Use the existing [EAS](https://attest.org) protocol. Register a schema for image authentication, create attestations for each signed image.

- **Pros**: Already deployed on Ethereum, Optimism, Base, Arbitrum, and 15+ chains; built-in schema registry, indexer, and GraphQL API; supports on-chain and off-chain modes; ~50,000-80,000 gas per on-chain attestation; open-source SDK; active ecosystem
- **Cons**: Dependency on external protocol (but it's immutable and permissionless)

**We recommend Option C (EAS)** for its combination of low gas cost, existing infrastructure, and multi-chain deployment.

## Proposed Implementation

### Chain Selection: Multi-Chain Deployment

All contracts are deployed identically on multiple L2 chains. The user selects which chain to use based on their cost/trust preference. The same contract ABIs, schemas, and client SDK work across all chains — only the RPC endpoint changes.

#### Supported chains

| Chain | Type | Data availability | Trust model | EAS | Attestation cost | Notes |
|---|---|---|---|---|---|---|
| Ethereum L1 | L1 | On-chain | Ethereum consensus | Deployed | **~$7.20** | Too expensive for per-image; use for L1 anchoring only |
| **Base** | Optimistic rollup (OP Stack) | L1 calldata/blobs | L1 + sequencer (fraud proof fallback) | Pre-deployed `0x4200...0021` | **~$0.01-0.05** | Coinbase-operated; largest retail user base |
| **Optimism** | Optimistic rollup (OP Stack) | L1 calldata/blobs | L1 + sequencer (fraud proof fallback) | Pre-deployed `0x4200...0021` | **~$0.01-0.05** | Optimism Collective; public goods focus |
| **Arbitrum One** | Optimistic rollup (Nitro) | L1 calldata/blobs | L1 + sequencer (fraud proof fallback) | Deployed | **~$0.01-0.05** | Strongest Arbitrum chain; full L1 DA |
| **Arbitrum Nova** | AnyTrust (Nitro + DAC) | **Data Availability Committee** | L1 + DAC (≥2 of ~7 honest) | Deployed | **~$0.001-0.01** | 5-10x cheapest; weaker trust model |

#### Trust model comparison

**Base / Optimism / Arbitrum One** (full rollups):
```
Ethereum L1 consensus (~$50B+ economic security)
  └── All L2 transaction data posted to L1 (calldata or EIP-4844 blobs)
        └── Anyone can reconstruct L2 state independently
              └── Fraud proofs enforceable on L1 (7-day challenge window)
                    └── Our contracts + EAS attestations (fully verifiable from L1)
```

All transaction data is available on Ethereum L1. Even if the sequencer goes down or acts maliciously, anyone can reconstruct the full chain state and submit fraud proofs. This is the strongest trust model short of L1 itself.

**Arbitrum Nova** (AnyTrust):
```
Ethereum L1 consensus
  └── L2 state root posted to L1 (verifiable)
        └── Transaction data held by Data Availability Committee (DAC)
              └── DAC members: Offchain Labs, Consensys, Google Cloud, Quicknode, P2P, Reddit, ...
                    └── Trust: ≥2 of ~7 members must be honest and available
                          └── Our contracts + EAS attestations
```

Transaction data is NOT posted to L1. Instead, the DAC signs a data availability certificate that is posted to L1. If all DAC members collude or go offline, transaction data could become unavailable and fraud proofs could not be constructed.

**Risk assessment for image authentication on Nova:**

| Risk | Impact | Mitigation |
|---|---|---|
| DAC data withholding | Attestations become unverifiable from L1 | IPFS dual-write: full attestation data on IPFS independently |
| DAC collusion (fake attestations) | Forged attestation records | Signatures are cryptographically verifiable with PK alone; doesn't depend on chain |
| DAC downtime | Temporary inability to register new images | Fall back to Base/Optimism; retry when Nova recovers |
| Long-term DAC dissolution | Historical data potentially lost | IPFS + periodic L1 anchoring preserves data independently |

**Nova is acceptable for image authentication** because:
1. No funds are at risk (unlike DeFi)
2. Full attestation data is independently stored on IPFS (hybrid mode)
3. Signatures are verifiable with just the PK — chain is only needed for timestamps, duplicate detection, and reputation
4. Periodic L1 anchoring can provide additional trust for high-value attestations

#### Multi-chain deployment architecture

```
                    ┌─────────────────────────────────────────┐
                    │              Client SDK                  │
                    │  (same ABI, same schema, same logic)     │
                    └───────┬──────────┬──────────┬───────────┘
                            │          │          │
                    ┌───────▼──┐ ┌─────▼────┐ ┌──▼──────────┐
                    │   Base    │ │ Optimism │ │ Arb. Nova   │
                    │ (default) │ │          │ │ (cheapest)  │
                    └───────┬──┘ └─────┬────┘ └──┬──────────┘
                            │          │          │
                    Same contracts deployed on each:
                    - KeyRegistry
                    - ImageAuthResolver (or LightRegistration)
                    - ReputationRegistry
                    - EAS (pre-deployed / deployed)
                            │          │          │
                            └──────────┼──────────┘
                                       │
                              ┌────────▼────────┐
                              │   IPFS / Storacha │ (shared across all chains)
                              └─────────────────┘
                                       │
                              ┌────────▼────────┐
                              │  Ethereum L1     │ (optional periodic anchoring)
                              └─────────────────┘
```

**Key design decisions for multi-chain:**

1. **Same schema UID on all chains.** EAS schemas are registered per-chain, but we use the same schema definition everywhere. The schema UID (a deterministic hash of the schema string + resolver + revocable flag) will be identical if the resolver address differs, so we document the schema definition rather than a specific UID.

2. **IPFS is shared.** The metadataCID stored on-chain points to the same IPFS content regardless of which chain the attestation is on. A verifier on any chain can fetch the same IPFS data.

3. **Duplicate detection is cross-chain.** Two layers work together to prevent the same `(pHash, salt)` from being registered on any chain (see [Cross-Chain Duplicate Detection](#cross-chain-duplicate-detection)):
   - **Primary (fast path):** An off-chain indexer monitors all chains and checks for duplicates before registration. Sub-second latency, trusts the indexer service.
   - **Secondary (trustless):** A Bloom filter of all `(pHash, salt)` commitments is synchronized across chains. False positives cause a salt retry (harmless); false negatives are not possible with proper sizing.

4. **Reputation is per-chain.** Each chain has its own ReputationRegistry. Cross-chain reputation aggregation is a future enhancement (via cross-chain messaging or an off-chain indexer that reads all chains).

5. **Key registry is per-chain.** The user registers their signing key on each chain they use. Key rotation must be performed on each chain independently.

#### Recommended chain per use case

| Use case | Recommended chain | Cost per image (hybrid) | Trust level |
|---|---|---|---|
| **Default / general purpose** | **Base** | ~$0.07 | High (full rollup) |
| **Public goods / grants** | **Optimism** | ~$0.07 | High (full rollup) |
| **High volume / cost-sensitive** | **Arbitrum Nova** | **~$0.007-0.015** | Moderate (AnyTrust DAC) |
| **Legal / forensic evidence** | **Base + L1 anchor** | ~$0.07 + $7/batch | Highest (L1 timestamp) |
| **Maximum decentralization** | **Arbitrum One** | ~$0.07 | High (full rollup, non-Coinbase) |
| **Redundancy** | **Base + Nova** | ~$0.08 combined | High + cheap backup |

#### Hardening for Arbitrum Nova

For production deployment on Nova, these mitigations should be applied:

1. **IPFS dual-write (mandatory):** Always store the full off-chain attestation on IPFS regardless of on-chain success. This makes attestation data independently verifiable even if Nova's DAC fails.

2. **Periodic L1 anchoring (recommended for high-value):** Anchor a Merkle root of a batch of attestations on Ethereum L1. Cost: ~$7 per batch, amortized across hundreds or thousands of images. This provides L1-grade timestamps.

3. **Cross-chain fallback:** If Nova is unavailable, the client automatically falls back to Base or Optimism for registration. The IPFS data is the same either way.

4. **Event-based backup:** An off-chain indexer monitors Nova events and replicates registration data to a secondary store (database, or mirror registrations on a full rollup).

### EAS Schema Design

We register a single schema on EAS that captures the essential on-chain fields. Bulk data (thumbnail, EXIF) is stored off-chain on IPFS and referenced by CID.

#### Schema Definition

```
bytes32 sigPrefix,       // First 16 bytes of the stego signature (lookup index)
bytes signature,         // Full stego signature
uint8 scheme,            // 0=UOV-80, 1=UOV-100, 2=BLS-BN158, 3=BLS12-381
bytes publicKey,         // Stego signing public key (compressed)
bytes24 pHash,           // Perceptual hash (up to 184 bits = 23 bytes, padded)
bytes2 salt,             // Salt used in signing
bytes32 fileHash,        // SHA-256 of the image after stego embedding
bytes32 metadataCID      // IPFS CID (v1, raw) of the metadata JSON
```

**Schema UID**: Computed by EAS as `keccak256(abi.encodePacked(schema, resolverAddress, revocable))`.

#### Why these fields on-chain

| Field | Bytes | Gas (storage) | Rationale |
|---|---|---|---|
| `sigPrefix` | 16 | Indexed topic | Primary lookup key; truncated to 128 bits for gas efficiency |
| `signature` | 21-63 | ~2,000-5,000 | Full signature for verification; variable length |
| `scheme` | 1 | ~200 | Needed to interpret signature and select verification algorithm |
| `publicKey` | 21-97 | ~2,000-8,000 | Enables out-of-band PK lookup; verifier needs this |
| `pHash` | 12-23 | ~1,000-2,000 | Enables verification when pHash not embedded in payload |
| `salt` | 2 | ~200 | Needed for duplicate detection and UOV verification |
| `fileHash` | 32 | ~3,000 | Integrity check; verifier compares against SHA-256(received image) |
| `metadataCID` | 32 | ~3,000 | Pointer to IPFS metadata (thumbnail, EXIF, filename, etc.) |

**Total on-chain data**: ~137-266 bytes depending on scheme. Estimated gas for attestation: **~60,000-100,000 gas**.

#### What goes to IPFS (metadataCID)

The IPFS metadata JSON follows a structure inspired by ERC-721 metadata:

```json
{
  "name": "IMG_20240315_142530.jpg",
  "description": "Authenticated image",
    "image": "ipfs://Qm.../thumbnail.avif",
  "properties": {
    "file_size": 4521983,
    "file_name": "IMG_20240315_142530.jpg",
    "mime_type": "image/jpeg",
    "dimensions": "4032x3024",
    "exif": {
      "camera": "Pixel 8 Pro",
      "focal_length": "6.9mm",
      "iso": 200,
      "gps": "48.1351,11.5820"
    },
    "stego": {
      "scheme": "uov-80",
      "signature_hex": "a1b2c3...",
      "phash_hex": "deef001122...",
      "salt_hex": "f0a1",
      "embed_phash": true,
      "payload_bits": 400
    }
  }
}
```

IPFS storage is free (content-addressed, pinned by any interested party). Only the 32-byte CID is stored on-chain.

### Duplicate Detection

The ledger must enforce that the same `(pHash, salt)` pair cannot be registered twice. We implement this via a **custom EAS resolver contract**.

#### Resolver Contract

```solidity
// SPDX-License-Identifier: MIT
pragma solidity ^0.8.19;

import { SchemaResolver } from "@eas/contracts/resolver/SchemaResolver.sol";
import { IEAS, Attestation } from "@eas/contracts/IEAS.sol";

interface IReputationRegistry {
    function onImageRegistered(address user) external;
}

interface IKeyRegistry {
    function isKeyValidAt(bytes calldata publicKey, uint64 timestamp) external view returns (bool);
}

contract ImageAuthResolver is SchemaResolver {
    // keccak256(pHash || salt) => attestation UID (0 if none)
    mapping(bytes32 => bytes32) public pHashSaltIndex;

    // sigPrefix => attestation UID (for lookup)
    mapping(bytes16 => bytes32) public sigPrefixIndex;

    IReputationRegistry public reputationRegistry;
    IKeyRegistry public keyRegistry;

    constructor(IEAS eas, IReputationRegistry _rep, IKeyRegistry _keys) SchemaResolver(eas) {
        reputationRegistry = _rep;
        keyRegistry = _keys;
    }

    function onAttest(
        Attestation calldata attestation,
        uint256 /* value */
    ) internal override returns (bool) {
        // Decode the attestation data
        (
            bytes16 sigPrefix,
            bytes memory /* signature */,
            uint8 /* scheme */,
            bytes memory /* publicKey */,
            bytes24 pHash,
            bytes2 salt,
            bytes32 /* fileHash */,
            bytes32 /* metadataCID */
        ) = abi.decode(
            attestation.data,
            (bytes16, bytes, uint8, bytes, bytes24, bytes2, bytes32, bytes32)
        );

        // Enforce (pHash, salt) uniqueness
        bytes32 pHashSaltKey = keccak256(abi.encodePacked(pHash, salt));
        if (pHashSaltIndex[pHashSaltKey] != bytes32(0)) {
            return false; // Reject: duplicate (pHash, salt)
        }

        // Verify the signing key is currently valid for this attester
        if (address(keyRegistry) != address(0)) {
            if (!keyRegistry.isKeyValidAt(abi.encodePacked(publicKey), uint64(block.timestamp))) {
                return false; // Reject: signing key not active
            }
        }

        pHashSaltIndex[pHashSaltKey] = attestation.uid;

        // Index by signature prefix for lookup
        sigPrefixIndex[sigPrefix] = attestation.uid;

        // Notify reputation registry
        if (address(reputationRegistry) != address(0)) {
            reputationRegistry.onImageRegistered(attestation.attester);
        }

        return true;
    }

    function onRevoke(
        Attestation calldata /* attestation */,
        uint256 /* value */
    ) internal pure override returns (bool) {
        return true; // Allow revocation (e.g., key compromise)
    }
}
```

**Gas cost of the resolver**: Adds ~65,000-85,000 gas: two SSTORE operations for indexes (20,000 each), one SLOAD + external call for key validity check (~5,000), one external call to reputation registry (~25,000 including the SSTORE for imageCount). This is a one-time cost per registration, and the indexes enable O(1) on-chain lookups.

### Cross-Chain Duplicate Detection

The per-chain resolver enforces `(pHash, salt)` uniqueness on its own chain. To prevent the same `(pHash, salt)` from being registered on a different chain, two complementary layers provide cross-chain deduplication.

#### Layer 1: Off-chain indexer (fast path)

An off-chain indexer service monitors events from all deployed chains and maintains a unified `(pHash, salt)` index. The client queries this indexer before any on-chain registration.

```
Registration flow with indexer:

1. Client computes (pHash, salt) for new image
2. Client queries indexer: "Does keccak256(pHash || salt) exist on ANY chain?"
   └── Indexer checks unified index (Base + Optimism + Nova + ...)
   └── Response in <100ms
3a. "Not found" → proceed with on-chain registration on chosen chain
3b. "Found on Base, attestation UID=0x..." → reject (or retry with new salt)
```

| Property | Value |
|---|---|
| Latency | <100 ms |
| Gas cost | 0 (off-chain query) |
| Trust model | Trust the indexer service (operated by us) |
| Failure mode | If indexer is wrong (allows duplicate): two registrations exist; detectable and correctable post-hoc |
| Implementation | The Graph subgraph or custom indexer reading events from all chains |

The indexer is **auditable**: anyone can independently verify its correctness by replaying events from all chains and reconstructing the index. Discrepancies can be flagged.

#### Layer 2: On-chain Bloom filter (trustless, cross-chain)

Each chain maintains an on-chain Bloom filter of all `keccak256(pHash || salt)` values registered across ALL chains. The Bloom filters are periodically synchronized: each chain publishes its new entries, and all chains absorb entries from the others.

```solidity
// SPDX-License-Identifier: MIT
pragma solidity ^0.8.19;

contract CrossChainBloomFilter {
    // Bloom filter: 2048 bytes = 16384 bits
    // Optimal for ~5000 entries with <1% false positive rate
    // Can be resized by deploying a new contract with a larger filter
    uint256 constant BLOOM_SIZE_BITS = 16384;
    uint256 constant BLOOM_SIZE_WORDS = BLOOM_SIZE_BITS / 256; // 64 words
    uint256 constant NUM_HASHES = 10; // Number of hash functions

    uint256[64] public bloomFilter;
    uint256 public entryCount;

    event BloomUpdated(bytes32 indexed pHashSaltKey);
    event BloomSynced(uint256 newEntries, bytes32 sourceChainId);

    /// @notice Add a (pHash, salt) commitment to the Bloom filter.
    ///         Called by the resolver after a successful registration.
    function add(bytes32 pHashSaltKey) external {
        for (uint256 i = 0; i < NUM_HASHES; i++) {
            uint256 bit = uint256(keccak256(abi.encodePacked(pHashSaltKey, i))) % BLOOM_SIZE_BITS;
            uint256 wordIdx = bit / 256;
            uint256 bitIdx = bit % 256;
            bloomFilter[wordIdx] |= (1 << bitIdx);
        }
        entryCount++;
        emit BloomUpdated(pHashSaltKey);
    }

    /// @notice Check if a (pHash, salt) commitment MIGHT exist.
    ///         Returns true if all bits are set (possible match or false positive).
    ///         Returns false if any bit is unset (definitely not present).
    function mightContain(bytes32 pHashSaltKey) external view returns (bool) {
        for (uint256 i = 0; i < NUM_HASHES; i++) {
            uint256 bit = uint256(keccak256(abi.encodePacked(pHashSaltKey, i))) % BLOOM_SIZE_BITS;
            uint256 wordIdx = bit / 256;
            uint256 bitIdx = bit % 256;
            if (bloomFilter[wordIdx] & (1 << bitIdx) == 0) {
                return false; // Definitely not present
            }
        }
        return true; // Might be present (or false positive)
    }

    /// @notice Sync entries from another chain's Bloom filter.
    ///         Called by an authorized relayer with a batch of new entries.
    function syncFromChain(bytes32[] calldata pHashSaltKeys, bytes32 sourceChainId) external {
        for (uint256 i = 0; i < pHashSaltKeys.length; i++) {
            // Add each entry to the local Bloom filter
            bytes32 key = pHashSaltKeys[i];
            for (uint256 j = 0; j < NUM_HASHES; j++) {
                uint256 bit = uint256(keccak256(abi.encodePacked(key, j))) % BLOOM_SIZE_BITS;
                uint256 wordIdx = bit / 256;
                uint256 bitIdx = bit % 256;
                bloomFilter[wordIdx] |= (1 << bitIdx);
            }
        }
        entryCount += pHashSaltKeys.length;
        emit BloomSynced(pHashSaltKeys.length, sourceChainId);
    }
}
```

**Registration flow with Bloom filter:**

```
1. Client computes pHashSaltKey = keccak256(pHash || salt)
2. Client calls bloomFilter.mightContain(pHashSaltKey) on target chain
3a. Returns false → definitely unique cross-chain → proceed with registration
3b. Returns true → MIGHT be duplicate (or false positive)
    └── Retry with new salt: salt' = salt + 1, recompute pHashSaltKey, go to step 2
    └── Retries are cheap (just a view call) and fast
    └── Expected retries: <1% false positive rate → ~1 retry per 100 registrations
```

**Bloom filter parameters:**

| Parameter | Value | Notes |
|---|---|---|
| Filter size | 2,048 bytes (16,384 bits) | Stored in 64 `uint256` words on-chain |
| Hash functions | 10 | Optimal for target capacity / FP rate |
| Capacity | ~5,000 entries at <1% FP rate | Per filter instance |
| Capacity | ~10,000 entries at ~5% FP rate | Graceful degradation |
| False positive consequence | Salt retry (harmless) | Client generates new salt and retries |
| False negative rate | **0%** (guaranteed) | If entry was added, it will be detected |
| Storage gas (initial deploy) | ~1,300,000 (64 SSTORE) | One-time; ~$1.30 on L2 |
| `add()` gas | ~50,000-80,000 (10 SSTORE updates) | Per registration; amortized via resolver |
| `mightContain()` gas | ~5,000-10,000 (10 SLOAD) | Free (view function) |
| `syncFromChain()` gas | ~50,000 per 10 entries | Batch sync from other chains |

**Scaling**: When the filter approaches capacity, deploy a new contract with a larger filter (e.g., 8,192 bytes for ~20,000 entries at <1% FP). The old filter is kept for reads; new writes go to the new filter. Queries check both filters (OR logic).

**Sync mechanism**: A relayer service (operated by us, but verifiable) periodically reads new `BloomUpdated` events from each chain and calls `syncFromChain()` on all other chains. Sync frequency is configurable — every few minutes is sufficient since the off-chain indexer handles the fast path.

| Sync frequency | Sync gas per chain | Monthly cost (3 chains, 1000 images/month) |
|---|---|---|
| Every 10 minutes | ~5,000 per sync (if no new entries) | ~$2-5 |
| Every hour | ~50,000 per sync (batch of ~10 entries) | ~$0.50-1 |
| Every 6 hours | ~100,000 per sync (batch of ~50 entries) | ~$0.10-0.20 |

#### Combined cross-chain dedup flow

```
Client wants to register image on Arbitrum Nova:

1. Fast path: query off-chain indexer (all chains)
   ├── "Not found" → continue to step 2
   └── "Found" → reject or retry with new salt

2. Trustless check: call bloomFilter.mightContain() on Nova
   ├── false → definitely unique → proceed
   └── true → possible duplicate or false positive
       └── Retry with new salt (go to step 1)

3. Register on Nova (resolver also calls bloomFilter.add())

4. Background: relayer syncs Nova's new Bloom entry to Base and Optimism
```

**Why both layers?**

| Scenario | Indexer | Bloom filter |
|---|---|---|
| Normal operation | Handles 99%+ of dedup checks instantly | Serves as trustless fallback |
| Indexer down | Unavailable | Bloom filter still works (on-chain) |
| Indexer lies (allows dup) | Duplicate passes fast path | Bloom filter catches it at step 2 |
| Bloom false positive | N/A (indexer gave definitive answer) | Client retries with new salt (harmless) |
| Cross-chain race condition | Indexer may have stale data | Bloom sync lag (~minutes) is similar; per-chain resolver is final authority |

The two layers are complementary: the indexer is fast and definitive but trusted; the Bloom filter is trustless but probabilistic and slightly delayed. Together they provide both speed and integrity.

#### Security aspect: perceptual hash collisions

Cross-chain duplicate detection has a security dimension beyond convenience. Because perceptual hashes are similarity-based (not cryptographic), hash collisions between visually different images are possible — especially for shorter hashes (96 bits: ~2^48 collision resistance). The duplicate detection system prevents an attacker from:

1. **Collision exploitation**: Finding two different images with the same pHash and registering both (one legitimate, one malicious)
2. **Cross-chain forgery**: Registering a legitimate image on Base, then registering a different image with the same pHash on Nova to create confusion
3. **Reputation farming**: Registering the same image across multiple chains to inflate image count

The `(pHash, salt)` uniqueness constraint, enforced cross-chain, ensures that each pHash-salt combination is globally unique. If an attacker finds a pHash collision, they can only register it once (with one salt) across all chains. Subsequent attempts with the same pHash require a different salt, creating a detectable audit trail.

### Signature-Based Lookup

The verifier extracts a signature from a stego image and needs to find the corresponding ledger record. Two lookup paths:

#### Path 1: On-chain (via resolver index)

```solidity
// Look up attestation UID by signature prefix (first 16 bytes)
bytes32 uid = resolver.sigPrefixIndex(sigPrefix);
Attestation memory att = eas.getAttestation(uid);
// att.data contains the full record
```

Cost: One `SLOAD` (~2,100 gas) + one EAS read (view function, free).

#### Path 2: Off-chain (via EAS GraphQL API / The Graph)

EAS provides a hosted GraphQL indexer:

```graphql
query {
  attestations(
    where: {
      schemaId: { equals: "0x<schema-uid>" },
      data: { contains: "0x<sig-prefix-hex>" }
    }
  ) {
    id
    attester
    recipient
    time
    data
  }
}
```

Cost: Free (off-chain query). The EAS indexer on Base/Optimism indexes all attestations automatically.

**Recommendation**: Use the off-chain GraphQL path for normal lookups (free, fast). Use the on-chain resolver index as a fallback and for smart contract integrations.

### Key Rotation and Validity Tracking

The stego signing keys (UOV/BLS) are ephemeral and rotated frequently to limit exposure from their relatively low security (80-120 bits). The blockchain address (ECDSA on secp256k1) serves as the persistent identity. A **Key Registry** smart contract tracks the lifecycle of each signing key, establishing validity windows that prevent old keys from being used for new content.

#### Trust chain

```
Ethereum address (persistent identity, 128+ bit security)
  └── Key Registry (on-chain, tracks PK lifecycle)
        └── Signing key #1 (valid from T1 to T2)
        │     └── Image attestations signed during [T1, T2]
        └── Signing key #2 (valid from T2 to T3)
        │     └── Image attestations signed during [T2, T3]
        └── Signing key #N (valid from TN, active)
              └── Current image attestations
```

#### Key Registry Contract

```solidity
// SPDX-License-Identifier: MIT
pragma solidity ^0.8.19;

contract KeyRegistry {
    struct SigningKey {
        bytes publicKey;        // Compressed stego PK
        uint8 scheme;           // 0=UOV-80, 1=UOV-100, 2=BLS-BN158, 3=BLS12-381
        uint64 activatedAt;     // Block timestamp when key was registered
        uint64 revokedAt;       // Block timestamp when key was rotated/revoked (0 = active)
        bytes32 pkHash;         // keccak256(publicKey) for efficient lookup
    }

    // user address => list of signing keys (append-only)
    mapping(address => SigningKey[]) public signingKeys;

    // keccak256(publicKey) => (user address, key index) for PK lookup
    mapping(bytes32 => address) public pkOwner;
    mapping(bytes32 => uint256) public pkIndex;

    // user address => index of currently active key (or type(uint256).max if none)
    mapping(address => uint256) public activeKeyIndex;

    event KeyActivated(address indexed user, uint256 indexed keyIndex, bytes publicKey, uint8 scheme);
    event KeyRevoked(address indexed user, uint256 indexed keyIndex, uint64 revokedAt);

    /// @notice Register a new signing key. Automatically revokes the previous active key.
    function registerKey(bytes calldata publicKey, uint8 scheme) external {
        bytes32 pkh = keccak256(publicKey);
        require(pkOwner[pkh] == address(0), "PK already registered");

        // Revoke previous active key
        uint256 prevIdx = activeKeyIndex[msg.sender];
        if (signingKeys[msg.sender].length > 0 && signingKeys[msg.sender][prevIdx].revokedAt == 0) {
            signingKeys[msg.sender][prevIdx].revokedAt = uint64(block.timestamp);
            emit KeyRevoked(msg.sender, prevIdx, uint64(block.timestamp));
        }

        // Register new key
        uint256 newIdx = signingKeys[msg.sender].length;
        signingKeys[msg.sender].push(SigningKey({
            publicKey: publicKey,
            scheme: scheme,
            activatedAt: uint64(block.timestamp),
            revokedAt: 0,
            pkHash: pkh
        }));

        pkOwner[pkh] = msg.sender;
        pkIndex[pkh] = newIdx;
        activeKeyIndex[msg.sender] = newIdx;

        emit KeyActivated(msg.sender, newIdx, publicKey, scheme);
    }

    /// @notice Explicitly revoke a key (e.g., suspected compromise).
    function revokeKey(uint256 keyIndex) external {
        require(keyIndex < signingKeys[msg.sender].length, "Invalid index");
        require(signingKeys[msg.sender][keyIndex].revokedAt == 0, "Already revoked");
        signingKeys[msg.sender][keyIndex].revokedAt = uint64(block.timestamp);
        emit KeyRevoked(msg.sender, keyIndex, uint64(block.timestamp));
    }

    /// @notice Check if a PK was valid at a given timestamp.
    function isKeyValidAt(bytes calldata publicKey, uint64 timestamp) external view returns (bool) {
        bytes32 pkh = keccak256(publicKey);
        address owner = pkOwner[pkh];
        if (owner == address(0)) return false;
        SigningKey storage k = signingKeys[owner][pkIndex[pkh]];
        return timestamp >= k.activatedAt && (k.revokedAt == 0 || timestamp < k.revokedAt);
    }

    /// @notice Get the number of keys registered by a user.
    function keyCount(address user) external view returns (uint256) {
        return signingKeys[user].length;
    }
}
```

**Gas costs:**

| Operation | Gas | Cost on L2 | Frequency |
|---|---|---|---|
| `registerKey()` (first key) | ~120,000 | ~$0.12 | Once per key rotation |
| `registerKey()` (rotation, revokes previous) | ~140,000 | ~$0.14 | Once per key rotation |
| `revokeKey()` (emergency) | ~30,000 | ~$0.03 | Rare |
| `isKeyValidAt()` (verification) | ~5,000 | Free (view) | Per verification |

#### Key rotation workflow

1. Generate new stego key pair `(pk_new, sk_new)`
2. Call `keyRegistry.registerKey(pk_new, scheme)` — this atomically revokes the previous key and activates the new one with `activatedAt = block.timestamp`
3. Sign images with `sk_new`, embed signatures, register attestations
4. When rotating: discard `sk_new`, repeat from step 1

#### Verification with key validity

When verifying an image attestation, the verifier MUST check that the signing key was valid at the time the attestation was created:

```javascript
const attestation = await eas.getAttestation(uid);
const { publicKey } = decode(attestation.data);
const attestTime = attestation.time;

// Check key was valid when attestation was created
const valid = await keyRegistry.isKeyValidAt(publicKey, attestTime);
if (!valid) {
  // Key was not active at attestation time — possible backdated forgery
  reject();
}
```

This prevents an attacker who compromises an old (revoked) key from creating new attestations that appear legitimate.

### Relevant ERCs and Standards

| Standard | Relevance | Usage |
|---|---|---|
| **EAS** | Core protocol | Attestation creation, schema registry, indexing |
| **ERC-5192** (Soulbound) | Considered, not used | ERC-721 overhead is unnecessary; EAS attestations are already non-transferable |
| **ERC-721** | Considered, not used | Image records are attestations, not tradeable tokens |
| **ERC-1155** | Not relevant | Multi-token; no benefit for unique image records |
| **EIP-712** | Used internally by EAS | Typed structured data signing for off-chain attestations |
| **EIP-4844** (Blobs) | Future optimization | Blob transactions could store bulk image data at ~$0.001/KB; not yet supported by EAS |
| **ERC-7506** (EAS schema) | Informational | Proposed ERC for standardizing EAS schema definitions |

### Gas Cost Summary

| Operation | Gas (L2) | Cost on Base (~$0.001/1000 gas) | Notes |
|---|---|---|---|
| Schema registration | ~250,000 | ~$0.25 | One-time, per schema (need 4+ schemas) |
| Resolver deployment | ~800,000 | ~$0.80 | One-time |
| KeyRegistry deployment | ~600,000 | ~$0.60 | One-time |
| ReputationRegistry deployment | ~1,200,000 | ~$1.20 | One-time |
| BloomFilter deployment | ~1,300,000 | ~$1.30 | One-time per chain; 64 SSTORE for filter |
| Key registration/rotation | ~120,000-140,000 | ~$0.12-0.14 | Per key rotation |
| Image attestation (with resolver + Bloom) | ~180,000-250,000 | ~$0.18-0.25 | Per image; includes indexes + key check + reputation + Bloom add |
| Bloom filter check | ~5,000-10,000 | Free (view) | Per pre-registration dedup check |
| Bloom cross-chain sync | ~50,000 per 10 entries | ~$0.005/entry | Periodic batch sync from other chains |
| Rate a user | ~50,000 | ~$0.05 | One-time per (rater, user) pair |
| Rate an image | ~60,000 | ~$0.06 | One-time per (rater, image) pair |
| Submit user metadata | ~25,000 | ~$0.025 | Per metadata item |
| Lookup by sigPrefix (on-chain) | ~2,100 | Free (view) | Per verification |
| Get reputation score | ~10,000 | Free (view) | Per query |
| Revoke attestation | ~30,000 | ~$0.03 | For key compromise |

**Estimated cost per image registration: $0.18-0.25 on Base/Optimism** (fully on-chain with Bloom filter). With hybrid registration (LightRegistration + Bloom): ~$0.10-0.15. This includes the resolver's duplicate detection, Bloom filter update, key validity check, signature-prefix indexing, and reputation registry notification.

**One-time deployment cost: ~$5.15 per chain** for all contracts and schemas. For 3 chains: ~$15.45 total.

**Cross-chain Bloom sync overhead: ~$0.50-5/month** depending on volume and sync frequency.

### Batch Optimization

EAS supports multi-attestation in a single transaction:

```javascript
const tx = await eas.multiAttest([
  { schema: schemaUID, data: [attestation1, attestation2, ...attestationN] }
]);
```

Batching amortizes the base transaction cost (~21,000 gas) across multiple attestations. For a batch of 10 images, the per-image cost drops to approximately **$0.08-0.10**.

### Off-Chain Attestation Mode

For maximum gas savings, EAS supports **off-chain attestations**: the attestation data is signed with EIP-712 but NOT submitted to the blockchain. The signed attestation can be stored on IPFS, a centralized server, or shared peer-to-peer.

However, with the Key Registry, Reputation Registry, and ImageAuthResolver in place, a **pure off-chain attestation bypasses all on-chain checks**: no duplicate detection, no key validity enforcement, no reputation tracking. To preserve these guarantees while still reducing gas costs, we offer a **hybrid registration** mode.

#### Mode comparison

| Mode | Attestation gas | Checks gas | Total gas | Trust model | Timestamp | Duplicate detection | Key validity | Reputation |
|---|---|---|---|---|---|---|---|---|
| **Fully on-chain** | ~80,000 | ~85,000 (resolver) | **~165,000** | Trustless | Block timestamp | On-chain | On-chain | On-chain |
| **Hybrid: lightweight on-chain registration + off-chain attestation** | 0 (off-chain) | ~70,000 (register call) | **~70,000** | Hybrid | Block timestamp (from register tx) | On-chain | On-chain | On-chain |
| **Off-chain only + on-chain anchor** | 0 | ~40,000 (hash anchor) | **~40,000** | Partial | Block timestamp (from anchor) | **None** | **None** | **None** |
| **Fully off-chain** | 0 | 0 | **0** | Trust storage | Self-reported | **None** | **None** | **None** |

#### Hybrid registration contract

The hybrid mode splits the concerns: the full attestation data lives off-chain (IPFS), but a lightweight on-chain registration provides duplicate detection, key validity enforcement, reputation tracking, and a trusted timestamp — without the gas cost of storing the full attestation data on-chain.

```solidity
// SPDX-License-Identifier: MIT
pragma solidity ^0.8.19;

contract LightRegistration {
    IKeyRegistry public keyRegistry;
    IReputationRegistry public reputationRegistry;

    // keccak256(pHash || salt) => registrant
    mapping(bytes32 => address) public pHashSaltOwner;

    // sigPrefix => keccak256(off-chain attestation)
    mapping(bytes16 => bytes32) public sigPrefixToAttestation;

    event ImageRegistered(
        address indexed attester,
        bytes16 indexed sigPrefix,
        bytes32 pHashSaltKey,
        bytes32 attestationHash,
        bytes32 metadataCID
    );

    /// @notice Register an image with lightweight on-chain checks.
    ///         Full attestation data is stored off-chain (IPFS).
    function registerImage(
        bytes16 sigPrefix,
        bytes24 pHash,
        bytes2 salt,
        bytes calldata publicKey,
        bytes32 attestationHash,  // keccak256(full off-chain attestation)
        bytes32 metadataCID       // IPFS CID of attestation + metadata
    ) external {
        // Enforce (pHash, salt) uniqueness
        bytes32 pHashSaltKey = keccak256(abi.encodePacked(pHash, salt));
        require(pHashSaltOwner[pHashSaltKey] == address(0), "Duplicate (pHash, salt)");

        // Verify the signing key is currently valid
        require(
            keyRegistry.isKeyValidAt(publicKey, uint64(block.timestamp)),
            "Signing key not active"
        );

        // Record registration
        pHashSaltOwner[pHashSaltKey] = msg.sender;
        sigPrefixToAttestation[sigPrefix] = attestationHash;

        // Update reputation
        reputationRegistry.onImageRegistered(msg.sender);

        emit ImageRegistered(msg.sender, sigPrefix, pHashSaltKey, attestationHash, metadataCID);
    }
}
```

**Gas cost: ~65,000-75,000 gas (~$0.065-0.075 on L2)** — roughly half of the full on-chain attestation, while preserving all security checks. The event log contains enough data for off-chain indexers to locate the full attestation on IPFS.

#### Choosing a mode

| Use case | Recommended mode | Cost per image | Guarantees |
|---|---|---|---|
| **Default / standard** | Fully on-chain | ~$0.13-0.17 | All checks, full data on-chain |
| **Cost-sensitive, need checks** | Hybrid registration | **~$0.065-0.075** | All checks, data on IPFS |
| **Cost-sensitive, trust storage** | Off-chain + anchor | ~$0.04 | Trusted timestamp only |
| **Bulk ingestion, post-hoc checks** | Fully off-chain | ~$0.00 | None (verify later) |

The **hybrid mode is the recommended cost-optimized path** for production use. It is ~55% cheaper than fully on-chain while retaining duplicate detection, key validity enforcement, and reputation tracking.

### Off-Chain Attestation Size

An EAS off-chain attestation is an EIP-712 signed struct containing:

| Field | Size | Notes |
|---|---|---|
| EIP-712 domain separator | 32 bytes | Cached, not stored per-attestation |
| Schema UID | 32 bytes | |
| Recipient | 20 bytes | Zero address if no recipient |
| Time | 8 bytes | Signer-reported timestamp |
| Expiration time | 8 bytes | 0 for no expiration |
| Revocable | 1 byte | |
| Reference UID | 32 bytes | 0 if no reference |
| **Attestation data** | **137-266 bytes** | Our schema (variable by scheme) |
| EIP-712 signature (v, r, s) | 65 bytes | ECDSA signature |
| **Total** | **~303-432 bytes** | |

Including JSON wrapping and hex encoding (EAS SDK stores off-chain attestations as JSON), the serialized off-chain attestation is approximately **~1.5-2.5 KB** per image.

### Long-Term Persistent Storage Costs

When using off-chain attestations, the attestation data must be stored somewhere durable. This section covers the cost of storing both the off-chain attestation and the associated metadata (thumbnail, EXIF) on various persistent storage providers.

#### Data budget per image

| Component | Size | Notes |
|---|---|---|
| Off-chain attestation (JSON) | ~1.5-2.5 KB | EIP-712 signed attestation with schema data |
| Metadata JSON | ~0.5-2 KB | Filename, dimensions, EXIF, stego params |
| Thumbnail (optional) | ~15-40 KB | See thumbnail format proposal below |
| **Total without thumbnail** | **~2-4.5 KB** | |
| **Total with thumbnail** | **~18-45 KB** | |

#### Thumbnail format proposal

The thumbnail serves two verification purposes: (1) visual confirmation that the image matches expectations, and (2) detection of visible tampering or content substitution. For these purposes, perfect fidelity is not needed — the thumbnail must be recognizable and show the image's composition, but fine detail is irrelevant.

**Target resolution: ~2.5 megapixels** (e.g., 1920x1300 or equivalent aspect ratio). This is sufficient to see composition, faces, text, and gross modifications while being a significant downscale from modern camera sensors (12-50+ MP).

**Format comparison at 2.5MP (~1920x1300 photographic content):**

| Format | Quality | Typical size | Browser support | Notes |
|---|---|---|---|---|
| **AVIF** | q=20 | **15-30 KB** | Chrome, Firefox, Safari 16.4+ | Best compression; AV1-based; recommended |
| **AVIF** | q=30 | 25-50 KB | Same | Higher quality, still compact |
| WebP | q=20 | 25-50 KB | Universal (all modern browsers) | Good fallback if AVIF unsupported |
| WebP | q=30 | 40-80 KB | Same | |
| JPEG | q=15 | 50-100 KB | Universal | Legacy; 2-3x larger than AVIF at same quality |
| JPEG XL | q=20 | 15-35 KB | Limited (Chrome removed support) | Excellent compression but poor ecosystem support |
| BPG | q=20 | 12-25 KB | None (requires decoder) | Best compression but patent-encumbered, no browser support |

**Recommendation: AVIF at quality 20-25** as primary format, with WebP at quality 20-25 as fallback.

At AVIF q=20:
- Photographic content: ~15-30 KB
- High-detail/text-heavy content: ~25-40 KB
- Average: **~20-35 KB**

This is ~3-5x smaller than equivalent JPEG quality and sufficient for visual verification.

**Thumbnail generation parameters:**

```
# Using libavif / ffmpeg / ImageMagick
# Target: 2.5MP, maintain aspect ratio, AVIF quality 22
ffmpeg -i input.jpg -vf "scale='min(1920,iw)':min(1300,ih):force_original_aspect_ratio=decrease" \
  -c:v libaom-av1 -crf 32 -still-picture 1 thumbnail.avif

# Or with ImageMagick (if AVIF support compiled in)
magick input.jpg -resize 1920x1300\> -quality 22 thumbnail.avif

# WebP fallback
magick input.jpg -resize 1920x1300\> -quality 22 thumbnail.webp
```

**Why not smaller thumbnails?** At lower resolutions (e.g., 320x240 = 77 Kpixels), the thumbnail would be only ~1-3 KB but would not reveal text content, subtle modifications, or fine compositional details. The 2.5MP target strikes a balance: large enough for meaningful visual verification, small enough (~20-35 KB in AVIF) that storage costs remain negligible.

#### Data budget summary (updated with thumbnail sizing)

| Configuration | Size per image | Notes |
|---|---|---|
| **Without thumbnail** | ~2-4.5 KB | Attestation + metadata JSON only |
| **With AVIF thumbnail (q=22, 2.5MP)** | ~22-40 KB | + ~20-35 KB thumbnail |
| **With WebP thumbnail (q=22, 2.5MP)** | ~27-55 KB | + ~25-50 KB thumbnail |
| **With JPEG thumbnail (q=15, 2.5MP)** | ~52-105 KB | + ~50-100 KB thumbnail; not recommended |

#### IPFS and trusted timestamps

IPFS is a content-addressed protocol. CIDs are derived from the data content, not from when the data was added. **IPFS does not provide trusted timestamps natively.** Specifically:

- IPFS nodes do not record or attest to when content was first pinned
- Pinning services (Pinata, Storacha) may record upload timestamps in their internal databases and API responses, but these are **not cryptographically verifiable** and depend on trusting the service
- The off-chain attestation's `time` field is self-reported by the signer (the signer's local clock) and is **not independently verified**

To obtain a trusted timestamp, one of these approaches is needed:
1. **On-chain anchoring** (~40,000 gas): Submit a minimal on-chain transaction containing `keccak256(off-chain attestation)`. The block timestamp provides a tamper-proof time reference. This is the "off-chain + on-chain timestamp" hybrid mode.
2. **Timestamping authorities (TSA)**: Use an RFC 3161 timestamp from a trusted third party (e.g., DigiCert, FreeTSA). This adds ~1-4 KB to the stored data but provides a cryptographically verifiable timestamp without blockchain gas costs.
3. **OpenTimestamps**: An open protocol that anchors timestamps in the Bitcoin blockchain. Free, but timestamps are batched and may take hours to confirm.

#### Storage provider comparison

Prices as of early 2026. All providers store data on IPFS and/or Filecoin.

| Provider | Model | Free tier | Price | Trusted timestamp? | Notes |
|---|---|---|---|---|---|
| **Storacha** (formerly web3.storage) | Monthly subscription | 5 GB free | $0.15/GB/month (Mild); $0.05/GB/month (Medium, $10/mo for 100 GB) | No (upload timestamp in API only) | IPFS hot storage + Filecoin archival; best developer experience |
| **Filebase** | Monthly subscription | 5 GB free | $0.009/GB/month (S3-compatible API) | No | Cheapest monthly; S3 interface makes integration easy |
| **Filecoin direct** | One-time deal | N/A | ~$0.0001/GB/month equivalent | No | Cheapest long-term; higher retrieval latency (~minutes); requires deal-making |
| **NFT.Storage** | One-time fee | N/A | $4.99/GB one-time | No | Endowment-backed long-term preservation; see note below |
| **Self-hosted IPFS** | VPS cost | N/A | ~$5-20/month for a VPS (unlimited storage up to disk) | No (unless you add TSA) | Full control; requires maintenance; use `kubo` (Go-IPFS) |
| **Pinata** | Monthly subscription | 1 GB / 500 files | $20/month for 50 GB (Pro) | No (upload timestamp in API) | Most popular; good API; dedicated gateways |

#### Cost per image by provider

##### Without thumbnail (~3 KB per image: attestation + metadata)

| Volume/month | Cumulative data | Storacha (free) | Filebase (free) | Filecoin direct | Self-hosted |
|---|---|---|---|---|---|
| 100 | 300 KB | **Free** | **Free** | ~$0.00 | ~$5/mo VPS |
| 1,000 | 3 MB | **Free** | **Free** | ~$0.00 | ~$5/mo VPS |
| 10,000 | 30 MB | **Free** | **Free** | ~$0.00 | ~$5/mo VPS |
| 100,000 | 300 MB | **Free** | **Free** | ~$0.00 | ~$5/mo VPS |
| 1,000,000 | 3 GB | **Free** | **Free** | ~$0.00 | ~$10/mo VPS |

Without thumbnails, storage is effectively free at any volume — 1M images per month only accumulates 3 GB, well within free tiers.

##### With AVIF thumbnail (~30 KB per image: attestation + metadata + 2.5MP AVIF)

| Volume/month | Cumulative data | Storacha (free) | Storacha ($10/mo) | Filebase (free) | Filebase (paid) | Filecoin direct | NFT.Storage | Self-hosted |
|---|---|---|---|---|---|---|---|---|
| 100 | 3 MB | **Free** | **Free** | **Free** | **Free** | ~$0.00 | $0.01 one-time | ~$5/mo VPS |
| 1,000 | 30 MB | **Free** | **Free** | **Free** | **Free** | ~$0.00 | $0.15 one-time | ~$5/mo VPS |
| 10,000 | 300 MB | **Free** | **Free** | **Free** | $0.003/mo | ~$0.00 | $1.47 one-time | ~$5/mo VPS |
| 100,000 | 3 GB | **Free** | $0.15/mo | **Free** | $0.03/mo | ~$0.00 | $14.70 one-time | ~$10/mo VPS |
| 1,000,000 | 30 GB | $3.75/mo | $1.50/mo | $0.27/mo | $0.27/mo | ~$0.003/mo | $147 one-time | ~$20/mo VPS |

**Key insight**: Even with 2.5MP AVIF thumbnails, storage costs are negligible compared to on-chain attestation gas at any reasonable volume. The 5 GB free tiers on Storacha and Filebase cover ~170,000 images with thumbnails.

#### NFT.Storage for non-NFT use cases

NFT.Storage was designed for NFT metadata, but its service is not technically restricted to NFTs. It stores any content-addressed data on IPFS + Filecoin with an endowment-backed preservation guarantee. However:

- **Intended use**: NFT.Storage expects you to provide CIDs, blockchain address, contract address, and token IDs. Our EAS attestations don't have a token contract.
- **Workaround**: You could upload the off-chain attestation and metadata as generic IPFS content using NFT.Storage's upload API. The $4.99/GB one-time pricing is attractive for long-term preservation.
- **Risk**: NFT.Storage might restrict non-NFT data in the future, or the endowment model might not cover non-NFT use cases. For production use, Storacha or Filebase are safer choices.
- **With EAS**: An off-chain EAS attestation stored on IPFS via NFT.Storage would be durable but wouldn't have a trusted timestamp. You'd still need on-chain anchoring or a TSA for that.

**Recommendation**: Use **Storacha** (free tier or $10/mo) for general use, or **Filebase** for the cheapest paid option. Use **NFT.Storage** only if long-term endowment-backed preservation is critical and you accept the non-NFT risk. For trusted timestamps, combine any storage provider with on-chain anchoring (~$0.04 per image on L2).

### Combined Cost Summary

#### Without thumbnail (~3 KB per image)

| Configuration | On-chain gas | Storage | Timestamp | Dup detect | Key check | Reputation | Total per image |
|---|---|---|---|---|---|---|---|
| **Fully on-chain** | $0.13-0.17 | ~$0.00 | Trusted | Yes | Yes | Yes | **~$0.13-0.17** |
| **Hybrid registration + Storacha** | ~$0.07 | ~$0.00 | Trusted | Yes | Yes | Yes | **~$0.07** |
| **Hybrid registration + Filebase** | ~$0.07 | ~$0.00 | Trusted | Yes | Yes | Yes | **~$0.07** |
| **Hybrid registration + Filecoin** | ~$0.07 | ~$0.00 | Trusted | Yes | Yes | Yes | **~$0.07** |
| **Hybrid registration + self-hosted** | ~$0.07 | ~$5-20/mo VPS | Trusted | Yes | Yes | Yes | **~$0.07** + VPS |
| **Off-chain + anchor only** | ~$0.04 | ~$0.00 | Trusted | No | No | No | **~$0.04** |
| **Fully off-chain + Storacha** | $0.00 | ~$0.00 | Self-reported | No | No | No | **~$0.00** |

#### With 2.5MP AVIF thumbnail (~30 KB per image)

| Configuration | On-chain gas | Storage | Timestamp | Dup detect | Key check | Reputation | Total per image |
|---|---|---|---|---|---|---|---|
| **Fully on-chain + Storacha** | $0.13-0.17 | ~$0.00 | Trusted | Yes | Yes | Yes | **~$0.13-0.17** |
| **Hybrid registration + Storacha** | ~$0.07 | ~$0.00 | Trusted | Yes | Yes | Yes | **~$0.07** |
| **Hybrid registration + Filebase** | ~$0.07 | ~$0.00 | Trusted | Yes | Yes | Yes | **~$0.07** |
| **Hybrid registration + Filecoin** | ~$0.07 | ~$0.000003 | Trusted | Yes | Yes | Yes | **~$0.07** |
| **Hybrid registration + self-hosted** | ~$0.07 | ~$5-20/mo VPS | Trusted | Yes | Yes | Yes | **~$0.07** + VPS |
| **Off-chain + anchor + Storacha** | ~$0.04 | ~$0.00 | Trusted | No | No | No | **~$0.04** |
| **Off-chain + anchor + Filebase** | ~$0.04 | ~$0.00 | Trusted | No | No | No | **~$0.04** |
| **Fully off-chain + Storacha** | $0.00 | ~$0.00 | Self-reported | No | No | No | **~$0.00** |
| **Fully off-chain + NFT.Storage** | $0.00 | ~$0.00015 | Self-reported | No | No | No | **~$0.0002** |

Storage costs are negligible at all volumes (with or without thumbnails). The dominant cost is on-chain gas. The **hybrid registration** mode preserves all security guarantees at roughly half the gas cost of fully on-chain attestation.

### Recommendation by use case

| Use case | Recommended mode | Cost per image | Dup/Key/Rep checks | Notes |
|---|---|---|---|---|
| **Standard (recommended)** | Hybrid registration + Storacha | **~$0.07** | Yes | Best balance of cost and guarantees |
| **Simplest setup** | Fully on-chain + Storacha | ~$0.13-0.17 | Yes | All data on-chain; no IPFS dependency for attestation data |
| **Organization, batch** | Fully on-chain batch (10) + Storacha | ~$0.10-0.12 | Yes | Batch amortizes base tx cost |
| **High volume, need checks** | Hybrid registration + Filebase | **~$0.07** | Yes | Cheapest with full guarantees |
| **High volume, trust storage** | Off-chain + anchor + Storacha | ~$0.04 | No | Trusted timestamp but no dup/key/rep checks |
| **Bulk ingestion** | Fully off-chain + Storacha | ~$0.00 | No | Post-hoc verification; no on-chain guarantees |
| **Archival** | Hybrid registration + Filecoin | **~$0.07** | Yes | Long-term Filecoin deals for decades |
| **Maximum independence** | Hybrid registration + self-hosted | ~$0.07 + VPS | Yes | No third-party dependencies |

## Signing and Registration Flow

```
1. Sender: image ──► pHash(image) ──► stego_sign(sk, pHash)
                                          │
                                          ▼
                                     [signature]
                                          │
   ┌──────────────────────────────────────┤
   │                                      │
   ▼                                      ▼
   stego_embed(image, payload)       EAS attestation:
        │                            {
        ▼                              sigPrefix: sig[0:16],
   image' (with embedded sig)          signature: sig,
        │                              scheme: "uov-80",
        ▼                              publicKey: pk,
   SHA-256(image') ──────────────►     pHash: pHash,
                                       salt: salt,
                                       fileHash: SHA-256(image'),
                                       metadataCID: <IPFS CID>
                                     }
                                     ──► resolver checks (pHash,salt) uniqueness
                                     ──► attester = sender's Ethereum address
```

### Pre-registration check

Before signing, the client MUST query the resolver:

```javascript
const key = keccak256(abi.encodePacked(pHash, salt));
const existing = await resolver.pHashSaltIndex(key);
if (existing !== ZERO_BYTES32) {
  throw new Error("Duplicate (pHash, salt) -- choose a different salt or abort");
}
```

If the image already has an embedded signature (detected during stego analysis), the process must abort -- re-signing is not allowed.

## Verification Flow

```
1. Receiver: image' ──► stego_extract() ──► [signature, salt]
                    ──► pHash(image')   ──► [phash_receiver]
                                                │
   ┌────────────────────────────────────────────┘
   │
   ▼
2. Compute sigPrefix = signature[0:16]

3. Query EAS (GraphQL or on-chain):
   attestation = lookup(sigPrefix)
      │
      ▼
4. From attestation, retrieve:
   - publicKey (for signature verification)
   - pHash (original, for comparison)
   - fileHash (for integrity check)
   - metadataCID (for thumbnail/EXIF)
   - attester (Ethereum address = identity)
   - timestamp (when registered)

5. Verify:
   a. stego_verify(signature, pHash_from_attestation, publicKey)  ──► valid?
   b. similarity(phash_receiver, pHash_from_attestation)          ──► score
   c. SHA-256(image') == fileHash                                 ──► integrity
   d. (optional) fetch thumbnail from IPFS, visual comparison
   e. (optional) check attester's reputation
```

## Reputation System

The platform tracks reputation at two levels: **user reputation** (the Ethereum address / identity) and **image reputation** (individual attestations). Reputation scoring is implemented via a combination of a smart contract (for on-chain scoring rules and community ratings) and off-chain computation (for complex aggregation).

### Reputation Sources

#### Automated rules (computed on-chain or by indexer)

| Factor | Weight | Source | Notes |
|---|---|---|---|
| Account age | Low-Medium | `keyRegistry.signingKeys[user][0].activatedAt` | Older accounts are more trustworthy |
| Number of registered images | Low | Count of EAS attestations by attester | Activity level |
| Key rotation hygiene | Medium | Number of key rotations vs. account age | Regular rotation suggests security awareness |
| Absence of revocations | Medium | No emergency `revokeKey()` calls | Key compromises reduce trust |
| Attestation consistency | Medium | pHash similarity between claimed and verified images | Automated verification sampling |

#### User-provided metadata (on-chain via EAS)

Users can submit additional attestations about themselves to improve their reputation. These follow a separate EAS schema:

```
bytes32 metadataType,     // keccak256("email"), keccak256("website"), keccak256("organization"), etc.
bytes value,              // The metadata value (encrypted or hashed for privacy)
bytes32 proofCID          // IPFS CID of verification proof (e.g., DKIM proof for email)
```

| Metadata type | Verification method | Trust boost | Notes |
|---|---|---|---|
| **Email address** | DKIM signature proof or verification oracle | Medium | Proves control of email domain |
| **Website/domain** | DNS TXT record or `.well-known` file | Medium | Proves domain ownership |
| **Organization** | EAS attestation from a known org address | High | Third-party endorsement |
| **Social media** | OAuth proof or signed message from platform | Low-Medium | Proves account ownership |
| **Government ID** | KYC attestation from a licensed provider | High | Optional; privacy-sensitive |
| **PGP/GPG key** | Cross-signature with existing PGP key | Medium | Links to existing web of trust |

#### Private key attestation

For higher trust, users can attest to the security properties of their signing environment:

| Attestation | Meaning | Trust boost |
|---|---|---|
| **Hardware key storage** | Private key stored in HSM, TPM, or secure enclave | High |
| **TEE attestation** | Signing performed inside a Trusted Execution Environment (SGX, TrustZone) | High |
| **Multi-sig setup** | Key operations require multiple approvals | Medium |
| **Air-gapped signing** | Key never touches a networked device | Medium |

These attestations are self-reported but can be verified by technical means (e.g., Intel SGX remote attestation, TPM endorsement certificates). The reputation contract records the claim; verification is performed off-chain or by specialized resolver contracts.

#### Community ratings (on-chain)

Users can rate other users and individual images. Ratings are EAS attestations referencing the target:

**User rating schema:**
```
address ratedUser,        // The user being rated
uint8 rating,             // 1-5 stars
bytes32 commentCID        // IPFS CID of optional comment
```

**Image rating schema:**
```
bytes32 imageAttestationUID,  // Reference to the image attestation
uint8 rating,                 // 1-5 stars (authenticity confidence)
bytes32 commentCID            // IPFS CID of optional comment
```

### Reputation Contract

```solidity
// SPDX-License-Identifier: MIT
pragma solidity ^0.8.19;

import { IEAS, Attestation } from "@eas/contracts/IEAS.sol";

contract ReputationRegistry {
    IEAS public immutable eas;
    bytes32 public immutable imageSchema;
    bytes32 public immutable userRatingSchema;
    bytes32 public immutable imageRatingSchema;
    bytes32 public immutable userMetadataSchema;

    struct UserReputation {
        uint32 imageCount;           // Number of registered images
        uint32 userRatingCount;      // Number of ratings received (as user)
        uint64 userRatingSum;        // Sum of ratings (for average)
        uint32 imageRatingCount;     // Number of ratings on their images
        uint64 imageRatingSum;       // Sum of image ratings
        uint32 metadataCount;        // Number of verified metadata items
        uint16 keyRotations;         // Number of key rotations
        uint16 revocations;          // Number of emergency revocations
        uint64 firstSeen;            // Timestamp of first attestation
    }

    mapping(address => UserReputation) public reputation;

    // Per-image rating aggregation
    mapping(bytes32 => uint32) public imageRatingCounts;
    mapping(bytes32 => uint64) public imageRatingSums;

    // Prevent double-rating
    mapping(bytes32 => bool) public hasRated; // keccak256(rater, target) => bool

    event UserRated(address indexed rater, address indexed user, uint8 rating);
    event ImageRated(address indexed rater, bytes32 indexed imageUID, uint8 rating);
    event MetadataSubmitted(address indexed user, bytes32 metadataType);
    event ImageRegistered(address indexed user);

    constructor(
        IEAS _eas,
        bytes32 _imageSchema,
        bytes32 _userRatingSchema,
        bytes32 _imageRatingSchema,
        bytes32 _userMetadataSchema
    ) {
        eas = _eas;
        imageSchema = _imageSchema;
        userRatingSchema = _userRatingSchema;
        imageRatingSchema = _imageRatingSchema;
        userMetadataSchema = _userMetadataSchema;
    }

    /// @notice Called by the image attestation resolver when a new image is registered.
    function onImageRegistered(address user) external {
        if (reputation[user].firstSeen == 0) {
            reputation[user].firstSeen = uint64(block.timestamp);
        }
        reputation[user].imageCount++;
        emit ImageRegistered(user);
    }

    /// @notice Rate a user (1-5). One rating per (rater, user) pair.
    function rateUser(address user, uint8 rating) external {
        require(rating >= 1 && rating <= 5, "Invalid rating");
        bytes32 key = keccak256(abi.encodePacked(msg.sender, user));
        require(!hasRated[key], "Already rated");
        hasRated[key] = true;
        reputation[user].userRatingCount++;
        reputation[user].userRatingSum += rating;
        emit UserRated(msg.sender, user, rating);
    }

    /// @notice Rate an image attestation (1-5). One rating per (rater, image) pair.
    function rateImage(bytes32 imageUID, uint8 rating) external {
        require(rating >= 1 && rating <= 5, "Invalid rating");
        bytes32 key = keccak256(abi.encodePacked(msg.sender, imageUID));
        require(!hasRated[key], "Already rated");
        hasRated[key] = true;

        // Look up the image attestation to credit the attester
        Attestation memory att = eas.getAttestation(imageUID);
        require(att.uid != bytes32(0), "Image not found");

        imageRatingCounts[imageUID]++;
        imageRatingSums[imageUID] += rating;
        reputation[att.attester].imageRatingCount++;
        reputation[att.attester].imageRatingSum += rating;
        emit ImageRated(msg.sender, imageUID, rating);
    }

    /// @notice Record that a user submitted verified metadata.
    function onMetadataSubmitted(address user) external {
        reputation[user].metadataCount++;
        emit MetadataSubmitted(user, bytes32(0));
    }

    /// @notice Record a key rotation event (called by KeyRegistry).
    function onKeyRotated(address user) external {
        reputation[user].keyRotations++;
    }

    /// @notice Record a key revocation event (called by KeyRegistry).
    function onKeyRevoked(address user) external {
        reputation[user].revocations++;
    }

    /// @notice Compute a reputation score (0-1000) for a user.
    ///         This is a simplified on-chain scoring function.
    ///         More sophisticated scoring can be done off-chain.
    function getReputationScore(address user) external view returns (uint256) {
        UserReputation storage r = reputation[user];
        if (r.firstSeen == 0) return 0;

        uint256 score = 0;

        // Account age: up to 200 points (max at 365 days)
        uint256 ageDays = (block.timestamp - r.firstSeen) / 86400;
        score += (ageDays > 365 ? 200 : (ageDays * 200) / 365);

        // Image count: up to 150 points (max at 100 images)
        score += (r.imageCount > 100 ? 150 : (uint256(r.imageCount) * 150) / 100);

        // User rating average: up to 250 points
        if (r.userRatingCount > 0) {
            uint256 avg = (r.userRatingSum * 100) / r.userRatingCount; // avg * 100
            score += (avg * 250) / 500; // scale to 0-250
        }

        // Image rating average: up to 200 points
        if (r.imageRatingCount > 0) {
            uint256 avg = (r.imageRatingSum * 100) / r.imageRatingCount;
            score += (avg * 200) / 500;
        }

        // Metadata: up to 150 points (30 per verified item, max 5)
        uint256 metaPts = uint256(r.metadataCount) * 30;
        score += (metaPts > 150 ? 150 : metaPts);

        // Key hygiene: up to 50 points
        if (r.keyRotations > 0 && r.revocations == 0) {
            score += 50;
        } else if (r.revocations > 0) {
            // Penalize revocations (key compromise)
            uint256 penalty = uint256(r.revocations) * 25;
            score = score > penalty ? score - penalty : 0;
        }

        return score > 1000 ? 1000 : score;
    }
}
```

**Gas costs for reputation operations:**

| Operation | Gas | Cost on L2 | Notes |
|---|---|---|---|
| `rateUser()` | ~50,000 | ~$0.05 | One-time per (rater, user) pair |
| `rateImage()` | ~60,000 | ~$0.06 | One-time per (rater, image) pair |
| `getReputationScore()` | ~10,000 | Free (view) | On-chain scoring |
| `onImageRegistered()` | ~25,000 | Included in resolver | Called by resolver |
| `onMetadataSubmitted()` | ~25,000 | ~$0.025 | Per metadata item |

### Reputation Scoring Weights

The on-chain scoring function above uses the following weight distribution:

| Component | Max points | Weight | Notes |
|---|---|---|---|
| Account age | 200 | 20% | Linear up to 365 days |
| Image count | 150 | 15% | Linear up to 100 images |
| User ratings (average) | 250 | 25% | Community trust |
| Image ratings (average) | 200 | 20% | Content quality |
| Verified metadata | 150 | 15% | Identity strength (30 pts per item, max 5) |
| Key hygiene | 50 | 5% | Bonus for rotation without revocation |
| **Total** | **1000** | **100%** | |

The on-chain scoring is intentionally simple (fits in a view function, no gas cost to query). More sophisticated scoring — incorporating graph analysis, Sybil detection, cross-referencing with external data, or ML-based anomaly detection — can be computed off-chain by an indexer and published as a separate EAS attestation.

### Business Model: Platform Services

The Key Registry, Reputation Registry, and Image Authentication Resolver form a suite of smart contract services that we deploy, maintain, and offer as infrastructure. Revenue opportunities include:

| Service | Model | Notes |
|---|---|---|
| **Image attestation** (resolver) | Free or low per-tx fee | Core service; drives adoption |
| **Key Registry** | Free | Essential infrastructure; builds lock-in |
| **Reputation queries** (on-chain) | Free (view functions) | Public good; drives ecosystem |
| **Premium reputation** (off-chain) | Subscription or per-query fee | Advanced scoring, Sybil detection, graph analysis |
| **Verified metadata** | Per-verification fee | Email verification oracle, domain check, KYC integration |
| **Enterprise API** | SaaS subscription | Batch registration, custom schemas, priority indexing, SLA |
| **White-label** | License fee | Custom deployment of the full stack for organizations |
| **Dispute resolution** | Per-case fee | Mediated resolution for contested image authenticity |

The smart contracts themselves are open-source and permissionless — anyone can interact with them directly. The business model is built on the value-added services around the contracts: user-friendly interfaces, verification oracles, advanced analytics, and enterprise integrations.

## Implementation Roadmap

### Phase 1: Core Contracts and Schema (single chain)

- Deploy `KeyRegistry`, `CrossChainBloomFilter` on Base Sepolia (testnet)
- Register the EAS image authentication schema
- Deploy `ImageAuthResolver` with duplicate detection, Bloom filter, and signature-prefix indexing
- Integrate resolver with `ReputationRegistry.onImageRegistered()` and Bloom filter
- Test key registration, rotation, revocation, and validity checks
- Test duplicate detection (per-chain + Bloom) and signature-prefix lookup
- Estimated effort: 4-6 days

### Phase 2: Reputation System

- Deploy `ReputationRegistry` contract
- Register EAS schemas for user ratings, image ratings, and user metadata
- Implement rating functions and on-chain scoring
- Test scoring weights and edge cases
- Estimated effort: 2-3 days

### Phase 3: Multi-Chain Deployment and Cross-Chain Dedup

- Deploy all contracts to Optimism Sepolia and Arbitrum Nova (testnet)
- Build off-chain indexer for cross-chain `(pHash, salt)` deduplication
- Implement Bloom filter sync relayer (periodic cross-chain sync)
- Test cross-chain duplicate detection: indexer fast path + Bloom fallback
- Test salt retry on Bloom false positives
- Estimated effort: 5-7 days

### Phase 4: Client Integration

- Extend the unified API (`stego_sig.h`) with key registration and image registration steps
- Implement IPFS metadata upload (AVIF thumbnail, EXIF)
- Implement EAS attestation creation via the EAS SDK
- Add pre-registration duplicate check (indexer + Bloom) and key validity check
- Implement chain selection in client SDK
- Integrate key rotation workflow into the signing flow
- Estimated effort: 5-7 days

### Phase 5: Verifier

- Implement signature-prefix lookup via EAS GraphQL (multi-chain)
- Integrate PK retrieval, key validity check, and signature verification
- Add file hash comparison and reputation score display
- Build a verification CLI/web tool with chain auto-detection
- Estimated effort: 3-5 days

### Phase 6: Metadata Verification Services

- Implement email verification oracle (DKIM proof)
- Implement domain verification (DNS TXT / `.well-known`)
- Integrate with EAS user metadata schema
- Connect to reputation scoring
- Estimated effort: 5-7 days

### Phase 7: Production Deployment

- Deploy all contracts to Base, Optimism, and Arbitrum Nova mainnets
- Register production schemas on all chains
- Audit all contracts (KeyRegistry, ReputationRegistry, ImageAuthResolver, CrossChainBloomFilter)
- Set up IPFS pinning (Storacha or Filebase)
- Deploy off-chain indexer and Bloom sync relayer to production
- Estimated effort: 3-5 days

## Cost Projections

### Hybrid registration by chain (recommended — all checks, data on IPFS)

| Volume | Base / Optimism | Arbitrum Nova | Notes |
|---|---|---|---|
| 100 images/month | **~$7** | **~$0.70-1.50** | All checks preserved on both |
| 1,000 images/month | **~$70** | **~$7-15** | |
| 10,000 images/month | **~$700** | **~$70-150** | |
| 100,000 images/month | **~$7,000** | **~$700-1,500** | Nova is 5-10x cheaper |

### Fully on-chain by chain (EAS attestation + resolver with all checks)

| Volume | Base / Optimism | Arbitrum Nova | Notes |
|---|---|---|---|
| 100 images/month | ~$13-17 | ~$1.30-3.50 | |
| 1,000 images/month | ~$130-170 | ~$13-35 | |
| 10,000 images/month | ~$1,000-1,200 | ~$100-240 | With batching |
| 100,000 images/month | ~$10,000-12,000 | ~$1,000-2,400 | With batching |

### Off-chain attestation + on-chain anchor (no dup/key/rep checks)

| Volume | Base / Optimism | Arbitrum Nova | Notes |
|---|---|---|---|
| 100 images/month | ~$4 | ~$0.40-1.00 | No duplicate detection |
| 1,000 images/month | ~$40 | ~$4-10 | |
| 100,000 images/month | ~$4,000 | ~$400-1,000 | |

### Fully off-chain (no on-chain guarantees)

| Volume | Monthly cost | Notes |
|---|---|---|
| Any volume up to 5 GB cumulative | **$0** | Storacha/Filebase free tier; any chain (no chain used) |
| 1M images/month (30 GB cumulative growth) | ~$1-5/month storage | Zero gas; no on-chain checks |

### Multi-chain cost example

A user who deploys on both Base (for trust) and Arbitrum Nova (for cost) and registers each image on the cheaper chain with periodic L1 anchoring:

| Volume | Nova hybrid registration | L1 anchor (monthly batch) | Total |
|---|---|---|---|
| 100 images/month | ~$1 | ~$7 (1 batch) | **~$8** |
| 1,000 images/month | ~$10 | ~$7 (1 batch) | **~$17** |
| 10,000 images/month | ~$100 | ~$28 (4 weekly batches) | **~$128** |
| 100,000 images/month | ~$1,000 | ~$28 (4 weekly batches) | **~$1,028** |

This gives Nova-level costs with L1-grade timestamp anchoring for the entire batch. Individual duplicate detection and key validity checks still run on Nova.

## References

- [Ethereum Attestation Service (EAS)](https://attest.org) -- Core protocol
- [EAS Contracts](https://github.com/ethereum-attestation-service/eas-contracts) -- Solidity implementation
- [EAS SDK](https://docs.attest.org/docs/developer-tools/eas-sdk) -- JavaScript/TypeScript SDK
- [ERC-5192: Minimal Soulbound NFTs](https://eips.ethereum.org/EIPS/eip-5192) -- Evaluated, not used
- [EIP-712: Typed Structured Data](https://eips.ethereum.org/EIPS/eip-712) -- Used by EAS for off-chain attestations
- [EIP-4844: Shard Blob Transactions](https://eips.ethereum.org/EIPS/eip-4844) -- Future optimization
- [Base](https://base.org) -- L2 deployment target (full rollup, Coinbase)
- [Optimism](https://optimism.io) -- L2 deployment target (full rollup, Optimism Collective)
- [Arbitrum Nova](https://nova.arbitrum.io) -- L2 deployment target (AnyTrust, cheapest)
- [Arbitrum One](https://arbitrum.io) -- L2 deployment target (full rollup, Offchain Labs)
- [IPFS](https://ipfs.io) -- Off-chain storage for thumbnails, EXIF, metadata
- [Storacha](https://storacha.network) (formerly web3.storage) -- Decentralized hot storage on IPFS/Filecoin
- [Filebase](https://filebase.com) -- S3-compatible IPFS/Filecoin pinning
- [NFT.Storage](https://nft.storage) -- Endowment-backed long-term NFT preservation ($4.99/GB one-time)
- [Filecoin](https://filecoin.io) -- Decentralized storage network for archival
- [OpenTimestamps](https://opentimestamps.org) -- Free Bitcoin-anchored timestamps
