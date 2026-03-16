# System Flow Diagram

This diagram illustrates the complete interaction between the sender, receiver, and all system components during image authentication.

## Architecture Overview

```
                                    ┌─────────────────────────────────────────────┐
                                    │              ETHEREUM L2 (Base)              │
                                    │                                             │
                                    │  ┌─────────────┐   ┌────────────────────┐  │
                                    │  │ KeyRegistry  │   │ ImageAuthResolver  │  │
                                    │  │             │   │                    │  │
                                    │  │ BLS keys    │   │ sigPrefixIndex     │  │
                                    │  │ C2PA keys   │   │ pHashSaltIndex     │  │
                                    │  │ (P-256 x,y) │   │ P256Verifier       │  │
                                    │  └──────┬──────┘   │ c2paLookup         │  │
                                    │         │          │ c2paSchema (CAIP10) │  │
                                    │         │          └─────────┬──────────┘  │
                                    │         │                    │             │
                                    │  ┌──────┴────────────────────┴──────────┐  │
                                    │  │         EAS (Attestation Service)     │  │
                                    │  │                                       │  │
                                    │  │  Attestation = 12-field schema:       │  │
                                    │  │  sigPrefix, signature, scheme, PK,    │  │
                                    │  │  pHash, pHashVersion, salt, fileHash, │  │
                                    │  │  metadataCID, c2paCertHash, c2paSig,  │  │
                                    │  │  fileName                             │  │
                                    │  └──────────────────────────────────────┘  │
                                    │                                             │
                                    │  ┌──────────────────┐  ┌───────────────┐   │
                                    │  │ CrossChainBloom   │  │ Reputation    │   │
                                    │  │ Filter            │  │ Registry      │   │
                                    │  └──────────────────┘  └───────────────┘   │
                                    └─────────────────────────────────────────────┘

  ┌──────────┐                                                          ┌──────────────────────┐
  │   IPFS   │  manifest.c2pa + metadata.json (directory CID)           │  C2PA Resolution API  │
  │  (kubo)  │◄────────────────────────────────────────────────────────►│  (FastAPI :8000)      │
  └──────────┘                                                          │  /matches/byBinding   │
                                                                        │  /manifests/{uid}     │
                                                                        └──────────────────────┘
```

## Sender Flow

