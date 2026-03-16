# Fuzzy Signatures for Steganographic Image Authentication

This project implements compact digital signature schemes for authenticating images through a steganographic channel embedded in the image itself.

Instead of signing the raw image bytes, the sender signs a **perceptual hash** of the image. The receiver then compares the sender-side perceptual hash against their own perceptual hash of the received image.

That makes the result a **fuzzy signature** rather than a binary valid/invalid signature:

- exact cryptographic signatures break if even one bit changes
- perceptual-hash signatures survive benign image changes such as recompression, resize, or light post-processing
- verification yields an **authenticity score** or similarity measure instead of a strict boolean

## How It Works

```
Sender:  image ──► pHash() ──► sign(sk, phash) ──► stego_embed(image, signature) ──► image'

Receiver: image' ──► stego_extract() ──► P(w) = phash_sender
                 ──► pHash(image')   = phash_receiver
                 ──► similarity(phash_sender, phash_receiver) = authenticity score
```

The signature is embedded steganographically in the image. The receiver extracts it, recovers or retrieves the sender's perceptual hash, and compares it against their own perceptual hash of the received image.

Typical interpretation:

- high similarity -> authentic or only lightly modified
- medium similarity -> related but modified image
- low similarity -> suspicious or unrelated image

For a scheme-independent discussion of perceptual hash choices, truncation trade-offs, and fuzzy-signature behavior, see [perceptual-hash-considerations.md](https://github.com/mstattma/perceptual-fuzzy-hash-test-vibe/blob/master/docs/perceptual-hash-considerations.md) in the perceptual-fuzzy-hash-test-vibe repo.

## Architecture

### Stego Channel Pipeline

```text
[pHash(image)] -> [sign] -> [stego transport] -> [extract] -> [verify]
```

Current transport layers in the repository:

- **direct payload model** used by the signature demos (`UOV/`, `BLS/`, `unified-api/`)
- **castLabs Stardust** watermark transport (moved to `perceptual-fuzzy-hash-test-vibe` repo)

Signature-layer behavior:

- **UOV** recovers the sender-side perceptual hash from the signature itself (message recovery)
- **BLS** carries or externally retrieves the pHash and always carries a 2-byte salt in the payload

Verification-layer behavior:

- compare sender-side and receiver-side perceptual hashes for an authenticity score
- optionally consult the ledger for PK lookup, timestamps, duplicate detection, reputation, and metadata

### Ledger-Backed Verification

The Ethereum ledger, C2PA integration, Stardust watermarking, SE2 UI, and end-to-end demos have been moved to [`consumer-sdproof-candidate`](https://github.com/mstattma/consumer-sdproof-candidate), which consumes this repo as a submodule for `stego_payload_tool`.

## Implemented Signature Schemes

| Directory | Scheme | Sig (bits) | PK (bits) | SK (bits) | Payload Size | Classical Security | Quantum Security | Status |
|-----------|--------|-----------|----------|----------|-------------|-------------------|-----------------|--------|
| [UOV/](UOV/) | Oil and Vinegar (80-bit) | 400 | 204,000 | 175,456 | 400 bits (sig only, pHash recovered) | 80 bits | ~40-48 bits (est.) | Working prototype |
| [UOV/](UOV/) | Oil and Vinegar (100-bit) | 504 | 403,200 | 346,056 | 504 bits (sig only, pHash recovered) | 100 bits | ~50-60 bits (est.) | Working prototype |
| [BLS/](BLS/) | BLS (BN-P158) | 168 | 328 | 160 | 280-368 bits (pHash + salt + sig, no PK) | ~78 bits | 0 (broken by Shor) | Working prototype |
| [BLS/](BLS/) | BLS (BLS12-381) | 392 | 776 | 256 | 504-592 bits (pHash + salt + sig, no PK) | ~117-120 bits | 0 (broken by Shor) | Working prototype |

## Related Repos

| Repo | Purpose |
|---|---|
| [`consumer-sdproof-candidate`](https://github.com/mstattma/consumer-sdproof-candidate) | Integration layer: Stardust watermarking, Ethereum ledger (EAS), C2PA manifests, SE2 UI, IPFS, end-to-end demos. Consumes this repo as a submodule for `stego_payload_tool`. |
| [`perceptual-fuzzy-hash-test-vibe`](https://github.com/mstattma/perceptual-fuzzy-hash-test-vibe) | Perceptual hash algorithms, benchmarks, hash adapters, collision analysis |

## Stego Transport Components

Stardust watermark transport has been moved to the [`perceptual-fuzzy-hash-test-vibe`](https://github.com/mstattma/perceptual-fuzzy-hash-test-vibe) repo, where it is integrated with the Python stego CLI (`python -m stego sign/verify`). This repo provides the signature tools consumed by that CLI via git submodule.

## Unified API

The [unified-api/](unified-api/) directory currently provides a **scheme-agnostic signature API** (`stego_sig.h`) that abstracts over the implemented signature schemes. A single `stego_demo.c` works identically with any backend.

It currently focuses on the **signature layer only**. Additional media transport / stego functionality may be added later.

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

For ledger, C2PA, UI, and system integration documentation, see [`consumer-sdproof-candidate/docs/`](https://github.com/mstattma/consumer-sdproof-candidate/tree/master/docs). For perceptual hash analysis, see [`perceptual-fuzzy-hash-test-vibe/docs/`](https://github.com/mstattma/perceptual-fuzzy-hash-test-vibe/tree/master/docs).

## Quick Start

```bash
cd unified-api
make test SCHEME=bls-bn158       # Test BLS-BN158 sign/verify
make test SCHEME=uov-80          # Test UOV-80 sign/verify
make stego_payload_tool SCHEME=bls-bn158   # Build payload tool (used by consumer repo)
```

## License

See individual subdirectories for license details.
