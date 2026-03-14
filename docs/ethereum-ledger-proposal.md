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

### Layer 1 vs Layer 2

| Chain | Attestation gas | Cost at 30 gwei | Cost at $3000 ETH | Notes |
|---|---|---|---|---|
| Ethereum L1 | ~80,000 | 2,400,000 gwei | **~$7.20** | Too expensive for per-image registration |
| Optimism | ~80,000 | ~0.01 gwei L2 + L1 data | **~$0.01-0.05** | EAS pre-deployed at `0x4200...0021` |
| Base | ~80,000 | ~0.01 gwei L2 + L1 data | **~$0.01-0.05** | EAS pre-deployed at `0x4200...0021` |
| Arbitrum | ~80,000 | ~0.1 gwei L2 + L1 data | **~$0.01-0.05** | EAS deployed |
| Arbitrum Nova | ~80,000 | Ultra-low | **~$0.001-0.01** | Cheapest; AnyTrust chain |

**Recommendation: Base or Optimism** as primary chain. Both are OP Stack L2s with EAS pre-deployed as a system contract, strong ecosystem support, and sub-cent attestation costs. Arbitrum Nova is the cheapest option if cost is the dominant concern.

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
| Schema registration | ~250,000 | ~$0.25 | One-time, per schema (need 4 schemas) |
| Resolver deployment | ~800,000 | ~$0.80 | One-time |
| KeyRegistry deployment | ~600,000 | ~$0.60 | One-time |
| ReputationRegistry deployment | ~1,200,000 | ~$1.20 | One-time |
| Key registration/rotation | ~120,000-140,000 | ~$0.12-0.14 | Per key rotation |
| Image attestation (with resolver) | ~130,000-170,000 | ~$0.13-0.17 | Per image, includes indexes + key check + reputation |
| Rate a user | ~50,000 | ~$0.05 | One-time per (rater, user) pair |
| Rate an image | ~60,000 | ~$0.06 | One-time per (rater, image) pair |
| Submit user metadata | ~25,000 | ~$0.025 | Per metadata item |
| Lookup by sigPrefix (on-chain) | ~2,100 | Free (view) | Per verification |
| Get reputation score | ~10,000 | Free (view) | Per query |
| Revoke attestation | ~30,000 | ~$0.03 | For key compromise |

**Estimated cost per image registration: $0.13-0.17 on Base/Optimism.** This includes the resolver's duplicate detection, key validity check, signature-prefix indexing, and reputation registry notification.

**One-time deployment cost: ~$3.85** for all contracts and schemas.

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

| Mode | Gas cost | Trust model | Timestamp |
|---|---|---|---|
| On-chain | ~100,000-140,000 | Trustless (blockchain consensus) | Block timestamp (tamper-proof) |
| Off-chain | **0** | Requires trust in storage layer | Signer's timestamp (self-reported) |
| Off-chain + on-chain timestamp | ~40,000 | Hybrid (timestamp on-chain, data off-chain) | Trusted timestamp via on-chain tx |

For applications where a trusted timestamp is not critical, off-chain attestations reduce gas to zero. The attestation can be anchored on-chain later if needed.

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

| Configuration | Attestation | Storage | Timestamp | Total per image |
|---|---|---|---|---|
| **On-chain + Storacha (free)** | $0.10-0.14 | ~$0.00 | Trusted (block) | **~$0.10-0.14** |
| **On-chain batch (10) + Storacha** | $0.08-0.10 | ~$0.00 | Trusted | **~$0.08-0.10** |
| **Off-chain + Storacha (free)** | $0.00 | ~$0.00 | Self-reported | **~$0.00** |
| **Off-chain + Storacha + anchor** | ~$0.04 | ~$0.00 | Trusted | **~$0.04** |
| **Off-chain + Filebase (free)** | $0.00 | ~$0.00 | Self-reported | **~$0.00** |
| **Off-chain + Filebase + anchor** | ~$0.04 | ~$0.00 | Trusted | **~$0.04** |
| **Off-chain + Filecoin direct** | $0.00 | ~$0.00 | Self-reported | **~$0.00** |
| **Off-chain + Filecoin + anchor** | ~$0.04 | ~$0.00 | Trusted | **~$0.04** |
| **Off-chain + self-hosted** | $0.00 | ~$5-20/mo VPS | Self-reported | **~$0.00** + VPS |
| **Off-chain + self-hosted + anchor** | ~$0.04 | ~$5-20/mo VPS | Trusted | **~$0.04** + VPS |