```
SENDER                          SYSTEM COMPONENTS                    ON-CHAIN
──────                          ─────────────────                    ────────

 ┌─────────────┐
 │ Input Image  │
 └──────┬──────┘
        │
        ▼
 ┌──────────────┐
 │ 1. Compute    │   DinoHash-96 (12 bytes = 96 bits)
 │    pHash      │──────────────────────────────────────┐
 └──────┬───────┘                                       │
        │                                               │
        ▼                                               │
 ┌──────────────┐                                       │
 │ 2. Generate   │   1 byte random                      │
 │    salt       │                                      │
 └──────┬───────┘                                       │
        │                                               │
        ▼                                               │
 ┌──────────────┐   stego_payload_tool                  │
 │ 3. BLS-BN158  │   generate <pHash_hex>               │
 │    sign pHash │──► payload = bls_salt(2B) || sig(21B) │
 │               │   = 23 bytes = 184 bits              │
 └──────┬───────┘                                       │
        │                                               │
        ▼                                               │
 ┌──────────────┐                                       │   ┌──────────────────┐
 │ 4. Check for  │   keccak256(pHashVersion,pHash,salt) │   │ ImageAuthResolver │
 │    duplicates │──────────────────────────────────────►│──►│ pHashSaltIndex    │
 │    (retry     │◄── UID or zero ──────────────────────│◄──│ BloomFilter       │
 │     salt)     │                                      │   └──────────────────┘
 └──────┬───────┘                                       │
        │                                               │
        ▼                                               │
 ┌──────────────┐                                       │   ┌──────────────────┐
 │ 5. Register   │   registerKey(pk, scheme=2)          │   │ KeyRegistry       │
 │    BLS key    │──────────────────────────────────────►│──►│ BLS key stored    │
 └──────┬───────┘                                       │   └──────────────────┘
        │                                               │
        ▼                                               │
 ┌──────────────┐                                       │   ┌──────────────────┐
 │ 6. Register   │   registerC2PAKey(x, y, certHash,    │   │ KeyRegistry       │
 │    C2PA key   │    certCID)                          │   │ P-256 pub key     │
 │    (ES256     │──────────────────────────────────────►│──►│ stored (x,y)     │
 │     P-256)    │                                      │   └──────────────────┘
 └──────┬───────┘                                       │
        │                                               │
        ▼                                               │
 ┌──────────────┐   Stardust embed                      │
 │ 7. Embed      │   sp-width=7, sp-height=7            │
 │    watermark  │   sp-density=100, pm-mode=3           │
 │    in image   │   payload = 184-bit WM-ID            │
 │               │──► watermarked.png + sidecar YUVs    │
 └──────┬───────┘                                       │
        │                                               │
        ▼                                               │
 ┌──────────────┐                                       │
 │ 8. SHA-256    │   fileHash of watermarked image      │
 │    file hash  │                                      │
 └──────┬───────┘                                       │
        │                                               │
        ▼                                               │
 ┌──────────────┐   Ethereum wallet key (secp256k1):    │
 │ 9. Generate   │   ethSig = personal_sign(            │
 │    ethSig     │     keccak256(pHash||pHashVer||salt)) │
 └──────┬───────┘                                       │
        │                                               │
        ▼                                               │
 ┌──────────────┐   C2PA private key (P-256):           │
 │10. Generate   │   c2paSig = ES256_sign(              │
 │    c2paSig    │     sha256(pHash||pHashVer||salt))    │
 └──────┬───────┘                                       │
        │                                               │
        ▼                                               │
 ┌──────────────┐   c2pa-python Builder.sign():         │
 │11. Generate   │   Assertions:                        │
 │    C2PA       │   - c2pa.soft-binding (Stardust WM)  │
 │    manifest   │   - stego.imageauth:                 │
 │    + embed    │       pHash, pHashVersion, salt,     │
 │    in image   │       ethSig, c2paSig                │
 │               │   - c2pa.hash.data (auto, SHA-256)   │
 │               │   Claim signature: ES256 (P-256)     │
 │               │   Timestamp: DigiCert TSA (RFC 3161) │
 │               │──► manifest embedded in PNG (JUMBF)  │
 └──────┬───────┘                                       │
        │                                               │
        ▼                                               │
 ┌──────────────┐   IPFS directory upload:              │   ┌──────────────────┐
 │12. Upload to  │   manifest.c2pa + metadata.json      │   │      IPFS        │
 │    IPFS       │──────────────────────────────────────►│──►│ directory CID    │
 │               │◄── CID (immutable content hash) ─────│◄──│ QmXyz...         │
 └──────┬───────┘                                       │   └──────────────────┘
        │                                               │
        ▼                                               │
 ┌──────────────┐   EAS.attest({                        │   ┌──────────────────┐
 │13. Register   │     sigPrefix, signature, scheme,    │   │ EAS              │
 │    EAS        │     publicKey, pHash, pHashVersion,  │   │                  │
 │    attestation│     salt, fileHash, metadataCID,     │   │ Attestation UID  │
 │               │     c2paCertHash, c2paSig, fileName  │   │ created          │
 │               │   })                                 │   │                  │
 │               │──────────────────────────────────────►│──►│ Resolver.onAttest│
 │               │                                      │   │  ├─ dedup check  │
 │               │   Resolver verifies:                 │   │  ├─ BLS key check│
 │               │   ✓ No (pHash,salt) duplicate        │   │  ├─ P256 verify  │
 │               │   ✓ BLS key valid in KeyRegistry     │   │  │   c2paSig     │
 │               │   ✓ c2paSig valid (P-256 via         │   │  ├─ write indexes│
 │               │     RIP-7212 or Solidity fallback)   │   │  └─ Bloom update │
 │               │◄── attestation UID ──────────────────│◄──│                  │
 └──────┬───────┘                                       │   └──────────────────┘
        │
        ▼
 ┌──────────────┐
 │ Signed image  │   Contains: watermark (pixel data) + C2PA manifest (JUMBF metadata)
 │ ready to send │   IPFS: manifest.c2pa + metadata.json recoverable by CID
 │               │   Ledger: attestation with all verification data
 └──────────────┘
```

## Social Media Transform (Simulated)

```
 ┌──────────────┐      ffmpeg                    ┌──────────────┐
 │ Signed image  │──── JPEG q50 ────────────────►│ Received      │
 │ (PNG, 850 KB) │     + asymmetric 60% crop     │ image         │
 │               │     crop=iw*0.6:ih*0.8:       │ (JPEG, ~10KB) │
 │               │       iw*0.15:ih*0.1          │               │
 └──────────────┘                                └──────┬───────┘
                                                        │
   What survives:                                       │  What's lost:
   ✓ Stardust watermark (pixel-domain, robust)          │  ✗ C2PA manifest (JUMBF metadata stripped)
   ✓ Perceptual hash similarity (tolerant)              │  ✗ File hash match (pixels changed)
   ✓ On-chain attestation (immutable)                   │  ✗ Original resolution
   ✓ IPFS manifest (recoverable by CID)                 │  ✗ Lossless quality
```

