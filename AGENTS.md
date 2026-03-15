# AGENTS.md

Guidance for coding agents operating in this repository.

## Scope

| Directory | What it is | Language |
|---|---|---|
| `UOV/` | Post-quantum UOV signatures with salt-in-digest message recovery | C99 |
| `BLS/` | BLS BN-P158 and BLS12-381 signatures using RELIC | C99 |
| `unified-api/` | Scheme-agnostic signature API (`stego_sig.h`) over UOV and BLS | C99 |
| `scripts/` | Repo-level utilities | Bash, Python |
| `ethereum-ledger/` | Solidity contracts, Hardhat scripts, Dockerized local chain | Solidity, JS |
| `ui/` | Scaffold-ETH 2 UI with custom pages (has its own `ui/AGENTS.md`) | TypeScript, React |
| `docs/` | Design documents and proposals | Markdown |

When changing one area, check whether docs, Docker flow, generated ABI files, and `AGENTS.md` also need updates.

## Rules

- No `.cursor/rules/`, `.cursorrules`, or `.github/copilot-instructions.md` exist. This file is the only agent guidance.
- Prefer minimal, targeted changes. Do not reformat unrelated code.
- Preserve standalone nature of subprojects (`ethereum-ledger/` is intentionally separate from `ui/packages/hardhat`).
- Do not silently change security-sensitive defaults (salt size, pHash limits, signature layout, gas assumptions).
- Stardust has been moved to `perceptual-fuzzy-hash-test-vibe`; this repo no longer contains it.

## Build / Test Quick Reference

### Full local stack (Docker, recommended)

```bash
docker compose up -d          # Hardhat node :8545 + SE2 UI :3000
docker compose logs -f
```

This is the preferred development path for both ledger and UI work.

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

### `ethereum-ledger/`

```bash
npx hardhat compile
npx hardhat run scripts/deploy.js --network localhost
npx hardhat run scripts/demo.js --network localhost
SIMULATE_NETWORK=arbitrumNova npx hardhat run scripts/demo.js --network localhost
node scripts/export-abis.js      # regenerate UI contract ABIs
```

### `ui/`

Docker-first (preferred; started by `docker compose up`). Manual fallback:

```bash
cd ui && yarn install && yarn workspace @se-2/nextjs dev
```

After contract changes: `cd ethereum-ledger && npx hardhat compile && node scripts/export-abis.js`

### `scripts/` (repo-level)

The `scripts/` directory previously contained `stardust_image_demo.sh` and `patch_stardust.py`, which have been moved to `perceptual-fuzzy-hash-test-vibe`.

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

### Solidity (`ethereum-ledger/contracts/`)

- Version `0.8.27`, EVM target `cancun`, optimizer on.
- Small focused contracts with explicit events.
- Preserve separation: `KeyRegistry` = key lifecycle, `CrossChainBloomFilter` = dedup, `ImageAuthResolver` = EAS resolver.
- Consider gas cost before adding on-chain state; prefer IPFS for bulk data.
- If interfaces change, update deploy/demo scripts and `docs/ethereum-ledger-proposal.md`.

### JavaScript (`ethereum-ledger/scripts/`)

- CommonJS (`require`). Keep scripts as one-off operational tools.
- Print actionable summaries (addresses, gas with USD, network).
- Gas cost display uses `formatGas()` with per-network pricing and `ETH_PRICE_USD` env override.

### TypeScript / React (`ui/`)

- Follow SE2 conventions and `ui/AGENTS.md`.
- App Router patterns in `packages/nextjs/app/`.
- Use SE2 hooks: `useScaffoldReadContract`, `useScaffoldWriteContract`, `useScaffoldEventHistory`.
- Our contracts are in `externalContracts.ts`, not `deployedContracts.ts`.
- `decodeTxData.ts` is patched to decode both deployed and external contracts and to label contract creation transactions.

### Shell / Python (`scripts/`)

- Bash scripts: `set -euo pipefail`.
- Stardust patch script has moved to `perceptual-fuzzy-hash-test-vibe/stego/patch_stardust.py`.

## UI Extensions That Must Be Preserved

- Custom pages: `/users` (event-driven), `/keys` (state-driven, accepts `?address=`), `/bloom`
- Homepage and debug page have project-specific copy, not SE2 defaults.
- `decodeTxData.ts` patches: external contract decoding + contract creation labeling.

## Stardust (moved)

Stardust watermark transport has been moved to `perceptual-fuzzy-hash-test-vibe`. This repo no longer contains the `stardust/` submodule, `patch_stardust.py`, or `stardust_image_demo.sh`.

## Downstream Consumer: Python Stego CLI

This repo is consumed as a **git submodule** by [`perceptual-fuzzy-hash-test-vibe`](https://github.com/mstattma/perceptual-fuzzy-hash-test-vibe), which wraps our binaries and contracts in a Python CLI (`python -m stego`).

The downstream CLI depends on:
- `unified-api/stego_payload_tool` (BLS-BN158 keygen/sign/verify)
- `ethereum-ledger/deployment.json` (contract addresses for ledger interaction)
- `ethereum-ledger/` contract ABIs (attestation schema)

Stardust binaries are now built and managed in the downstream repo directly.

If you change any of these interfaces (binary CLI args, payload format, attestation schema, deployment.json structure), the Python CLI in the other repo will also need updating.

## Gotchas

- `ui/` on OneDrive/WSL2 paths is very slow for `yarn install`. Docker avoids this.
- Next.js cache in Docker volume `nextjs-cache`; clear with `docker volume rm ...nextjs-cache` if stale.
- Hardhat node container state is in-memory; `docker compose down` + `up` resets everything.
- `deployment.json` bridges deploy and demo scripts; demo detects stale addresses and falls back.
- BLS12-381 unified-api binary crashes at `-O2`; use `-O1` (documented in Makefile).
- There is no unified fine-grained unit-test framework across the repo; for many areas, the closest “single test” is a narrow demo target (`make test SCHEME=...`, one Hardhat script, or one UI page flow).

## When You Change These, Update Docs Too

| What changed | Update |
|---|---|
| Signature sizes / payload layout | `docs/scheme-comparison.md`, `unified-api/README.md`, top-level `README.md` |
| pHash embedding / max lengths | `docs/perceptual-hash-considerations.md`, `unified-api/README.md` |
| Gas costs / network assumptions | `docs/ethereum-ledger-proposal.md`, `ethereum-ledger/README.md` |
| Contracts / ABI | deploy scripts, `export-abis.js`, `docs/ethereum-ledger-proposal.md` |
| Docker workflow | `docker-compose.yml` comments, `ethereum-ledger/README.md` |
| UI pages or SE2 patches | `docs/ui-se2-customizations.md` |
| Stardust integration | `docs/stardust-stego-demo.md` |
| Binary interfaces or attestation schema | Also update downstream Python CLI in `perceptual-fuzzy-hash-test-vibe/stego/` |
