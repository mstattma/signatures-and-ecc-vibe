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
  "image": "ipfs://Qm.../thumbnail.jpg",
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

contract ImageAuthResolver is SchemaResolver {
    // keccak256(pHash || salt) => attestation UID (0 if none)
    mapping(bytes32 => bytes32) public pHashSaltIndex;

    // sigPrefix => attestation UID (for lookup)
    mapping(bytes16 => bytes32) public sigPrefixIndex;

    constructor(IEAS eas) SchemaResolver(eas) {}

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
        pHashSaltIndex[pHashSaltKey] = attestation.uid;

        // Index by signature prefix for lookup
        sigPrefixIndex[sigPrefix] = attestation.uid;

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

**Gas cost of the resolver**: Adds ~40,000 gas for the two SSTORE operations (index writes). First write to a zero slot costs 20,000 gas each. This is a one-time cost per registration, and the indexes enable O(1) on-chain lookups.

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

### Key Rotation

The stego signing keys (UOV/BLS) are ephemeral and rotated frequently. The blockchain address (ECDSA on secp256k1) serves as the persistent identity.

#### Trust chain

```
Ethereum address (persistent identity)
  └── Signs EAS attestation (tx signature = blockchain key)
        └── Contains stego public key (ephemeral)
              └── Stego signature in image (verified against PK from attestation)
```

No additional key registry contract is needed. The EAS attestation's `attester` field is the Ethereum address that signed the transaction, which is the persistent identity. Each attestation contains the stego PK used for that signing session. The EAS indexer allows querying all attestations by a given attester, providing a history of all stego keys used by that identity.

#### Key rotation workflow

1. Generate new stego key pair `(pk_new, sk_new)`
2. Sign images with `sk_new`, embed signatures
3. Register each image on EAS with `pk_new` in the attestation
4. When rotating: discard `sk_new`, generate `(pk_newer, sk_newer)`
5. No on-chain key rotation transaction needed -- the new PK simply appears in new attestations

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
| Schema registration | ~250,000 | ~$0.25 | One-time, per schema |
| Resolver deployment | ~500,000 | ~$0.50 | One-time |
| Image attestation (no resolver) | ~60,000-80,000 | ~$0.06-0.08 | Per image |
| Image attestation (with resolver) | ~100,000-140,000 | ~$0.10-0.14 | Per image, includes index writes |
| Lookup by sigPrefix (on-chain) | ~2,100 | Free (view) | Per lookup |
| Revoke attestation | ~30,000 | ~$0.03 | For key compromise |

**Estimated cost per image registration: $0.10-0.14 on Base/Optimism.** This includes the resolver's duplicate detection indexes.

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

## Reputation System (Future Work)

The attester's Ethereum address accumulates a history of attestations. A reputation score could be derived from:

- Number of registered images
- Age of the oldest attestation (account longevity)
- Number of verified-correct images (community attestations)
- Absence of revocations (no key compromise history)
- Third-party endorsements (other EAS attestations referencing this attester)

EAS's composability enables this: a "reputation attestation" schema can reference the image authentication schema, building a web of trust.

## Implementation Roadmap

### Phase 1: Schema and Resolver

- Register the EAS schema on Base Sepolia (testnet)
- Deploy the `ImageAuthResolver` contract
- Test duplicate detection and signature-prefix lookup
- Estimated effort: 1-2 days

### Phase 2: Client Integration

- Extend the unified API (`stego_sig.h`) with a registration step
- Implement IPFS metadata upload (thumbnail, EXIF)
- Implement EAS attestation creation via the EAS SDK
- Add pre-registration duplicate check
- Estimated effort: 3-5 days

### Phase 3: Verifier

- Implement signature-prefix lookup via EAS GraphQL
- Integrate PK retrieval and verification
- Add file hash comparison
- Build a simple verification CLI/web tool
- Estimated effort: 3-5 days

### Phase 4: Production Deployment

- Deploy to Base mainnet (or Optimism)
- Register production schema
- Audit resolver contract
- Set up IPFS pinning (Pinata, web3.storage, or self-hosted)
- Estimated effort: 1-2 days

## Cost Projections

| Volume | Monthly cost (Base L2) | Notes |
|---|---|---|
| 100 images/month | ~$10-14 | Hobbyist/individual |
| 1,000 images/month | ~$100-140 | Small organization |
| 10,000 images/month | ~$1,000-1,400 | With batching: ~$800-1,000 |
| 100,000 images/month | ~$10,000-14,000 | Consider off-chain attestations for bulk |

For very high volumes, a hybrid approach using off-chain attestations with periodic on-chain anchoring (Merkle root of a batch) can reduce costs by 10-100x.

## References

- [Ethereum Attestation Service (EAS)](https://attest.org) -- Core protocol
- [EAS Contracts](https://github.com/ethereum-attestation-service/eas-contracts) -- Solidity implementation
- [EAS SDK](https://docs.attest.org/docs/developer-tools/eas-sdk) -- JavaScript/TypeScript SDK
- [ERC-5192: Minimal Soulbound NFTs](https://eips.ethereum.org/EIPS/eip-5192) -- Evaluated, not used
- [EIP-712: Typed Structured Data](https://eips.ethereum.org/EIPS/eip-712) -- Used by EAS for off-chain attestations
- [EIP-4844: Shard Blob Transactions](https://eips.ethereum.org/EIPS/eip-4844) -- Future optimization
- [Base](https://base.org) -- Recommended L2 deployment target
- [IPFS](https://ipfs.io) -- Off-chain storage for thumbnails, EXIF, metadata