## Receiver Flow

```
RECEIVER                        SYSTEM COMPONENTS                    ON-CHAIN
────────                        ─────────────────                    ────────

 ┌──────────────┐
 │ Received      │   (C2PA manifest stripped, pixels modified)
 │ image (JPEG)  │
 └──────┬───────┘
        │
        ▼
 ┌──────────────┐   Stardust extract
 │ 1. Extract    │   sp-width=7, sp-height=7
 │    watermark  │   sp-density=100, fec=2
 │               │──► payload = bls_salt(2B) || sig(21B)
 └──────┬───────┘
        │
        ▼
 ┌──────────────┐
 │ 2. Parse      │   sig_prefix = payload[2:18] (16 bytes)
 │    payload    │
 └──────┬───────┘
        │
        ▼
 ┌──────────────┐                                       ┌──────────────────┐
 │ 3. Lookup     │   sigPrefixIndex[sigPrefix]          │ ImageAuthResolver │
 │    attestation│──────────────────────────────────────►│                  │
 │    on ledger  │◄── attestation UID ──────────────────│ EAS.getAttest()  │
 │               │◄── full 12-field attestation data ───│                  │
 └──────┬───────┘                                       └──────────────────┘
        │
        │  Now receiver has: pHash, pHashVersion, salt, PK,
        │  signature, fileHash, metadataCID, c2paCertHash,
        │  c2paSig, attester address, timestamp
        │
        ▼
 ┌──────────────┐   stego_payload_tool verify
 │ 4. Verify BLS │   (payload, PK, pHash from ledger)
 │    signature  │──► VALID / INVALID
 └──────┬───────┘
        │
        ▼
 ┌──────────────┐   DinoHash-96 of received image
 │ 5. Compute    │   Compare with pHash from ledger
 │    receiver   │──► Hamming distance (expect >0 after transform)
 │    pHash      │──► Similarity % (e.g., 85% after JPEG+crop)
 └──────┬───────┘
        │
        ▼
 ┌──────────────┐   Convert metadataCID (bytes32) → CIDv0   ┌──────────┐
 │ 6. Recover    │   IPFS fetch: <CID>/manifest.c2pa         │   IPFS   │
 │    C2PA       │──────────────────────────────────────────►│          │
 │    manifest   │◄── manifest.c2pa bytes ──────────────────│          │
 │    from IPFS  │                                           └──────────┘
 └──────┬───────┘
        │
        ▼
 ┌──────────────┐   c2pa.Reader validates:
 │ 7. Validate   │   ✓ Claim signature (ES256)
 │    C2PA       │   ✓ Timestamp (RFC 3161)
 │    manifest   │   ✗ Hard binding (SHA-256 — fails, expected)
 │               │   ✓ Soft binding (Stardust WM-ID matches)
 └──────┬───────┘
        │
        ▼
 ┌──────────────┐   From stego.imageauth assertion:
 │ 8. Verify     │                                          ┌──────────────────┐
 │    binding    │   ethSig → ecrecover → attester addr     │ KeyRegistry       │
 │    signatures │   c2paSig + c2paCertHash                 │ getC2PAKeyBy      │
 │               │──────────────────────────────────────────►│ CertHash(hash)   │
 │               │◄── (pubKeyX, pubKeyY, active) ───────────│ → P-256 pub key  │
 │               │   P-256 verify c2paSig → VALID/INVALID   └──────────────────┘
 └──────┬───────┘
        │
        │  Binding proves: both Ethereum wallet key AND C2PA key
        │  were available when (pHash, pHashVersion, salt) were committed
        │
        ▼
 ┌──────────────┐                                          ┌──────────────────┐
 │ 9. Lookup     │   getReputation(attester)               │ ReputationRegistry│
 │    attester   │──────────────────────────────────────────►│                  │
 │    reputation │◄── attestationCount, endorsementScore,  │ score(attester)  │
 │               │    disputeCount, disputesWon, firstSeen │                  │
 └──────┬───────┘                                          └──────────────────┘
        │
        ▼
 ┌──────────────┐
 │10. Analyze    │   Tier 1: Watermark extraction + BLS verify
 │    results    │     → proves image was signed by the attester's BLS key
 │               │
 │               │   Tier 2: pHash comparison
 │               │     → Hamming distance quantifies modification
 │               │     → high similarity + valid BLS = authentic content
 │               │     → low similarity = heavily modified or different image
 │               │
 │               │   Tier 3: Ledger data analysis
 │               │     → c2paSig binding: both keys present at signing ✓
 │               │     → fileHash mismatch: file was modified (expected) ✗
 │               │     → metadataCID: C2PA manifest recoverable from IPFS ✓
 │               │     → attester reputation: trustworthiness score
 │               │     → timestamp: when the attestation was created
 │               │     → fileName: original image identifier
 │               │     → pHashVersion: which hash algo was used
 │               │
 │               │   Conclusion:
 │               │     HIGH confidence: BLS valid + similarity >70% + good reputation
 │               │     MODERATE: BLS valid + similarity 40-70%
 │               │     LOW: BLS valid but similarity <40% or poor reputation
 │               │     REJECT: BLS invalid or no attestation found
 └──────────────┘
```