Without thumbnails, storage is negligible at all volumes. The dominant cost is always the attestation/anchor gas.

#### With 2.5MP AVIF thumbnail (~30 KB per image)

| Configuration | Attestation | Storage | Timestamp | Total per image |
|---|---|---|---|---|
| **On-chain + Storacha (free)** | $0.10-0.14 | ~$0.00 | Trusted (block) | **~$0.10-0.14** |
| **On-chain batch (10) + Storacha** | $0.08-0.10 | ~$0.00 | Trusted | **~$0.08-0.10** |
| **Off-chain + Storacha (free)** | $0.00 | ~$0.00 | Self-reported | **~$0.00** |
| **Off-chain + Storacha ($10/mo)** | $0.00 | ~$0.000005 | Self-reported | **~$0.00** |
| **Off-chain + Storacha + anchor** | ~$0.04 | ~$0.00 | Trusted | **~$0.04** |
| **Off-chain + Filebase (free)** | $0.00 | ~$0.00 | Self-reported | **~$0.00** |
| **Off-chain + Filebase + anchor** | ~$0.04 | ~$0.00 | Trusted | **~$0.04** |
| **Off-chain + Filecoin direct** | $0.00 | ~$0.000003 | Self-reported | **~$0.00** |
| **Off-chain + Filecoin + anchor** | ~$0.04 | ~$0.000003 | Trusted | **~$0.04** |
| **Off-chain + self-hosted** | $0.00 | ~$5-20/mo VPS | Self-reported | **~$0.00** + VPS |
| **Off-chain + self-hosted + anchor** | ~$0.04 | ~$5-20/mo VPS | Trusted | **~$0.04** + VPS |
| **Off-chain + NFT.Storage** | $0.00 | ~$0.00015 one-time | Self-reported | **~$0.0002** |

Even with 2.5MP AVIF thumbnails, the per-image storage cost never exceeds $0.001. The on-chain attestation or anchor gas remains the dominant cost by 100-1000x.

### Recommendation by use case

| Use case | Recommended configuration | Cost per image | Notes |
|---|---|---|---|
| **Hobbyist, low volume** | On-chain attestation + Storacha (free) | ~$0.10-0.14 | Simplest; trusted timestamp included |
| **Organization, medium volume** | On-chain batch + Storacha (free) | ~$0.08-0.10 | Batch to save on gas |
| **High volume, trusted timestamp needed** | Off-chain + Storacha + on-chain anchor | ~$0.04 | 60-70% gas savings vs full on-chain |
| **High volume, timestamp not critical** | Off-chain + Storacha (free) | ~$0.00 | Zero marginal cost; self-reported time |
| **Archival, long-term preservation** | Off-chain + Filecoin direct + on-chain anchor | ~$0.04 | Cheapest archival; Filecoin deals for decades |
| **Maximum independence** | Off-chain + self-hosted IPFS + on-chain anchor | ~$0.04 + VPS | No third-party dependencies |

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

### Phase 1: Core Contracts and Schema

- Deploy `KeyRegistry` contract on Base Sepolia (testnet)
- Register the EAS image authentication schema
- Deploy `ImageAuthResolver` with duplicate detection and signature-prefix indexing
- Integrate resolver with `ReputationRegistry.onImageRegistered()`
- Test key registration, rotation, revocation, and validity checks
- Test duplicate detection and signature-prefix lookup
- Estimated effort: 3-5 days

### Phase 2: Reputation System

- Deploy `ReputationRegistry` contract
- Register EAS schemas for user ratings, image ratings, and user metadata
- Implement rating functions and on-chain scoring
- Test scoring weights and edge cases
- Estimated effort: 2-3 days

