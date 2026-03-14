# AGENTS.md

This file is for coding agents working anywhere in this repository.

## Scope

This repo contains several semi-independent projects:

- `UOV/` - post-quantum UOV implementation with salt-in-digest message recovery
- `BLS/` - BLS BN-P158 and BLS12-381 implementation using RELIC
- `unified-api/` - scheme-agnostic C API over UOV and BLS
- `ethereum-ledger/` - Solidity contracts, Hardhat scripts, Dockerized local chain
- `ui/` - Scaffold-ETH 2 based UI for exploring and operating the ledger
- `docs/` - design documents and proposals

When changing one area, check whether docs, Docker flow, and generated ABI/address files also need updates.

## Repo-Level Rules

- There is **no root `.cursor/rules/`, `.cursorrules`, or `.github/copilot-instructions.md`**.
- The `ui/` subtree has its own `ui/AGENTS.md`; treat it as additional local guidance for that subtree.
- Prefer minimal, targeted changes over broad refactors.
- Preserve the standalone nature of subprojects. In particular, `ethereum-ledger/` is intentionally separate from `ui/packages/hardhat`.
- Do not silently change security-sensitive defaults (salt size, pHash length limits, signature layout, gas assumptions) without updating docs.

## Primary Workflows

## Full local stack (recommended)

From repo root:

```bash
docker compose up -d
docker compose logs -f
```

Services:

- Hardhat node: `http://localhost:8545`
- UI: `http://localhost:3000`
- Contract explorer/debug: `http://localhost:3000/debug`

The node container auto-deploys contracts and writes `ethereum-ledger/deployment.json`.
The UI container auto-generates `externalContracts.ts` from deployment data.

## Quick command reference

### `UOV/`

Main build/test entrypoints live in `UOV/pqov`.

```bash
cd UOV/pqov
make PARAM=80          # build 80-bit variant
make PARAM=100         # build 100-bit variant
make test PARAM=80     # run tests for one parameter set
make test PARAM=100
make clean
```

If a single named test target exists in the local Makefile, prefer it. Otherwise use the nearest narrow target.

### `BLS/`

```bash
cd BLS
make stego_demo CURVE=BN-158
make stego_demo CURVE=BLS12-381
make test CURVE=BN-158
make test CURVE=BLS12-381
make build-relic
make clean
```

### `unified-api/`

```bash
cd unified-api
make stego_demo SCHEME=uov-80
make stego_demo SCHEME=uov-100
make stego_demo SCHEME=bls-bn158
make stego_demo SCHEME=bls12-381
make test SCHEME=uov-80
make test SCHEME=bls-bn158
make clean
```

There is no granular unit-test suite here; `make test SCHEME=...` is the closest single-scheme test.

### `ethereum-ledger/`

```bash
cd ethereum-ledger
npx hardhat compile
npx hardhat run scripts/deploy.js --network localhost
npx hardhat run scripts/demo.js --network localhost
npx hardhat run scripts/deploy.js --network baseSepolia
npx hardhat run scripts/demo.js --network baseSepolia
node scripts/export-abis.js
```

Useful variants:

```bash
SIMULATE_NETWORK=arbitrumNova npx hardhat run scripts/demo.js --network localhost
ETH_PRICE_USD=2500 npx hardhat run scripts/demo.js --network localhost
```

### `ui/`

Docker-first workflow is preferred. If running manually:

```bash
cd ui
yarn install
yarn workspace @se-2/nextjs dev
```

If contracts changed:

```bash
cd ethereum-ledger
npx hardhat compile
node scripts/export-abis.js
```

## Build/Lint/Test Expectations

- For C/C code, rebuild only the affected scheme/project if possible.
- For Solidity changes, always run `npx hardhat compile` in `ethereum-ledger/`.
- For UI changes, prefer at least a local page load or container restart if full lint/build is too expensive.
- After contract ABI/address changes, regenerate `ui/packages/nextjs/contracts/externalContracts.ts`.
- If you touch `docker-compose.yml`, verify both `node` and `ui` services still start.

## Code Style - General