## C2PA Recovery Flow (Third-Party Client)

```
A C2PA-aware client that doesn't know our system can still recover the manifest:

 ┌──────────────┐
 │ Image with    │   Client detects Stardust watermark
 │ watermark     │
 └──────┬───────┘
        │
        ▼
 ┌──────────────┐   Look up "castlabs.stardust" in SBAL
 │ SBAL lookup   │──► softBindingResolutionApis:
 │               │      ["https://resolver.example/v1",
 │               │       "eip155:8453:0xDc64..."]
 └──────┬───────┘
        │
        ├──── Path A: HTTP Resolution API ────┐
        │                                      │
        ▼                                      ▼
 ┌──────────────┐                      ┌──────────────┐
 │ GET /matches/ │                      │ On-chain     │
 │ byBinding     │                      │ c2paLookup   │
 │ ?alg=castlabs │                      │ JSON(input)  │
 │ .stardust     │                      │              │
 │ &value=<b64>  │                      │ (via CAIP-10 │
 │               │                      │  contract)   │
 └──────┬───────┘                      └──────┬───────┘
        │                                      │
        │◄── manifestId (EAS UID) ────────────┘
        │
        ▼
 ┌──────────────┐
 │ GET /manifests│   Resolution API fetches from IPFS:
 │ /{uid}        │   EAS.getAttestation(uid) → metadataCID
 │               │   IPFS cat <CID>/manifest.c2pa
 │               │──► application/c2pa response
 └──────┬───────┘
        │
        ▼
 ┌──────────────┐
 │ Standard C2PA │   Validate claim signature, assertions,
 │ validation    │   soft binding, timestamp, etc.
 └──────────────┘
```

## Data Flow Summary

```
                    ┌─────────────────┐
                    │   Sender's Keys  │
                    │                 │
                    │ Ethereum wallet ─┼──► signs EAS tx + ethSig
                    │ C2PA P-256 key ─┼──► signs c2paSig + C2PA claim
                    │ BLS-BN158 key ──┼──► signs pHash → watermark payload
                    └─────────────────┘
                             │
              ┌──────────────┼──────────────┐
              ▼              ▼              ▼
       ┌────────────┐ ┌───────────┐ ┌─────────────┐
       │ Watermark   │ │ C2PA      │ │ EAS         │
       │ (pixels)    │ │ Manifest  │ │ Attestation │
       │             │ │ (JUMBF +  │ │ (on-chain)  │
       │ BLS sig     │ │  IPFS)    │ │             │
       │ embedded    │ │           │ │ 12 fields   │
       │ in image    │ │ ethSig    │ │ c2paSig     │
       │             │ │ c2paSig   │ │ verified by │
       │             │ │ pHash     │ │ P256Verifier│
       │             │ │ salt      │ │             │
       └──────┬─────┘ └─────┬─────┘ └──────┬──────┘
              │              │              │
              │   Survives   │  Stripped by  │  Immutable
              │   transform  │  ffmpeg but   │  on-chain
              │              │  recoverable  │
              │              │  from IPFS    │
              │              │              │
              ▼              ▼              ▼
       ┌─────────────────────────────────────────┐
       │         Receiver Verification            │
       │                                         │
       │  watermark ──► BLS verify ──► VALID     │
       │  watermark ──► sigPrefix ──► ledger     │
       │  ledger ──► metadataCID ──► IPFS        │
       │  IPFS ──► C2PA manifest ──► validate    │
       │  ledger ──► reputation ──► trust score  │
       │  pHash compare ──► similarity %         │
       └─────────────────────────────────────────┘
```