### Phase 3: Client Integration

- Extend the unified API (`stego_sig.h`) with key registration and image registration steps
- Implement IPFS metadata upload (AVIF thumbnail, EXIF)
- Implement EAS attestation creation via the EAS SDK
- Add pre-registration duplicate check and key validity check
- Integrate key rotation workflow into the signing flow
- Estimated effort: 5-7 days

### Phase 4: Verifier

- Implement signature-prefix lookup via EAS GraphQL
- Integrate PK retrieval, key validity check, and signature verification
- Add file hash comparison and reputation score display
- Build a verification CLI/web tool
- Estimated effort: 3-5 days

### Phase 5: Metadata Verification Services

- Implement email verification oracle (DKIM proof)
- Implement domain verification (DNS TXT / `.well-known`)
- Integrate with EAS user metadata schema
- Connect to reputation scoring
- Estimated effort: 5-7 days

### Phase 6: Production Deployment

- Deploy all contracts to Base mainnet (or Optimism)
- Register production schemas
- Audit all contracts (KeyRegistry, ReputationRegistry, ImageAuthResolver)
- Set up IPFS pinning (Storacha or Filebase)
- Estimated effort: 2-3 days

## Cost Projections

### Fully on-chain (attestation + resolver)

| Volume | Monthly cost (Base L2) | Notes |
|---|---|---|
| 100 images/month | ~$10-14 | Hobbyist/individual |
| 1,000 images/month | ~$100-140 | Small organization |
| 10,000 images/month | ~$800-1,000 | With batching |
| 100,000 images/month | ~$8,000-10,000 | With batching |

### Off-chain attestation + on-chain anchor (hybrid)

| Volume | Monthly cost (anchor + storage) | Notes |
|---|---|---|
| 100 images/month | ~$4 + free storage | ~$0.04/image |
| 1,000 images/month | ~$40 + free storage | Storacha/Filebase free tier covers this |
| 10,000 images/month | ~$400 + free storage | Still within free tier for most providers |
| 100,000 images/month | ~$4,000 + ~$1-2/mo storage | 60% cheaper than fully on-chain |

### Off-chain attestation only (no trusted timestamp)

| Volume | Monthly cost | Notes |
|---|---|---|
| Any volume up to 5 GB cumulative | **$0** | Storacha/Filebase free tier |
| 1M images/month (25 GB cumulative growth) | ~$1-5/month storage | Zero gas; self-reported timestamps only |

For very high volumes, a hybrid approach using off-chain attestations with periodic on-chain Merkle root anchoring (one tx per batch) can reduce costs by 100x while still providing trusted timestamps for the entire batch.

## References

- [Ethereum Attestation Service (EAS)](https://attest.org) -- Core protocol
- [EAS Contracts](https://github.com/ethereum-attestation-service/eas-contracts) -- Solidity implementation
- [EAS SDK](https://docs.attest.org/docs/developer-tools/eas-sdk) -- JavaScript/TypeScript SDK
- [ERC-5192: Minimal Soulbound NFTs](https://eips.ethereum.org/EIPS/eip-5192) -- Evaluated, not used
- [EIP-712: Typed Structured Data](https://eips.ethereum.org/EIPS/eip-712) -- Used by EAS for off-chain attestations
- [EIP-4844: Shard Blob Transactions](https://eips.ethereum.org/EIPS/eip-4844) -- Future optimization
- [Base](https://base.org) -- Recommended L2 deployment target
- [IPFS](https://ipfs.io) -- Off-chain storage for thumbnails, EXIF, metadata
- [Storacha](https://storacha.network) (formerly web3.storage) -- Decentralized hot storage on IPFS/Filecoin
- [Filebase](https://filebase.com) -- S3-compatible IPFS/Filecoin pinning
- [NFT.Storage](https://nft.storage) -- Endowment-backed long-term NFT preservation ($4.99/GB one-time)
- [Filecoin](https://filecoin.io) -- Decentralized storage network for archival
- [OpenTimestamps](https://opentimestamps.org) -- Free Bitcoin-anchored timestamps
