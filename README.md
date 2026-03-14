# Fuzzy Signatures for Steganographic Image Authentication

This project implements compact digital signature schemes for authenticating images through a steganographic channel embedded in the image itself. By signing a **perceptual hash** of the cover image and recovering it via message recovery, the receiver can compute an **authenticity score** -- a form of fuzzy signatures that are robust against minor image modifications.

## How It Works

```
Sender:  image ──► pHash() ──► sign(sk, phash) ──► stego_embed(image, signature) ──► image'

Receiver: image' ──► stego_extract() ──► P(w) = phash_sender
                 ──► pHash(image')   = phash_receiver
                 ──► similarity(phash_sender, phash_receiver) = authenticity score
```

The signature is embedded steganographically in the image. The receiver extracts it, recovers the sender's perceptual hash, and compares it against their own perceptual hash of the received image. Because perceptual hashes are robust against compression, resizing, and other non-malicious modifications, the result is a graduated authenticity score rather than a binary valid/invalid.

## Implementations

| Directory | Scheme | Sig (bits) | PK (bits) | SK (bits) | Payload Size | Classical Security | Quantum Security | Status |
|-----------|--------|-----------|----------|----------|-------------|-------------------|-----------------|--------|
| [UOV/](UOV/) | Oil and Vinegar (80-bit) | 400 | 204,000 | 175,456 | 400 bits (sig only, pHash recovered) | 80 bits | ~40-48 bits (est.) | Working prototype |
| [UOV/](UOV/) | Oil and Vinegar (100-bit) | 504 | 403,200 | 346,056 | 504 bits (sig only, pHash recovered) | 100 bits | ~50-60 bits (est.) | Working prototype |
| [BLS/](BLS/) | BLS (BN-P158) | 168 | 328 | 160 | 280-368 bits (pHash + salt + sig, no PK) | ~78 bits | 0 (broken by Shor) | Working prototype |
| [BLS/](BLS/) | BLS (BLS12-381) | 392 | 776 | 256 | 504-592 bits (pHash + salt + sig, no PK) | ~117-120 bits | 0 (broken by Shor) | Working prototype |

## Unified API

The [unified-api/](unified-api/) directory provides a **scheme-agnostic API** (`stego_sig.h`) that abstracts over all signature schemes. A single `stego_demo.c` works identically with any backend:

```bash
cd unified-api
make test SCHEME=uov-80       # Post-quantum, 400-bit payload
make test SCHEME=bls-bn158    # Classical, 264-352 bit payload
```

### Payload sizes (bits, pHash embedded, no PK)

| pHash | UOV-80 (max 144b) | UOV-100 (max 184b) | BLS-BN158 | BLS12-381 |
|---|---|---|---|---|
| 96-bit | 400 | 504 | **280** | 504 |
| 144-bit | 400 | 504 | **328** | 552 |
| 184-bit | N/A (rejected) | 504 | **368** | 592 |

### Payload sizes (bits, pHash NOT embedded, no PK)

For BLS, the pHash can be omitted from the payload (e.g., looked up from a ledger by signature). The payload then contains only the salt and signature.

| pHash | UOV-80 | UOV-100 | BLS-BN158 | BLS12-381 |
|---|---|---|---|---|
| any | 400 (msg recovery) | 504 (msg recovery) | **184** | **408** |

> **Note:** All schemes include a 2-byte (16-bit) salt. For UOV the salt is embedded in the recovered digest (zero extra payload). For BLS the salt is always transmitted in the payload. UOV-80 can sign pHashes up to **144 bits** (18 bytes); larger pHashes are rejected. Use UOV-100 (max 184 bits) or BLS for larger pHashes.

### Payload sizes (bits, pHash embedded, with PK in-band)

| pHash | UOV-80 (max 144b) | UOV-100 (max 184b) | BLS-BN158 | BLS12-381 |
|---|---|---|---|---|
| 96-bit | 204,400 | 403,704 | **608** | 1,280 |
| 144-bit | 204,400 | 403,704 | **656** | 1,328 |
| 184-bit | N/A | 403,704 | **696** | 1,368 |

## Documentation

