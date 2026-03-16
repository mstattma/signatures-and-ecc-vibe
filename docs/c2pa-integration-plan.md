# C2PA Integration Plan

This document describes how we integrate with the [C2PA (Coalition for Content Provenance and Authenticity)](https://c2pa.org) specification v2.3 to make our image authentication system interoperable with the broader content provenance ecosystem.

## Goal

Generate C2PA-compliant manifests with soft binding assertions during our sign flow, embed them in the output image, store them on IPFS for recovery after metadata stripping, and expose a Soft Binding Resolution API so that any C2PA-aware client can recover our manifests from watermarked images.

Critically, the system uses a **dual-key binding architecture** to cryptographically link the C2PA manifest to the on-chain EAS attestation, preventing manifest-swap attacks even if the Ethereum wallet key is compromised.

## How C2PA Maps to Our Architecture

| C2PA Concept | Our Equivalent |
|---|---|
| **Soft binding (watermark)** | Stardust watermark payload (WM-ID, 184 bits) |
| **Soft binding (fingerprint)** | DinoHash-96 perceptual hash (deferred) |
| **Hard binding** | SHA-256 file hash (stored in attestation as `fileHash`) |
| **C2PA Manifest** | JUMBF manifest embedded in image + stored on IPFS |
| **Manifest Repository** | Our Ethereum ledger (EAS + resolver) + IPFS (kubo) |
| **Soft Binding Resolution API** | FastAPI endpoint in `c2pa-resolution/` |
| **Federated Lookup via DLT** | `ImageAuthResolver.c2paLookup()` + `c2paSchema()` |
| **Claim Signature (C2PA)** | ES256 (P-256) with self-signed X.509 certificate |
| **BLS-BN158 signature (ours)** | Fuzzy-signature authentication embedded via Stardust |
| **Signer identity** | KeyRegistry (BLS + C2PA P-256 keys) + Ethereum address |
| **Durable Content Credential** | Watermark soft binding + IPFS manifest + ledger attestation |

## Dual-Key Binding Architecture

### Problem

The system uses three independent signing keys:

1. **Ethereum wallet key** (secp256k1) — signs EAS attestation transactions on-chain
2. **C2PA signing key** (ES256 / P-256) — signs the C2PA manifest claim
3. **BLS-BN158 key** — signs the perceptual hash, embedded via Stardust watermark

Without explicit binding, an attacker who compromises the Ethereum wallet key could:
- Create a new BLS key, sign a new image, register it under the stolen identity
- Attach an old C2PA manifest (from the legitimate user) to the new attestation
- The receiver sees a valid EAS attestation + a valid C2PA manifest, but they belong to different images

### Solution: Cross-Signatures Over Binding Values

Both the Ethereum key and the C2PA key sign the same binding values: `(pHash || pHashVersion || salt)`. These signatures go into both the C2PA manifest and the EAS attestation.

**`ethSig`** = `personal_sign(keccak256(pHash || pHashVersion || salt))` using the Ethereum wallet key
- Proves the wallet key holder authorized this specific pHash+salt combination
- Stored in the C2PA manifest's `stego.imageauth` metadata assertion
- Implicitly verified on-chain (the attester signed the EAS transaction containing these values)

**`c2paSig`** = ES256 signature over `sha256(pHash || pHashVersion || salt)` using the C2PA P-256 private key
- Proves the C2PA key holder authorized this specific pHash+salt combination
- Stored in both the C2PA manifest AND the EAS attestation data
- **Verified on-chain by the ImageAuthResolver** during `onAttest` using P-256 signature verification

The on-chain verification of `c2paSig` is the key defense: an attacker with only the Ethereum wallet key cannot produce a valid `c2paSig` (they don't have the C2PA private key), so the resolver rejects the attestation.

### Cryptographic Binding Graph

```
Ethereum wallet key (secp256k1)
  |-- signs EAS attestation tx (on-chain, contains pHash, salt, c2paSig, c2paCertHash, metadataCID)
  \-- signs ethSig over (pHash || pHashVersion || salt) --> stored in C2PA manifest

C2PA signing key (ES256 / P-256)
  |-- signs C2PA claim (covers all assertions in the manifest)
  \-- signs c2paSig over sha256(pHash || pHashVersion || salt) --> stored in manifest AND on-chain

BLS-BN158 signing key
  \-- signs pHash --> embedded in image via Stardust watermark, registered on-chain (sigPrefix)
```

### Binding Verification

**Forward (attestation -> manifest):**
The EAS attestation contains `metadataCID` which is an immutable IPFS content identifier (CID). The CID is a hash of the content — it cannot be changed without producing a different CID. So the attestation is cryptographically bound to the exact manifest bytes stored on IPFS.

**Reverse (manifest -> attestation):**
The C2PA manifest's `stego.imageauth` assertion contains `ethSig` — the Ethereum wallet signature over the binding values. The receiver can `ecrecover` this to obtain the attester's Ethereum address, then look up the attestation by the EAS UID (also available in the manifest) or by `pHashSaltIndex`.

**Cross-key binding:**
Both keys sign the same `(pHash || pHashVersion || salt)` tuple. The on-chain resolver verifies `c2paSig` at attestation time. An attacker needs **both** keys to produce a consistent attestation + manifest pair.

### Attack Scenarios

| Attack | Defense |
|---|---|
| Attacker has Ethereum key, attaches old C2PA manifest to new attestation | Resolver verifies `c2paSig` — attacker can't produce valid ES256 sig without C2PA key. Attestation rejected. |
| Attacker has Ethereum key, creates new C2PA manifest with own C2PA key | Receiver sees `c2paCertHash` on-chain doesn't match the legitimate user's registered cert. Mismatch detected. |
| Attacker has C2PA key but not Ethereum key | Can't create EAS attestation (requires Ethereum tx signature). |
| Attacker modifies C2PA manifest on IPFS | Impossible — IPFS is content-addressed. Different content = different CID. |
| Same pHash+salt reused across attestations | `pHashSaltIndex` dedup check rejects duplicates on-chain. |

## C2PA Key Management

### P-256 Key and Certificate Registration

The C2PA signing key (ES256 / P-256) and its X.509 certificate are managed through the KeyRegistry contract:

```solidity
struct C2PAKey {
    uint256 pubKeyX;      // P-256 public key X coordinate (32 bytes)
    uint256 pubKeyY;      // P-256 public key Y coordinate (32 bytes)
    bytes32 certHash;     // sha256(DER certificate bytes)
    bytes32 certCID;      // IPFS CID of full X.509 certificate (for retrieval)
    uint64 activatedAt;
    uint64 revokedAt;
}
```

- The P-256 public key coordinates are stored on-chain (64 bytes total) for signature verification
- The full X.509 certificate is stored on IPFS (too large for on-chain)
- `certHash = sha256(DER certificate bytes)` is the lookup key
- Registration is one-time per identity, alongside BLS key registration
- C2PA key revocation follows the same pattern as BLS key revocation

### P-256 Signature Verification On-Chain

The ImageAuthResolver verifies `c2paSig` during `onAttest`:

1. Look up `(pubKeyX, pubKeyY)` from KeyRegistry using `c2paCertHash`
2. Compute `digest = sha256(pHash || pHashVersion || salt)` from the attestation data
3. Verify ES256 signature `c2paSig` over `digest` against the public key
4. Reject attestation if verification fails

**Implementation strategy:**
- Use [RIP-7212](https://github.com/ethereum/RIPs/blob/master/RIPS/rip-7212.md) P-256 precompile (address `0x100`) on chains that support it (Base, Optimism, Arbitrum) — costs ~3,450 gas
- Use a Solidity P-256 library (e.g., [daimo p256-verifier](https://github.com/daimo-eth/p256-verifier) or [FCL](https://github.com/rdubois-crypto/FreshCryptoLib)) on chains without the precompile (including local Hardhat) — costs ~200-300K gas
- The resolver auto-detects: try RIP-7212 first, fall back to Solidity library

### Why SHA-256 for the c2paSig Digest

ES256 is defined as ECDSA-on-P-256-with-SHA-256. We use standard `sha256(pHash || pHashVersion || salt)` as the digest to stay spec-compliant. This means any standard ES256 verifier can check the `c2paSig` without modification. The Solidity `sha256()` precompile costs 60 + 12 gas per word — negligible. (Using `keccak256` would also work cryptographically but would be non-standard.)

## EAS Attestation Schema

### Updated Schema

```
sigPrefix       bytes16     First 16 bytes of BLS signature (lookup index)
signature       bytes       Full BLS-BN158 signature
scheme          uint8       Signature scheme (0=UOV-80, 1=UOV-100, 2=BLS-BN158, 3=BLS12-381)
publicKey       bytes       BLS public key
pHash           bytes24     Perceptual hash (DinoHash-96 = 12 bytes, zero-padded to 24)
pHashVersion    uint16      Hash algorithm version (enables algorithm evolution)
salt            bytes2      Dedup salt (1 byte used, zero-padded to 2)
fileHash        bytes32     SHA-256 of the output image
metadataCID     bytes32     IPFS CID of the manifest directory (raw SHA-256 digest from CIDv0)
c2paCertHash    bytes32     sha256(DER certificate bytes) — identifies the C2PA signing key
c2paSig         bytes       ES256 signature over sha256(pHash || pHashVersion || salt)
fileName        string      Original image filename
```

New fields vs previous schema: `c2paCertHash` (32 bytes), `c2paSig` (~64 bytes).

This is a **breaking schema change** — requires a new schema UID. Old attestations use the old schema. Clean slate for development.

### What the Resolver Verifies in `onAttest`

1. **Dedup**: `keccak256(pHashVersion, pHash, salt)` not in `pHashSaltIndex` (existing)
2. **BLS key validity**: `keyRegistry.isKeyCurrentlyValid(publicKey)` (existing)
3. **C2PA signature**: ES256 verify `c2paSig` over `sha256(pHash || pHashVersion || salt)` against the P-256 public key looked up from `c2paCertHash` (new)
4. **Index updates**: `pHashSaltIndex`, `sigPrefixIndex` (existing)
5. **Bloom filter**: `bloomFilter.add(pHashSaltKey)` (existing)

## C2PA Manifest Structure

The C2PA manifest is embedded in the output image (JUMBF metadata, does not modify pixel data) AND uploaded to IPFS for recovery after metadata stripping.

### Assertions

```json
{
  "claim_generator": "stego-cli/1.0",
  "assertions": [
    {
      "label": "c2pa.soft-binding",
      "data": {
        "alg": "castlabs.stardust",
        "blocks": [{ "scope": {}, "value": "<184-bit WM-ID payload hex>" }]
      }
    },
    {
      "label": "c2pa.hash.data",
      "data": { "name": "sha256", "hash": "<SHA-256 of output image hex>" }
    },
    {
      "label": "stego.imageauth",
      "data": {
        "pHash": "<perceptual hash hex>",
        "pHashVersion": 1,
        "salt": "<salt hex>",
        "ethSig": "<Ethereum wallet signature over keccak256(pHash || pHashVersion || salt)>",
        "c2paSig": "<ES256 signature over sha256(pHash || pHashVersion || salt)>"
      }
    }
  ]
}
```

### Actions

- `c2pa.watermarked.bound` — records that a Stardust watermark was applied with a BLS-BN158 authentication payload

### Claim Signature

ES256 (P-256) using a self-signed X.509 certificate (development). Production requires a certificate from a CA on the [C2PA Trust List](https://contentcredentials.org/trust-list).

The claim signature covers all assertions, including `stego.imageauth`. This means the C2PA key implicitly signs the binding values (pHash, pHashVersion, salt) and the ethSig/c2paSig.

## IPFS Storage

### Directory Structure

Manifests are stored as IPFS directories (rather than single files, to allow co-locating additional metadata):

```
<CID>/
  manifest.c2pa     # C2PA manifest store (JUMBF binary)
  metadata.json      # Extended metadata (full-length hashes, Stardust params, etc.)
```

The directory CID is immutable (IPFS is content-addressed — all CIDs, including directory CIDs, are the hash of their content). The metadata.json file allows storing additional data that would bloat the C2PA manifest or exceed on-chain storage limits without a schema change.

### CID On-Chain Representation

`metadataCID` in the attestation is a `bytes32` containing the raw SHA-256 digest extracted from the CIDv0 multihash. Conversion:

- CIDv0 = `base58(0x12 || 0x20 || sha256_digest)` — 46-character base58 string
- On-chain: store just the 32-byte `sha256_digest`
- Reconstruct CIDv0: prepend `0x12 0x20`, base58-encode

### IPFS is Not Searchable

IPFS is a content-addressed retrieval system, not a search engine. You can only fetch data by CID. All discoverability comes from the on-chain indexes (`sigPrefixIndex`, `pHashSaltIndex`) or the Soft Binding Resolution API. IPFS is purely the dumb storage layer.

## Manifest Recovery: Resolution API vs DLT K-V Store

The C2PA spec defines two parallel mechanisms for recovering a manifest from a soft binding value (e.g., an extracted watermark payload). Both ultimately answer the same question: "given this watermark, where is the C2PA manifest?"

### The Two Paths

**Path A: HTTP Soft Binding Resolution API** — a web service that accepts a soft binding value and returns the manifest. The client must know the endpoint URL ahead of time (e.g., from the SBAL, hardcoded, or discovered out-of-band).

```
Client → GET /matches/byBinding?alg=castlabs.stardust&value=<base64>
       ← { matches: [{ manifestId: "0xabc..." }] }
Client → GET /manifests/0xabc...
       ← application/c2pa manifest bytes
```

**Path B: DLT K-V Store (federated discovery)** — a smart contract that maps soft binding values to manifest IDs. The client doesn't need to know any HTTP endpoint to perform the lookup; it queries the blockchain directly. However, it still needs an HTTP endpoint to fetch the actual manifest bytes (smart contracts can't serve files).

```
Client → c2paLookup("castlabs.stardust", <binding bytes>) on-chain
       ← attestation UID (manifest ID)
Client → uses manifest ID with a known /manifests endpoint, or
         discovers the endpoint from the SBAL or c2paSchema() output
```

### Semantic Relationship

`/matches/byBinding` and `c2paLookup()` are **semantically identical** — both map a soft binding value to a manifest ID. The DLT K-V store is the decentralized, trustless alternative to the HTTP endpoint for the *lookup* step. The HTTP endpoint can be an opaque service run by anyone; the DLT K-V store is verifiable on-chain.

However, the DLT K-V store alone is **not sufficient** for full manifest recovery — it returns a manifest ID, not the manifest bytes. The client still needs a service implementing `/manifests/{id}` to fetch the actual C2PA manifest content. In our case, that service reads `metadataCID` from the EAS attestation and fetches from IPFS.

### How We Implement Both

#### HTTP Resolution API (`c2pa-resolution/`)

Python FastAPI service implementing the [C2PA Soft Binding Resolution API OpenAPI spec](https://c2pa.org/specifications/specifications/2.2/softbinding/Decoupled.html#soft-binding-resolution-api).

Location: `signatures-and-ecc-vibe/c2pa-resolution/`

| Route | Method | Purpose | Backend |
|---|---|---|---|
| `/matches/byBinding` | GET/POST | Query by Stardust WM-ID → manifest IDs | Resolver `sigPrefixIndex` on-chain lookup |
| `/matches/byContent` | POST | Upload image → extract watermark → query (deferred) | Stardust extraction + resolver |
| `/manifests/{manifestId}` | GET | Fetch C2PA manifest store by attestation UID | `EAS.getAttestation(uid)` → decode `metadataCID` → IPFS fetch `{CID}/manifest.c2pa` |
| `/services/supportedAlgorithms` | GET | List supported algorithms | Static: `castlabs.stardust` |
| `/health` | GET | Service health check | Ledger connection + deployment check |

**Query format** (per C2PA OpenAPI spec):
- `alg` (string): Algorithm name from the SBAL — `castlabs.stardust`
- `value` (string): **Base64-encoded** binding value — the full Stardust WM-ID payload (23 bytes: `bls_salt(2B) || signature(21B)`)
- `maxResults` (integer, optional): Maximum number of results (default 10)

**Response**: `c2pa.softBindingQueryResult` with `matches[].manifestId` (EAS attestation UID as hex string) and `matches[].similarityScore` (100 for exact sig prefix match).

**Internal flow for `/matches/byBinding`**: base64-decode the value → extract sig prefix from bytes [2:18] → call `sigPrefixIndex[sigPrefix]` on the ImageAuthResolver contract → return the attestation UID as `manifestId`.

**Internal flow for `/manifests/{manifestId}`**: hex-decode the UID → call `EAS.getAttestation(uid)` → decode attestation data → extract `metadataCID` (field index 8) → convert bytes32 to CIDv0 → IPFS fetch `{CID}/manifest.c2pa` → return as `application/c2pa`.

**`/services/supportedAlgorithms`** returns the list of soft binding algorithms this endpoint can handle. Per the C2PA OpenAPI spec, the response is a `c2pa.softBindingAlgList` with separate `watermarks` and `fingerprints` arrays. Our endpoint returns:

```json
{
  "watermarks": [{ "alg": "castlabs.stardust" }],
  "fingerprints": []
}
```

This is the reverse of the SBAL: the SBAL maps algorithms → endpoints ("which endpoints support this algorithm?"), while `/services/supportedAlgorithms` maps endpoint → algorithms ("which algorithms does this endpoint support?"). A C2PA client that already knows the endpoint (e.g., from the SBAL or configuration) can call this to confirm the endpoint supports the algorithm it needs before sending a binding query. When we add DinoHash as a registered fingerprint algorithm, it would appear in the `fingerprints` array.

#### DLT K-V Store (ImageAuthResolver)

The `ImageAuthResolver` contract implements the DLT side with two lookup functions:

- **`c2paLookup(string alg, bytes bindingValue) → bytes32`** — native Solidity interface for direct callers (resolution API, other contracts). Takes Solidity types, returns raw `bytes32` attestation UID.

- **`c2paLookupJSON(string inputJSON) → string`** — C2PA spec-conformant interface. Per Section 2.2, the function "must accept and return a single JSON string." Input: `{"softBindingValue":"<base64>"}`. Output: `{"endpoints":[],"manifestId":"0x<hex UID>"}`. Includes on-chain base64 decoding and JSON construction. The `endpoints` array is empty because endpoint discovery happens via the SBAL; clients combine the `manifestId` with a known `/manifests` endpoint.

- **`c2paSchema() → string`** — returns a JSON array conforming to the [C2PA DLT K-V store schema (Section 2.2)](https://c2pa.org/specifications/specifications/2.2/softbinding/Decoupled.html#_schema_for_decentralized_key_value_store). Points to `c2paLookupJSON` as the `byBindingFunctionName`. Input/output schemas are proper JSON Schema objects:

```json
[{
  "resolutionMethod": "smartContract",
  "querySmartContract": {
    "smartContractAddress": "eip155:31337:0x1234...abcd",
    "byBindingFunctionName": "c2paLookupJSON",
    "byBindingInputSchema": {
      "type": "object",
      "properties": {
        "softBindingValue": { "type": "string", "description": "base64-encoded soft binding value" }
      },
      "required": ["softBindingValue"],
      "additionalProperties": false
    },
    "byBindingOutputSchema": {
      "type": "object",
      "properties": {
        "endpoints": { "type": "array", "items": { "type": "string", "format": "uri" } },
        "manifestId": { "type": "string" }
      },
      "required": ["endpoints"],
      "additionalProperties": false
    }
  }
}]
```

The `smartContractAddress` is in [CAIP-10](https://github.com/ChainAgnostic/CAIPs/blob/main/CAIPs/caip-10.md) format (`eip155:{chainId}:{address}`), dynamically computed from `block.chainid` and `address(this)`.

**Why two functions**: `c2paLookup` is efficient for Solidity-to-Solidity calls and our resolution API (no JSON overhead). `c2paLookupJSON` exists solely for C2PA spec conformance — generic C2PA clients discovering us via the SBAL can call it without knowing our native ABI. The JSON string interface adds gas cost (base64 decoding + string manipulation on-chain) but is only used for DLT federated lookup, not high-frequency operations.

### Discovery via the Soft Binding Algorithm List (SBAL)

The [SBAL](https://github.com/c2pa-org/softbinding-algorithm-list) is the authoritative registry of C2PA soft binding algorithms. Each entry has an optional `softBindingResolutionApis` field — an array of URI strings.

**Key finding**: The SBAL schema uses a single `"format": "uri"` array for both HTTP endpoints and smart contract addresses. There is no `type` discriminator field. A C2PA client must inspect the URI format to determine which it is:
- `https://...` → HTTP resolution API endpoint
- `eip155:...` → CAIP-10 smart contract address (call `c2paSchema()` to learn the interface)

**Current state of the SBAL** (as of March 2026): 28 algorithms are registered (Digimarc, Adobe Trustmark, castLabs, Microsoft InvisMark, Steg.AI, ISCC, etc.). **None of them have populated `softBindingResolutionApis`**. The field is optional and no vendor has published a resolution endpoint or smart contract address yet. DLT federated lookup is entirely theoretical at this point.

castLabs Stardust is registered as `castLabs.watermark.1` (identifier 12) with `decodedMediaTypes: ["video"]` only. Our usage is for images. A future SBAL update would need to either add `"image"` to the castLabs entry or register a separate entry.

### Planned SBAL Registration (Future)

When we deploy to a public chain (e.g., Base), our `softBindingResolutionApis` entry would contain both:

```json
"softBindingResolutionApis": [
    "https://resolver.imageauth.example/v1",
    "eip155:8453:0xDc64a140Aa3E981100a9becA4E685f962f0cF6C9"
]
```

The HTTPS endpoint is the resolution API (simpler, faster). The CAIP-10 address is the on-chain contract (decentralized, trustless, auditable). A C2PA client can use either; the HTTPS endpoint is preferred when available, the DLT fallback provides resilience if the service goes down.

This registration requires coordination with castLabs (who own the Stardust SBAL entry) and is deferred to post-testnet deployment.

## On-Chain Lookup Paths

The system provides multiple independent lookup paths, all free (view calls):

| Lookup Path | Key | How Obtained | Returns |
|---|---|---|---|
| `sigPrefixIndex[sigPrefix]` | 16 bytes from BLS signature | Extracted from Stardust watermark | Attestation UID |
| `pHashSaltIndex[keccak256(pHashVersion, pHash, salt)]` | Computed from pHash + salt | Recomputed from image + assertion data | Attestation UID |
| `bloomFilter.mightContain(key)` | Same as pHashSaltIndex | Same | Boolean (probabilistic) |
| `EAS.getAttestation(uid)` | Attestation UID | From any of the above | Full attestation record |

The Bloom filter is for cross-chain dedup pre-filtering — returns boolean only, no UID.

## Sign Flow

```
 1.  Load image
 2.  Compute DinoHash-96 pHash
 3.  Generate 1-byte salt
 4.  Sign pHash with BLS-BN158 → payload (bls_salt || sig, 184 bits)
 5.  Check ledger for (pHash, salt) duplicates, retry salt if needed
 6.  Register BLS key on KeyRegistry (if not already)
 7.  Register C2PA key on KeyRegistry (if not already)
       → registerC2PAKey(pubKeyX, pubKeyY, certHash, certCID)
 8.  Embed watermark via Stardust → watermarked PNG
 9.  Compute fileHash = SHA-256(watermarked image)
10.  Generate ethSig = personal_sign(keccak256(pHash || pHashVersion || salt))
11.  Generate c2paSig = ES256_sign(sha256(pHash || pHashVersion || salt))
12.  Generate C2PA manifest via c2pa-python:
       - c2pa.soft-binding assertion (Stardust alg + WM-ID value)
       - c2pa.hash.data hard binding (fileHash)
       - stego.imageauth assertion (pHash, pHashVersion, salt, ethSig, c2paSig)
       - c2pa.watermarked.bound action
       - Embed manifest in output image (JUMBF metadata, does not modify pixels)
13.  Upload to IPFS as directory (manifest.c2pa + metadata.json) → get CID
14.  Register EAS attestation with all fields:
       sigPrefix, signature, scheme, publicKey, pHash, pHashVersion, salt,
       fileHash, metadataCID, c2paCertHash, c2paSig, fileName
       → Resolver verifies: dedup + BLS key + c2paSig → accept/reject
       → Returns attestation UID
15.  Validate with c2patool (optional, if installed)
16.  Output summary
```

### Ordering Rationale

The C2PA manifest is generated **before** the EAS attestation (steps 12-13 before step 14) because:
- The manifest must exist on IPFS before the attestation so `metadataCID` is populated
- The manifest does NOT contain the EAS UID (which doesn't exist yet)
- The link from manifest → attestation is via `ethSig` in the `stego.imageauth` assertion (ecrecover → attester address → pHashSaltIndex lookup)
- The link from attestation → manifest is via `metadataCID` (immutable IPFS CID)

EAS computes the attestation UID during the `attest()` transaction (deterministic hash of attestation content). The UID is not known until the transaction completes. The manifest cannot contain the UID because it must be finalized and uploaded to IPFS before the attestation.

## Verify Flow

```
 1.  Extract Stardust watermark from image → payload
 2.  Parse bls_salt || signature, compute sig prefix (bytes [2:18])
 3.  Look up attestation on ledger by sig prefix → full attestation record
 4.  Verify BLS signature against PK from attestation
 5.  Compute receiver pHash, compare Hamming distance with ledger pHash
       → expect non-zero distance if image was transformed
 6.  Fetch C2PA manifest from IPFS using metadataCID
 7.  Validate C2PA manifest:
       - Verify claim signature (ES256)
       - Check soft binding assertion (Stardust alg + WM-ID matches extracted payload)
       - Check hard binding (SHA-256 — will fail if image was transformed, expected)
 8.  Verify stego.imageauth assertion:
       - Verify ethSig: ecrecover → should match attester address from attestation
       - Verify c2paSig: P-256 verify against cert from certHash lookup on KeyRegistry
       - Compare pHash, pHashVersion, salt with on-chain attestation values
 9.  Look up attester reputation from ReputationRegistry
10.  Output verification summary
```

## Recovery Flow (C2PA ecosystem interop)

When a third-party C2PA client encounters a watermarked image without a manifest:

```
1.  Client detects Stardust watermark in the image
2.  Client looks up Stardust's algorithm entry in the C2PA Soft Binding Algorithm List
3.  Entry contains softBindingResolutionApis pointing to our endpoint
4.  Client queries our /matches/byBinding with alg + base64(WM-ID)
5.  Our endpoint base64-decodes the value, extracts sig prefix from bytes [2:18]
6.  Queries resolver by sig prefix → returns attestation UID as manifestId
7.  Client fetches /manifests/{manifestId}
8.  Our endpoint reads metadataCID from the attestation, fetches from IPFS
9.  Returns application/c2pa response
10. Client validates the C2PA manifest normally
```

## End-to-End Demo

A standalone demo script (`demo_e2e.py`) orchestrates the full flow:

### Sender Side
1. Download test image from picsum.photos
2. Execute the full sign flow (steps 1-16 above)
3. Validate with `c2patool` if available
4. Print sender summary

### Simulate Social Media Transform
5. Use ffmpeg to recompress to JPEG quality 50
6. Use ffmpeg to make asymmetric 60% crop: `crop=iw*0.6:ih*0.8:iw*0.15:ih*0.1`
7. Confirm C2PA manifest is stripped (ffmpeg doesn't recognize JUMBF metadata)
8. Print transform summary (file size change, resolution change)

### Receiver Side
9. Extract Stardust watermark from transformed image
10. Parse payload, look up attestation on ledger
11. Verify BLS signature
12. Compute receiver pHash, compare with ledger pHash → expect non-zero Hamming distance
13. Query `/matches/byBinding` on C2PA Resolution API
14. Fetch C2PA manifest from `/manifests/{uid}` (pulls from IPFS)
15. Verify `stego.imageauth` assertion (ethSig + c2paSig)
16. Look up attester reputation
17. Print receiver summary: hard binding invalid (expected), soft binding valid, pHash similarity score, reputation data

## Implementation Phases

### Phase 1: C2PA Key Infrastructure (Contracts)

| Item | File(s) | Description |
|---|---|---|
| 1.1 | `KeyRegistry.sol` | Add `C2PAKey` struct, `registerC2PAKey()`, `revokeC2PAKey()`, `getC2PAKey()` |
| 1.2 | `ImageAuthResolver.sol` | Add P-256 signature verification in `onAttest` (Solidity library + RIP-7212 detection) |
| 1.3 | All schema-dependent files | Update attestation schema: add `c2paCertHash`, `c2paSig` fields |
| 1.4 | `deploy.js` | Deploy updated contracts, register new schema |

### Phase 2: C2PA Manifest with Dual Signatures (Python)

| Item | File(s) | Description |
|---|---|---|
| 2.1 | `stego/c2pa_manifest.py` | Embed manifest in image, add `stego.imageauth` assertion with ethSig + c2paSig |
| 2.2 | `stego/c2pa_keys.py` (new) | ES256 key pair management, X.509 cert generation, P-256 signing, pub key coordinate extraction |
| 2.3 | `stego/ledger.py` | Add `sign_binding()` for ethSig generation |

### Phase 3: Updated CLI Flows (Python)

| Item | File(s) | Description |
|---|---|---|
| 3.1 | `stego/cli.py` | Rewrite `cmd_sign` with new flow (dual sigs, embedded manifest, IPFS dir, c2paSig in attestation) |
| 3.2 | `stego/cli.py` | Rewrite `cmd_verify` with new flow (c2paSig/ethSig checks, reputation lookup) |
| 3.3 | `stego/ledger.py` | Update `ATTESTATION_TYPES`, `AttestationRecord`, `register_image()`, add `register_c2pa_key()`, `get_reputation()` |
| 3.4 | `stego/ipfs.py` | Add `upload_directory()` for IPFS directory uploads |

### Phase 4: Resolution API Updates

| Item | File(s) | Description |
|---|---|---|
| 4.1 | `c2pa-resolution/app.py` | Base64 encoding (per C2PA spec), updated schema, `maxResults`, `similarityScore` |

### Phase 5: End-to-End Demo

| Item | File(s) | Description |
|---|---|---|
| 5.1 | `demo_e2e.py` | Standalone script: sender sign → ffmpeg transform → receiver verify + resolution API query |

### Phase 6: Documentation

| Item | File(s) | Description |
|---|---|---|
| 6.1 | `docs/c2pa-integration-plan.md` | This document (already updated) |
| 6.2 | READMEs, AGENTS.md (both repos) | Updated schema, new KeyRegistry methods, new deps, demo instructions |
| 6.3 | `docs/ethereum-ledger-proposal.md` | ReputationRegistry, C2PA key registration, P-256 verification gas costs |

### Phase 7: Deferred / Lower Priority

| Item | Description |
|---|---|
| 7.1 | Perceptual hash benchmark execution (framework exists, needs dataset + run) |
| 7.2 | Base Sepolia testnet deployment (needs funded wallet; RIP-7212 available on Base) |
| 7.3 | `/matches/byContent` endpoint (requires server-side Stardust extraction) |

### Future Investigation

- **pHashVersion not embedded in watermark or manifest.** The `pHashVersion` field exists in the attestation schema, allowing hash algorithm evolution (e.g., from DinoHash-96 to a future algorithm). However, the receiver currently has no way to discover which hash algorithm to use for local pHash recalculation — the version is only available after the ledger lookup. Two approaches to consider:
  1. **Embed pHashVersion in the Stardust payload** — would require reducing salt or signature space (tight at 184 bits already), or increasing Stardust capacity.
  2. **Include pHashVersion in the C2PA `stego.imageauth` assertion** — already done (the assertion has `pHashVersion`), but the manifest is only available after IPFS recovery, which requires the ledger lookup anyway. For now, the receiver must assume a default hash algorithm or complete the ledger lookup before computing the local pHash.

- **Multi-hash attestations.** Currently each attestation stores one pHash with one pHashVersion. For robustness and forward compatibility, an image could be attested with multiple perceptual hashes (e.g., DinoHash-96 + pHash-64 + PDQ-256) each as separate attestations sharing the same `refUID`, or as a single attestation with an extended schema. Considerations:
  - Separate attestations: each has its own `pHashSaltIndex` entry, enabling lookup by any hash algorithm. Gas cost multiplied per hash. The `refUID` field (currently zero) could link them.
  - Extended schema: a single attestation with an array of `(pHashVersion, pHash)` tuples. Would require a schema change and more complex resolver logic. More gas-efficient but less flexible for per-algorithm lookup.
  - The current `pHashSaltIndex` key includes `pHashVersion`, so different hash algorithms for the same image naturally produce different index keys and don't collide.

- **Embed BLS PK in Stardust payload** for ledger-free initial signature verification. Currently the 256-bit WM-ID limit is too small for `salt(2B) + sig(21B) + PK(41B) = 64B = 512 bits`. Would require either higher Stardust capacity or compressed PK representation. Enables offline/initial verification without a ledger roundtrip.

### Dependency Graph

```
Phase 1.1 (KeyRegistry C2PA keys)
  -> Phase 1.2 (P-256 verification in resolver)
    -> Phase 1.3 (schema change)
      -> Phase 1.4 (deploy script)
        -> Phase 3.3 (ledger.py updates)
          -> Phase 3.1 (sign flow)
          -> Phase 3.2 (verify flow)
            -> Phase 5 (demo script)

Phase 2.1 (c2pa_manifest.py) --\
Phase 2.2 (c2pa_keys.py)     ----> Phase 3.1 (sign flow)
Phase 2.3 (ethSig in ledger) --/

Phase 3.4 (IPFS directory) -> Phase 3.1 (sign flow)
Phase 4.1 (resolution API) -> Phase 5 (demo script)
Phase 6 (docs) -> after Phase 5
```

## What Already Exists (Implemented)

### signatures-and-ecc-vibe
- `ImageAuthResolver.sol` with `c2paLookup()`, `c2paSchema()`, `pHashSaltIndex`, `sigPrefixIndex`, Bloom filter
- `ReputationRegistry.sol` (attestation counts, endorsements, disputes)
- `c2pa-resolution/` FastAPI service (needs updating for base64 + new schema)
- Docker Compose with Hardhat node + SE2 UI + IPFS (kubo) + C2PA API
- `stego_payload_tool` with `keygen` command and `--sk` flag for persistent keys

### perceptual-fuzzy-hash-test-vibe
- `stego/c2pa_manifest.py` (needs rewrite for embedded manifest + stego.imageauth assertion)
- `stego/ipfs.py` (needs directory upload support)
- `stego/cli.py` sign/verify flows (need rewrite for new architecture)
- `stego/signing.py` with persistent key support
- `stego/ledger.py` (needs schema update + new methods)

## Decisions Made

| Decision | Choice | Rationale |
|---|---|---|
| C2PA manifest library | `c2pa-python` v0.28.0 | Python bindings to c2pa-rs; actively maintained |
| Manifest storage | Embedded in image + IPFS directory | Embedded for direct verification; IPFS for recovery after stripping |
| IPFS structure | Directory (not single file) | Future-proof for additional metadata files |
| X.509 signing | Self-signed ES256 certificate | Sufficient for dev; production needs CA from C2PA Trust List |
| IPFS node | Local kubo in Docker | No external dependency; production adds Storacha/Filebase pinning |
| Resolution API | Python FastAPI | Implements C2PA OpenAPI spec; containerized |
| P-256 on-chain verification | Solidity library + RIP-7212 | Solidity for Hardhat/any chain; RIP-7212 for Base/Optimism/Arbitrum |
| c2paSig digest | `sha256` (not keccak256) | Standards-compliant with ES256; `sha256` precompile is cheap in Solidity |
| byBinding value encoding | Base64 | Per C2PA Soft Binding Resolution API spec |
| Schema migration | Clean slate | Breaking change; all dev/test data regenerated |
| Manifest ordering | Manifest before attestation | Manifest must be on IPFS for `metadataCID`; no EAS UID in manifest |
| Attestation -> manifest link | `metadataCID` (immutable IPFS CID) | CID = hash of content; cryptographic commitment |
| Manifest -> attestation link | `ethSig` in assertion + `pHashSaltIndex` lookup | No EAS UID needed in manifest |
| Dual-key binding | ethSig (secp256k1) + c2paSig (P-256) | Prevents manifest-swap attack if one key is compromised |

## References

- [C2PA Technical Specification v2.3](https://c2pa.org/specifications/specifications/2.3/specs/C2PA_Specification.html)
- [C2PA Soft Binding Resolution API](https://c2pa.org/specifications/specifications/2.2/softbinding/Decoupled.html)
- [C2PA Soft Binding Algorithm List](https://github.com/c2pa-org/softbinding-algorithm-list)
- [c2pa-python](https://github.com/contentauth/c2pa-python) (Python bindings to c2pa-rs)
- [c2pa-rs](https://github.com/contentauth/c2pa-rs) (Rust reference implementation)
- [c2patool](https://github.com/contentauth/c2pa-rs/tree/main/cli) (CLI validator)
- [RIP-7212](https://github.com/ethereum/RIPs/blob/master/RIPS/rip-7212.md) (P-256 precompile)
- [daimo p256-verifier](https://github.com/daimo-eth/p256-verifier) (Solidity P-256 library)
- [EAS SDK](https://github.com/ethereum-attestation-service/eas-sdk) (TypeScript/JavaScript)
- [EAS Contracts](https://github.com/ethereum-attestation-service/eas-contracts) (Solidity)
