# Compact Digital Signatures and ECC for Steganographic Channels

This project explores compact digital signature schemes and error-correcting codes for bandwidth-constrained steganographic channels (~400-1000 usable bits per image).

## Implementations

| Directory | Scheme | Signature Size | Security | Status |
|-----------|--------|---------------|----------|--------|
| [UOV/](UOV/) | Oil and Vinegar (post-quantum) | 400-504 bits | 80-100 bits | Working prototype |

## Documentation

- [Scheme Comparison](docs/scheme-comparison.md) -- Analysis of all PQC signature candidates considered

## Architecture

```
[signature bytes] ──► [outer RS-ECC] ──► [interleaver] ──► [stego embed in image]
```

Each implementation provides the signature layer. The outer ECC and stego embedding layers are planned future work.

## License

See individual subdirectories for license details.