- [Unified API](unified-api/) -- Scheme-agnostic signature API with payload size tables
- [UOV Implementation](UOV/) -- Post-quantum signatures with message recovery
- [BLS Implementation](BLS/) -- Classical BLS signatures (BN-P158 and BLS12-381)
- [Scheme Comparison](docs/scheme-comparison.md) -- Analysis of all PQC signature candidates considered
- [Ethereum Ledger Proposal](docs/ethereum-ledger-proposal.md) -- EAS-based on-chain registration with gas cost analysis
- [Ethereum Ledger Implementation](ethereum-ledger/) -- Solidity contracts, deployment scripts, and E2E demo
- [Web UI](ui/) -- Scaffold-ETH 2 contract explorer (`docker compose up` → http://localhost:3000/debug)
- [SE2 UI Customizations](docs/ui-se2-customizations.md) -- All changes made to upstream Scaffold-ETH 2 and the project-specific UI extensions
- [Stardust Stego Demo](docs/stardust-stego-demo.md) -- End-to-end image flow using castLabs Stardust as the stego transport layer
- [Video Extension](docs/video-extension.md) -- Merkle tree approach for authenticating video with per-interval tamper detection

The Web UI includes custom routes:

- `/users` -- registered users discovered from `KeyActivated` events
- `/keys` -- key lifecycle explorer and key management UI
- `/bloom` -- cross-chain duplicate detection explorer
- `/debug` -- raw contract interaction
- `/blockexplorer` -- local transaction explorer with decoded external contract calls

## Quick Start (Docker)

The full stack (Hardhat blockchain node + Scaffold-ETH 2 UI) runs with a single command:

```bash
docker compose up -d             # Start node (port 8545) + UI (port 3000)
docker compose logs -f           # Follow logs
```

- **http://localhost:3000/debug** -- Interactive contract explorer (KeyRegistry, BloomFilter)
- **http://localhost:8545** -- JSON-RPC endpoint for scripts and wallets

The node auto-deploys contracts on startup. The UI auto-generates contract ABIs from the deployment. Next.js compilation cache is persisted across restarts via a Docker volume (first load ~40s, subsequent loads ~2-3s).

Run the E2E demo against the running node:
```bash
cd ethereum-ledger
npx hardhat run scripts/demo.js --network localhost

# Simulate Arbitrum Nova gas pricing:
SIMULATE_NETWORK=arbitrumNova npx hardhat run scripts/demo.js --network localhost
```

See [ethereum-ledger/](ethereum-ledger/) for full setup options (Docker, manual, testnet).

Run the additional image stego transport demo using castLabs Stardust:

```bash
./scripts/stardust_image_demo.sh
```

This flow uses the real BLS-BN158 payload generator from `unified-api/`, embeds the 184-bit `salt || sig` payload into an image using Stardust, extracts it again, and verifies the recovered payload.

## Architecture

### Stego Channel Pipeline

```
[pHash(image)] ──► [sign] ──► [outer RS-ECC] ──► [interleaver] ──► [stego embed]
```

Each implementation provides the signature layer. Both schemes include a 2-byte salt (configurable) to ensure similar images produce different signatures. UOV recovers the pHash from the signature (message recovery, salt embedded in digest); BLS transmits the pHash and salt explicitly alongside the signature. The outer ECC and stego embedding layers are planned future work.

### Ledger-Backed Verification (Planned)

The stego signature provides in-image authentication, but a backend ledger enables out-of-band public key lookup, key rotation, duplicate detection, and richer verification. The full architecture integrates the stego channel with a blockchain or append-only ledger.

#### Signing and Registration Flow

```
                                    ┌─────────────────────────────────┐
                                    │         Ledger / Blockchain      │
                                    │                                  │
  image ──► pHash(image) ──► sign(sk, pHash, salt)                    │
              │                  │                                     │
              │                  ▼                                     │
              │            [signature]                                 │
              │                  │                                     │
              │                  ├──► stego_embed(image, payload)──► image'
              │                  │                                     │
              │                  └──► register_on_ledger() ───────────►│
              │                       │                                │
              │                       ▼                                │
              │                  Ledger Transaction:                   │
              │                  {                                     │
              │                    signature,                          │
              │                    signature_scheme,                   │
              │                    pHash,                              │
              │                    salt,                               │
              │                    public_key,                         │
              │                    file_hash: SHA-256(image'),         │
              │                    file_name,                          │
              │                    file_size,                          │
              │                    thumbnail (optional),               │
              │                    EXIF metadata (optional),           │
              │                    timestamp (ledger-provided),        │
              │                  }                                     │
              │                  signed with blockchain private key    │
              └───────────────────────────────────────────────────────┘
```

#### Verification Flow

```
  image' ──► stego_extract() ──► [signature]
         ──► pHash(image')   ──► [phash_receiver]
                                      │
         ┌────────────────────────────┘
         │
         ▼
  1. Recover/extract phash_sender from signature
  2. Compare similarity(phash_sender, phash_receiver)
         │
         ├── HIGH similarity: image likely authentic
         │         │
         │         ▼
         │   3. (Optional) Look up signature on ledger
         │         │
         │         ▼
         │   4. Retrieve PK, file_hash, thumbnail, metadata
         │   5. Verify signature against retrieved PK
         │   6. Verify file_hash matches SHA-256(image')
         │   7. Compare thumbnail for visual consistency
         │
         └── LOW similarity: image may be modified
                   │
                   ▼
             3. Look up signature on ledger (essential)
             4. Retrieve file_hash, thumbnail, original pHash
             5. Use file_hash and thumbnail to assess
                whether the modification is benign
                (compression, resize) or malicious (tampering)
```

#### Key Design Decisions

**Key rotation.** The stego signing keys (UOV or BLS) have limited security (80-120 bits) due to bandwidth constraints. These keys should be rotated frequently -- potentially per-session or per-batch. The blockchain transaction is signed with a separate, long-lived blockchain private key (e.g., ECDSA on secp256k1 or Ed25519) providing 128+ bit security. The ledger establishes ownership of all stego signing keys: the blockchain key is the identity anchor, and the stego keys are ephemeral.

**Duplicate detection (cross-chain).** The same `(pHash, salt)` pair cannot be registered on any chain. Two layers enforce this:
- **Off-chain indexer (fast path):** Monitors all chains, unified index, sub-second lookups. Trusted but auditable.
- **On-chain Bloom filter (trustless):** Per-chain Bloom filter synced across chains. False positives cause a harmless salt retry. Zero false negatives guaranteed.

The signing process queries both layers before embedding and errors out if a matching `(pHash, salt)` record already exists. This also prevents re-signing a file that already has an embedded signature.

**Signature-based lookup.** The ledger must provide efficient lookup by signature value. Since signatures may be long (up to 504 bits for UOV-100), the index can use a truncated prefix of the signature (e.g., first 80-128 bits) for lookup, with full-length comparison for disambiguation. This enables the verifier to extract a signature from an image and immediately look up the corresponding ledger record.

**Ledger record contents.**

| Field | Required | Purpose |
|---|---|---|
| `signature` | Yes | The stego signature (indexed for lookup) |
| `signature_scheme` | Yes | Scheme identifier (e.g., "uov-80", "bls12-381") |
| `pHash` | Yes | Perceptual hash of the original image |
| `salt` | Yes | Salt used in signing (ensures distinct signatures for same pHash; needed for verification and duplicate detection) |
| `public_key` | Yes | Stego signing PK (enables out-of-band PK distribution) |
| `file_hash` | Yes | SHA-256 of the image after embedding (integrity check) |
| `file_name` | Yes | Original filename |
| `file_size` | Yes | File size in bytes |
| `thumbnail` | Optional | Low-resolution preview for visual verification |
| `EXIF_metadata` | Optional | Camera/device metadata, GPS, etc. |
| `timestamp` | Yes (ledger) | When the record was created (ledger-provided) |
| `blockchain_signature` | Yes (ledger) | Transaction signed with the user's blockchain key |

**Blockchain identity.** The user's blockchain key pair serves as a persistent identity. The blockchain may store additional user information at the user's discretion:
- Display name or pseudonym
- Contact information
- Organization affiliation
- A reputation score (potentially computed from verification history, community feedback, or third-party attestation)

This creates a trust chain: `blockchain identity ──► stego signing key ──► embedded signature ──► authenticated image`.

**Key validity tracking.** The [KeyRegistry](ethereum-ledger/contracts/KeyRegistry.sol) contract tracks the lifecycle of each signing key with `activatedAt` and `revokedAt` timestamps. Registering a new key atomically revokes the previous one. The resolver verifies that a signing key was active at attestation time, preventing use of compromised/revoked keys for new content.

**Reputation system.** The [ReputationRegistry](docs/ethereum-ledger-proposal.md#reputation-system) scores users (0-1000 points) based on account age, image count, community ratings (users + images), verified metadata (email, domain, organization), and key hygiene. See the [Ethereum Ledger Proposal](docs/ethereum-ledger-proposal.md) for the full contract and scoring weights.

**Multi-chain deployment.** Contracts are deployed identically on multiple L2 chains. Users choose based on cost/trust preference:

| Chain | Cost per image (hybrid) | Trust model |
|---|---|---|
| Base / Optimism | ~$0.07 | Full rollup (L1 data availability) |
| Arbitrum Nova | ~$0.007-0.015 | AnyTrust DAC (mitigated by IPFS dual-write) |

See the [Ethereum Ledger Proposal](docs/ethereum-ledger-proposal.md#chain-selection-multi-chain-deployment) for the full multi-chain architecture and Arbitrum Nova trust analysis.

**Business model.** The smart contracts are open-source and permissionless. Revenue from premium reputation analytics, metadata verification oracles (email, domain), enterprise API, white-label deployments, and dispute resolution. See the [proposal](docs/ethereum-ledger-proposal.md#business-model-platform-services) for details.

### Video Extension

The architecture extends to video via a **Merkle tree of perceptual hashes**. A video has N pHashes (one per time interval); the Merkle root is signed once and embedded in the stego channel. The stego payload size is identical to still images. Individual intervals are independently verifiable via Merkle proofs. For live streaming, an incremental Merkle tree with periodic signing provides real-time verification at zero additional on-chain cost. See [docs/video-extension.md](docs/video-extension.md) for the full design.

#### Threat Model with Ledger

| Threat | Mitigation |
|---|---|
| Stego key compromise | Frequent key rotation; compromised key affects only images signed during that key's lifetime; ledger timestamps bound the exposure window |
| Forged signature without ledger record | Verifier checks ledger -- no record means unverified; reputation score reflects this |
| Re-signing a previously signed image | Ledger rejects duplicate (pHash, salt); embedding process queries ledger first |
| Attacker registers forged record | Blockchain signature ties record to identity; reputation system penalizes fraud |
| pHash collision (different image, same hash) | File hash and thumbnail provide independent verification channels |
| Stego channel degradation (low similarity score) | Ledger provides file hash and thumbnail as fallback verification |

## License

See individual subdirectories for license details.
