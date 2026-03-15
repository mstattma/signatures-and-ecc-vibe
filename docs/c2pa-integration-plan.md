# C2PA Integration Plan

This document describes how we integrate with the [C2PA (Coalition for Content Provenance and Authenticity)](https://c2pa.org) specification v2.3 to make our image authentication system interoperable with the broader content provenance ecosystem.

## Goal

Generate C2PA-compliant manifests with soft binding assertions during our sign flow, store them on IPFS, and expose a Soft Binding Resolution API so that any C2PA-aware client can recover our manifests from watermarked images — even after the manifest metadata has been stripped.

## How C2PA Maps to Our Architecture

| C2PA Concept | Our Equivalent |
|---|---|
| **Soft binding (watermark)** | Stardust watermark payload (WM-ID, 184 bits) |
| **Soft binding (fingerprint)** | DinoHash-96 perceptual hash (deferred) |
| **Hard binding** | SHA-256 file hash (stored in attestation as `fileHash`) |
| **C2PA Manifest** | EAS attestation + C2PA JUMBF manifest stored on IPFS |
| **Manifest Repository** | Our Ethereum ledger (EAS + resolver) + IPFS |
| **Soft Binding Resolution API** | New REST endpoint backed by our resolver |
| **Federated Lookup via DLT** | Our Ethereum contracts (CrossChainBloomFilter + resolver) |
| **Claim Signature (C2PA)** | X.509/CMS signature (for C2PA compliance) |
| **BLS-BN158 signature (ours)** | Fuzzy-signature authentication (separate from C2PA claim sig) |
| **Signer identity** | KeyRegistry + Ethereum address (our chain) / X.509 cert (C2PA) |
| **Durable Content Credential** | Watermark soft binding + IPFS manifest + ledger registration |

## What We Already Have

- Stardust watermark embedding and extraction
- DinoHash-96 perceptual hashing
- BLS-BN158 signature generation and verification
- Ethereum ledger with EAS attestation, resolver, bloom filter, key registry
- Python CLI (`python -m stego sign/verify`)
- Local Docker stack (Hardhat node + UI)

## What We Need to Add

### 1. C2PA Manifest Generation

Use [`c2pa-python`](https://github.com/contentauth/c2pa-python) (v0.28.0, Python bindings to c2pa-rs) to generate JUMBF-serialized C2PA manifests during the sign flow.

Each manifest will contain:

- **`c2pa.soft-binding` assertion**: References the Stardust watermark algorithm (already registered in the [C2PA Soft Binding Algorithm List](https://github.com/c2pa-org/softbinding-algorithm-list)) with our WM-ID payload as the `value` field.
- **`c2pa.watermarked.bound` action**: Required by C2PA whenever a soft binding watermark is applied.
- **`c2pa.hash.data` hard binding**: SHA-256 hash of the output image (matches our `fileHash`).
- **X.509 claim signature**: C2PA requires CMS or COSE signatures with X.509 certificates. We use a **self-signed certificate** for development. Production use requires a certificate from a CA on the C2PA Trust List.

The X.509 claim signature is separate from and in addition to our BLS-BN158 fuzzy signature. The BLS signature serves our perceptual-hash authentication use case; the X.509 signature serves C2PA ecosystem interoperability.

### 2. IPFS Storage

Add a local IPFS node (`kubo`) as a Docker container to the stack. After generating the C2PA manifest, upload it to IPFS and record the CID in the EAS attestation's `metadataCID` field.

- **Development**: Local IPFS node in Docker Compose. CIDs are valid but only accessible locally.
- **Production**: Add Storacha/Filebase pinning for global accessibility and durability.

### 3. Soft Binding Resolution API

Implement a REST endpoint in `signatures-and-ecc-vibe/c2pa-resolution/` following the [C2PA Soft Binding Resolution API OpenAPI spec](https://c2pa.org/specifications/specifications/2.2/softbinding/Decoupled.html).

#### Endpoints

| Route | Method | Purpose | Backend |
|---|---|---|---|
| `/matches/byBinding` | GET/POST | Query by Stardust WM-ID + algorithm name → manifest IDs | Resolver `sigPrefixIndex` lookup |
| `/matches/byContent` | POST | Upload image → extract watermark → query → manifest IDs | Stardust extract + resolver lookup |
| `/manifests/{manifestId}` | GET | Fetch C2PA manifest by ID | IPFS fetch using CID from attestation `metadataCID` |
| `/services/supportedAlgorithms` | GET | List supported algorithms | Static: Stardust watermark (now), DinoHash fingerprint (later) |

**One endpoint handles both watermark and fingerprint queries.** The `alg` parameter in `/matches/byBinding` identifies which algorithm the query value belongs to. When we eventually register DinoHash as a fingerprint algorithm, we add it to the same endpoint's supported algorithms list.

#### Technology

Python FastAPI service, containerized in Docker Compose alongside the Hardhat node and UI.

### 4. DLT Federated Lookup

Our Ethereum contracts already implement a key-value store pattern (sig prefix → attestation UID). To make this discoverable by C2PA clients:

- Add a C2PA-compatible smart contract method that returns the JSON schema describing our K-V store format (as specified in [C2PA Soft Binding API Section 2.2](https://c2pa.org/specifications/specifications/2.2/softbinding/Decoupled.html#_schema_for_decentralized_key_value_store)).
- The `softBindingResolutionApis` field in the Stardust algorithm list entry will include both:
  - Our HTTP resolution endpoint URL
  - Our Ethereum contract address in CAIP-10 format (e.g., `eip155:84532:0xDc64...`)

## C2PA Soft Binding Assertion Format

The `c2pa.soft-binding` assertion we generate:

```cbor
{
  "alg": "<stardust-registered-alg-name>",
  "blocks": [
    {
      "scope": {},
      "value": <184-bit WM-ID payload as CBOR bstr>
    }
  ]
}
```

The `alg` value comes from Stardust's entry in the [Soft Binding Algorithm List](https://github.com/c2pa-org/softbinding-algorithm-list). The `value` is our `salt || signature` payload (23 bytes) encoded as a CBOR byte string.

## Updated Sign Flow

```
1.  Load image
2.  Compute DinoHash-96 pHash
3.  Generate 1-byte salt
4.  Sign pHash with BLS-BN158 → payload (salt || sig, 184 bits)
5.  Embed payload via Stardust → watermarked image
6.  Store sidecar YUV files
7.  Compute fileHash = SHA-256(output image)
8.  NEW: Generate C2PA manifest via c2pa-python:
      - c2pa.soft-binding assertion (Stardust alg + WM-ID value)
      - c2pa.watermarked.bound action
      - c2pa.hash.data hard binding (fileHash)
      - Sign with self-signed X.509 certificate
9.  NEW: Upload C2PA manifest to IPFS → get CID
10. Register on ledger: EAS attestation with metadataCID = IPFS CID
11. Output summary
```

## Updated Verify Flow

```
1.  Extract Stardust watermark from image
2.  Parse salt || signature
3.  Look up on ledger by sig prefix → attestation
4.  Verify BLS signature against PK from attestation
5.  Compute receiver pHash, compare Hamming distance
6.  NEW: Fetch C2PA manifest from IPFS using metadataCID
7.  NEW: Validate C2PA manifest (soft binding, hard binding, signature)
8.  Output verification result + C2PA validation status
```

## Recovery Flow (C2PA ecosystem interop)

When a third-party C2PA client encounters a watermarked image without a manifest:

```
1.  Client detects Stardust watermark in the image
2.  Client looks up Stardust's algorithm entry in the C2PA Soft Binding Algorithm List
3.  Entry contains softBindingResolutionApis pointing to our endpoint
4.  Client queries our /matches/byBinding with alg + WM-ID value
5.  Our endpoint queries the resolver by sig prefix → returns manifest ID
6.  Client fetches /manifests/{manifestId}
7.  Our endpoint fetches C2PA manifest from IPFS and returns it
8.  Client validates the C2PA manifest normally
```

## Implementation Steps

### Step 1: Add `c2pa-python` dependency and IPFS container

- Add `c2pa-python` to `perceptual-fuzzy-hash-test-vibe/requirements.txt`
- Add IPFS (kubo) container to `signatures-and-ecc-vibe/docker-compose.yml`
- Test IPFS upload/fetch from Python

### Step 2: Generate C2PA manifest during sign

- Create `perceptual-fuzzy-hash-test-vibe/stego/c2pa_manifest.py`
- Generate self-signed X.509 certificate (or use c2pa-python's test cert)
- Build manifest with soft binding assertion, watermarked.bound action, hard binding
- Serialize to JUMBF bytes

### Step 3: Upload manifest to IPFS

- Create `perceptual-fuzzy-hash-test-vibe/stego/ipfs.py`
- Upload serialized manifest to local IPFS node
- Return CID

### Step 4: Update sign flow

- After Stardust embed, generate C2PA manifest → IPFS → record CID in `metadataCID`
- Update verify flow to fetch and display C2PA manifest from IPFS

### Step 5: Implement Soft Binding Resolution API

- Create `signatures-and-ecc-vibe/c2pa-resolution/` (Python FastAPI)
- Implement all 4 OpenAPI routes
- Back with our resolver (view calls) + IPFS (manifest fetch)
- Add to Docker Compose

### Step 6: Add DLT-compatible smart contract method

- Add a method to ImageAuthResolver or a new contract that returns C2PA K-V store JSON schema
- Register in the Stardust algorithm list's `softBindingResolutionApis`

### Step 7: Test end-to-end

- Sign image → embed → C2PA manifest → IPFS → ledger
- Query resolution API by watermark value → recover manifest
- Validate recovered manifest
- Third-party C2PA tool can also recover and validate

### Step 8: Document

- Update README, AGENTS.md, and relevant docs in both repos

## Decisions Made

| Decision | Choice | Rationale |
|---|---|---|
| C2PA manifest library | `c2pa-python` v0.28.0 | Python bindings to c2pa-rs; actively maintained; supports sign/verify/assertions |
| X.509 signing | Self-signed certificate | Sufficient for dev/testing; production needs CA from C2PA Trust List |
| IPFS | Local kubo node in Docker | No external dependency; real CIDs; production adds Storacha/Filebase pinning |
| Resolution API | Python FastAPI in `signatures-and-ecc-vibe/c2pa-resolution/` | Alongside the ledger; implements C2PA OpenAPI spec |
| Resolution endpoint count | One endpoint for all algorithms | C2PA API is algorithm-agnostic; `alg` parameter dispatches |
| Watermark algorithm registration | Stardust (already registered) | No action needed |
| Fingerprint algorithm registration | Deferred | Still determining implementation |

## Deferred Items

- DinoHash fingerprint algorithm registration in C2PA Soft Binding Algorithm List
- Production X.509 certificate from C2PA Trust List CA
- Storacha/Filebase IPFS pinning for global manifest accessibility
- `softBindingResolutionApis` update in the Stardust algorithm list (requires coordination with castLabs)
- C2PA manifest validation in the verify flow (fetch + validate, not just store)
- `/matches/byContent` endpoint (requires running Stardust extraction server-side)

## References

- [C2PA Technical Specification v2.3](https://c2pa.org/specifications/specifications/2.3/specs/C2PA_Specification.html)
- [C2PA Soft Binding Resolution API](https://c2pa.org/specifications/specifications/2.2/softbinding/Decoupled.html)
- [C2PA Soft Binding Algorithm List](https://github.com/c2pa-org/softbinding-algorithm-list)
- [c2pa-python](https://github.com/contentauth/c2pa-python) (Python bindings to c2pa-rs)
- [c2pa-rs](https://github.com/contentauth/c2pa-rs) (Rust reference implementation)
