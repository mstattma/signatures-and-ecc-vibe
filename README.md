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

| Directory | Scheme | Signature Size | Security | Status |
|-----------|--------|---------------|----------|--------|
| [UOV/](UOV/) | Oil and Vinegar (post-quantum) | 400-504 bits | 80-100 bits | Working prototype |

## Documentation

- [UOV Implementation](UOV/) -- Full documentation, API, build instructions
- [Scheme Comparison](docs/scheme-comparison.md) -- Analysis of all PQC signature candidates considered

## Architecture

```
[pHash(image)] ──► [UOV sign] ──► [outer RS-ECC] ──► [interleaver] ──► [stego embed]
```

Each implementation provides the signature layer. The outer ECC and stego embedding layers are planned future work.

## License

See individual subdirectories for license details.
