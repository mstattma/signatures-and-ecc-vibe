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

| Directory | Scheme | Payload Size | Security | Quantum-safe | Status |
|-----------|--------|-------------|----------|-------------|--------|
| [UOV/](UOV/) | Oil and Vinegar | 400-504 bits (sig only, pHash recovered) | 80-100 bits | Yes | Working prototype |
| [BLS/](BLS/) | BLS (BN-P158) | 264-352 bits (pHash + sig, no PK) | ~78 bits | No | Working prototype |
| [BLS/](BLS/) | BLS (BLS12-381) | 488-576 bits (pHash + sig, no PK) | ~117-120 bits | No | Working prototype |

## Documentation

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
