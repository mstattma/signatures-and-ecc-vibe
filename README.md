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

| Directory | Scheme | Payload Size | Classical Security | Quantum Security | Status |
|-----------|--------|-------------|-------------------|-----------------|--------|
| [UOV/](UOV/) | Oil and Vinegar | 400-504 bits (sig only, pHash recovered) | 80-100 bits | ~40-60 bits (est.) | Working prototype |
| [BLS/](BLS/) | BLS (BN-P158) | 264-352 bits (pHash + sig, no PK) | ~78 bits | 0 (broken by Shor) | Working prototype |
| [BLS/](BLS/) | BLS (BLS12-381) | 488-576 bits (pHash + sig, no PK) | ~117-120 bits | 0 (broken by Shor) | Working prototype |

## Unified API

The [unified/](unified/) directory provides a **scheme-agnostic API** (`stego_sig.h`) that abstracts over all signature schemes. A single `stego_demo.c` works identically with any backend:

```bash
cd unified
make test SCHEME=uov-80       # Post-quantum, 400-bit payload
make test SCHEME=bls-bn158    # Classical, 264-352 bit payload
```

### Payload sizes (bits, without PK)

| pHash | UOV-80 | UOV-100 | BLS-BN158 | BLS12-381 |
|---|---|---|---|---|
| 96-bit | 400 | 504 | **264** | 488 |
| 144-bit | 400 | 504 | **312** | 536 |
| 184-bit | 400 | 504 | **352** | 576 |

### Payload sizes (bits, with PK in-band)

| pHash | UOV-80 | UOV-100 | BLS-BN158 | BLS12-381 |
|---|---|---|---|---|
| 96-bit | 204,400 | 403,704 | **592** | 1,264 |
| 144-bit | 204,400 | 403,704 | **640** | 1,312 |
| 184-bit | 204,400 | 403,704 | **680** | 1,352 |

## Documentation

- [Unified API](unified/) -- Scheme-agnostic signature API with payload size tables
- [UOV Implementation](UOV/) -- Post-quantum signatures with message recovery
- [BLS Implementation](BLS/) -- Classical BLS signatures (BN-P158 and BLS12-381)
- [Scheme Comparison](docs/scheme-comparison.md) -- Analysis of all PQC signature candidates considered

## Architecture

```
[pHash(image)] ──► [sign] ──► [outer RS-ECC] ──► [interleaver] ──► [stego embed]
```

Each implementation provides the signature layer. UOV recovers the pHash from the signature (message recovery); BLS transmits the pHash explicitly alongside the signature. The outer ECC and stego embedding layers are planned future work.

## License

See individual subdirectories for license details.