- Keep code ASCII unless the file already uses Unicode or there is a strong reason.
- Favor explicit, boring code over clever abstractions.
- Keep comments high-signal. Document why, not the obvious what.
- Update docs when behavior, payload formats, or operational workflow changes.
- Avoid introducing new global tools/frameworks unless clearly justified.

## C / C99 Style (`UOV/`, `BLS/`, `unified-api/`)

- Match the existing file style; do not reformat unrelated blocks.
- Includes: standard/system headers first, then project headers.
- Use `snake_case` for functions and variables.
- Use `SCREAMING_SNAKE_CASE` for macros and compile-time constants.
- Prefer fixed-width integer types (`uint8_t`, `uint64_t`, etc.).
- Check return codes explicitly; do not assume success.
- Keep stack allocations conservative. Large buffers should be justified.
- In `unified-api/`, preserve the abstraction boundary: scheme-specific behavior belongs in backend files, not the shared header/demo unless unavoidable.
- Respect pHash size constraints:
  - UOV-80 max: 18 bytes / 144 bits
  - UOV-100 max: 23 bytes / 184 bits
- Do not reintroduce silent truncation of oversized pHashes.

## Solidity Style (`ethereum-ledger/contracts/`)

- Solidity version is `0.8.27`; keep optimizer-compatible code.
- Favor small, focused contracts with explicit events.
- Use clear revert strings for user-facing checks.
- Keep trust/security assumptions visible in comments.
- Preserve separation of concerns:
  - `KeyRegistry` = key lifecycle
  - `CrossChainBloomFilter` = duplicate detection primitive
  - `ImageAuthResolver` = EAS resolver / policy enforcement
- If adding new state, consider gas cost and whether it belongs on-chain or in IPFS/off-chain metadata.
- If contract interfaces change, update deploy/demo scripts and the proposal docs.

## JavaScript / Node Style (`ethereum-ledger/scripts/`)

- Use CommonJS in `ethereum-ledger/` scripts unless the project is migrated consistently.
- Keep scripts runnable as one-off operational tools.
- Print actionable summaries (addresses, schema UID, gas, network).
- Be explicit about local vs testnet behavior.
- If a script writes generated files (`deployment.json`, ABI exports), document that side effect.

## Next.js / TypeScript Style (`ui/`)

- Follow existing SE2 conventions and also respect `ui/AGENTS.md`.
- Use App Router patterns already present in `packages/nextjs/app/`.
- Prefer `type` over `interface` for local custom types unless extending is needed.
- Use named, focused client components for stateful UI.
- Prefer SE2 hooks for contract interaction:
  - `useScaffoldReadContract`
  - `useScaffoldWriteContract`
  - `useScaffoldEventHistory`
- Use `externalContracts.ts` for our contracts; do not move them into SE2's built-in deploy system unless intentionally refactoring architecture.
- If changing block explorer decoding, keep support for both `deployedContracts.ts` and `externalContracts.ts`.
- Use DaisyUI/Tailwind components already present in SE2.

## UI-specific project extensions that must be preserved

- Custom pages:
  - `/users`
  - `/keys`
  - `/bloom`
- URL linking from Users to Keys: `/keys?address=0x...`
- Homepage and debug page use project-specific copy, not default SE2 boilerplate.
- `decodeTxData.ts` has local patches:
  - decode external contracts
  - label contract creation transactions explicitly

## Testing Notes / Gotchas

- `ui/` on OneDrive-mounted paths may be very slow for package installs and first builds.
- Docker avoids most filesystem performance issues; prefer it for repeatable local work.
- Next.js cache is persisted via Docker volume `nextjs-cache`; changes usually trigger only partial recompilation.
- If the UI behaves stale after major changes, clear the cache volume.

## When you modify these, also update docs

- signature sizes / payload layout
- pHash embedding behavior
- gas assumptions or simulated network costs
- Docker workflow
- ledger architecture, trust model, or duplicate detection
- SE2 UI customizations

Relevant docs:

- `docs/scheme-comparison.md`
- `docs/ethereum-ledger-proposal.md`
- `docs/video-extension.md`
- `docs/ui-se2-customizations.md`
