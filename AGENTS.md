# AGENTS.md

Guidance for coding agents operating in this repository.

## Scope

| Directory | What it is | Language |
|---|---|---|
| `UOV/` | Post-quantum UOV signatures with salt-in-digest message recovery | C99 |
| `BLS/` | BLS BN-P158 and BLS12-381 signatures using RELIC | C99 |
| `unified-api/` | Scheme-agnostic signature API (`stego_sig.h`) over UOV and BLS | C99 |
| `docs/` | Signature scheme documentation | Markdown |

**Note**: The Ethereum ledger, UI, C2PA resolution API, Stardust watermarking, and end-to-end demos have been moved to [`consumer-sdproof-candidate`](https://github.com/mstattma/consumer-sdproof-candidate), which consumes this repo as a submodule.

When changing one area, check whether docs and `AGENTS.md` also need updates.

## Rules

- No `.cursor/rules/`, `.cursorrules`, or `.github/copilot-instructions.md` exist. This file is the only agent guidance.
- Prefer minimal, targeted changes. Do not reformat unrelated code.
- Do not silently change security-sensitive defaults (salt size, pHash limits, signature layout).
- This repo contains only signature schemes. Ledger, UI, C2PA, Stardust are in `consumer-sdproof-candidate`.

## Build / Test Quick Reference

### `UOV/pqov/`

```bash
make PARAM=80                 # build 80-bit variant
make PARAM=100                # build 100-bit variant
make test PARAM=80            # single-param test
make clean
```

### `BLS/`

```bash
make stego_demo CURVE=BN-158  # or BLS12-381
make test CURVE=BN-158
make build-relic              # build RELIC for both curves
make clean
```

### `unified-api/`

```bash
make stego_demo SCHEME=uov-80       # also: uov-100, bls-bn158, bls12-381
make test SCHEME=bls-bn158          # single-scheme test (build + run)
make stego_payload_tool SCHEME=bls-bn158      # payload tool (consumed by downstream Python CLI)
make clean
```

## Code Style

### General

- ASCII unless the file already uses Unicode. No emoji unless asked.
- Explicit, boring code over clever abstractions.
- High-signal comments: document *why*, not *what*.
- Update docs when behavior, payload formats, or workflow change.

### C / C99 (`UOV/`, `BLS/`, `unified-api/`)

- `snake_case` functions/variables, `SCREAMING_SNAKE_CASE` macros/constants.
- Standard headers first, then project headers.
- Prefer `uint8_t`, `uint64_t`, etc.
- Check return codes; do not assume success.
- Keep stack allocations conservative.
- In `unified-api/`, keep scheme-specific code in backend files, not shared headers.
- Respect the **current implemented** pHash limits: UOV-80 max 18 B / 144 bits, UOV-100 max 23 B / 184 bits.
- Design docs also discuss possible future 1-byte-salt variants (19 B / 24 B), but do not assume those are implemented unless you change code and docs together.
- Never silently truncate oversized pHashes (`ov_sign_digest` returns `-2`).
- Note: `unified-api/Makefile` uses `-O1` due to a RELIC BLS12-381 crash at `-O2`.

## Downstream Consumer

This repo is consumed as a git submodule by [`consumer-sdproof-candidate`](https://github.com/mstattma/consumer-sdproof-candidate), which uses `unified-api/stego_payload_tool` for BLS-BN158 signing.

If you change binary CLI args, payload format, or signature layout, update the consumer repo's `stego/signing.py`.

## Gotchas

- BLS12-381 unified-api binary crashes at `-O2`; use `-O1` (documented in Makefile).
- `unified-api/Makefile` uses `-O1` for all schemes due to this.

## When You Change These, Update Docs Too

| What changed | Update |
|---|---|
| Signature sizes / payload layout | `docs/scheme-comparison.md`, `unified-api/README.md`, top-level `README.md` |
| Binary interfaces (stego_payload_tool args) | Also update `consumer-sdproof-candidate/stego/signing.py` |
