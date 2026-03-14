# Scaffold-ETH 2 UI Customizations

This document records all changes made to the upstream Scaffold-ETH 2 codebase in `ui/` for the image authentication ledger project.

The `ui/` directory started as a direct clone of the upstream `scaffold-eth/scaffold-eth-2` repository. The sections below describe only the project-specific modifications and extensions added on top of upstream SE2.

## Goals

The UI was extended to:

- connect to the local Hardhat node started by Docker
- load contract ABIs and addresses from the separate `ethereum-ledger/` project
- expose project-specific views for users, keys, and Bloom filter state
- improve the block explorer so it can decode our external contract transactions
- replace default SE2 placeholder copy with project-specific language

## Architecture Changes

## External Contracts Integration

Upstream SE2 expects contracts deployed by its own `packages/hardhat` project and reads them from `packages/nextjs/contracts/deployedContracts.ts`.

Our contracts live in a separate Hardhat project under `ethereum-ledger/`, so we integrated them as **external contracts**.

### Added files

- `ui/generate-contracts.js`
- `ui/packages/nextjs/contracts/externalContracts.ts`

### How it works

1. `ethereum-ledger/scripts/deploy.js` writes `deployment.json`
2. the node container exports `deployment.json` and Hardhat `artifacts/` to a shared Docker volume
3. the UI container waits for those files and runs `ui/generate-contracts.js`
4. that script generates `ui/packages/nextjs/contracts/externalContracts.ts`
5. SE2 hooks (`useScaffoldReadContract`, `useScaffoldWriteContract`, etc.) can then interact with our contracts normally

### Why external contracts instead of deployedContracts?

We intentionally kept `ethereum-ledger/` as a standalone project with its own:

- Solidity contracts
- Hardhat config
- deploy scripts
- E2E demo scripts

Using `externalContracts.ts` lets the UI consume those contracts without moving them into SE2's built-in Hardhat workspace.

## Dockerization

The full UI stack is containerized.

### Added files

- `ui/Dockerfile`
- `ui/entrypoint.sh`
- `ui/.dockerignore`

### Behavior

The UI container:

1. waits for the Hardhat node container to become healthy
2. waits for `deployment.json` and contract artifacts in the shared volume
3. generates `externalContracts.ts`
4. starts the Next.js dev server on `0.0.0.0:3000`

This avoids host-side `yarn install` and filesystem slowness from the OneDrive-mounted path.

## Persistent Next.js Cache

The Docker setup persists the Next.js build cache between restarts.

### Change

In `docker-compose.yml`, the UI service mounts:

- `nextjs-cache:/app/packages/nextjs/.next`

### Effect

- first startup: full Next.js compile (~40s)
- later restarts: cached startup (~2-3s)
- after source changes: partial recompilation (~5-10s for affected pages)

This is a UI/runtime customization, not a source-code change inside SE2 itself, but it is an important part of the developer experience.

## Block Explorer Fixes

### 1. Decode external contract function calls

#### Modified file

- `ui/packages/nextjs/utils/scaffold-eth/decodeTxData.ts`

#### Problem

Upstream SE2 only loaded ABIs from `deployedContracts.ts` when decoding function selectors for the block explorer.

As a result, transactions for our contracts in `externalContracts.ts` were shown as:

- `⚠️ Unknown`

#### Fix

We patched `decodeTxData.ts` to import and merge ABIs from both:

- `deployedContracts.ts`
- `externalContracts.ts`

This enables decoding of our external contract calls such as:

- `registerKey`
- `authorizeAdder`
- `add`
- `mightContain`

### 2. Label contract creation transactions correctly

#### Modified file

- `ui/packages/nextjs/utils/scaffold-eth/decodeTxData.ts`

#### Problem

Upstream SE2 attempted to skip contract-creation bytecode using the prefix:

```ts
0x60e06040
```

But standard Solidity creation bytecode typically starts with:

```ts
0x60806040
```

This caused contract deployment transactions to fall through to the function decoder and appear as `Unknown`.

#### Fix

We now detect contract-creation payloads and label them explicitly as:

- `📄 Contract Creation`

## Homepage Customization

#### Modified file

- `ui/packages/nextjs/app/page.tsx`

#### Changes

Replaced the default SE2 boilerplate homepage text and cards with project-specific content:

- title changed from `Scaffold-ETH 2` to `Image Authentication Ledger`
- removed placeholder instructions like “edit page.tsx” and “YourContract.sol”
- added navigation cards for:
  - `Users`
  - `Keys`
  - `Bloom Filter`
  - `Block Explorer`
- added a dedicated `Debug Contracts` panel

## Debug Page Customization

#### Modified file

- `ui/packages/nextjs/app/debug/page.tsx`

#### Changes

Replaced the default SE2 placeholder text:

- removed “Check packages / nextjs / app / debug / page.tsx”
- added project-specific explanation for:
  - `KeyRegistry`
  - `CrossChainBloomFilter`

## Header Navigation Extensions

#### Modified file

- `ui/packages/nextjs/components/Header.tsx`

#### Changes

Added new top-level navigation entries:

- `Users`
- `Keys`
- `Bloom Filter`
- existing `Debug Contracts`

Also added matching Heroicons for those routes.

## New Project-Specific Pages

These pages are not part of upstream SE2.

### Users page

#### Added files

- `ui/packages/nextjs/app/users/page.tsx`
- `ui/packages/nextjs/app/users/_components/UserList.tsx`

#### Features

- discovers registered users by scanning `KeyActivated` events
- deduplicates users client-side
- shows per-user summary:
  - address
  - key count
  - active scheme
  - last key activation time
- links each user to the Keys page

### Keys page

#### Added files

- `ui/packages/nextjs/app/keys/page.tsx`
- `ui/packages/nextjs/app/keys/_components/KeyList.tsx`

#### Features

- query any address for registered keys
- read `keyCount` and `activeKeyIndex`
- list all keys via `getKey(address, index)`
- show:
  - index
  - status (`Active` / `Revoked`)
  - scheme name
  - public key preview
  - activation timestamp
  - revocation timestamp
- form to register a new key
- form to revoke a key by index
- supports deep linking via query param:
  - `/keys?address=0x...`

### Bloom page

#### Added files

- `ui/packages/nextjs/app/bloom/page.tsx`
- `ui/packages/nextjs/app/bloom/_components/BloomStatus.tsx`

#### Features

- displays Bloom filter status:
  - total entries
  - filter size
  - number of hash functions
- lets the user input:
  - pHash
  - salt
- computes `keccak256(bytes24(pHash) || bytes2(salt))`
- queries `mightContain()` on-chain
- explains result:
  - definitely unique
  - might exist / retry salt

## URL-Driven Linking Between Pages

#### Modified file

- `ui/packages/nextjs/app/keys/_components/KeyList.tsx`

#### Change

Added support for `?address=` query parameters using `useSearchParams()`.

This allows:

- `Users` → `View Keys` → `/keys?address=...`

The Keys page automatically reads the address from the URL and pre-fills the query field.

## Generated External Contract File

#### Generated file

- `ui/packages/nextjs/contracts/externalContracts.ts`

#### Purpose

Contains the runtime addresses and ABIs for:

- `KeyRegistry`
- `CrossChainBloomFilter`
- optionally `ImageAuthResolver` (when deployed)

for the current local chain (`31337`) and any configured target networks.

This file is generated, not hand-maintained.

## Runtime / Workflow Changes

### Full stack startup

With the current setup, the UI is expected to be launched via:

```bash
docker compose up -d
```

This starts:

- the Hardhat node container
- the UI container

and wires them together automatically.

### Manual refresh cycle after contract changes

After Solidity changes:

1. rebuild/redeploy the node container
2. regenerate `externalContracts.ts`
3. restart the UI container

The containerized workflow handles most of this automatically.

## Summary of Upstream SE2 Code Changes

Modified upstream files:

- `ui/packages/nextjs/utils/scaffold-eth/decodeTxData.ts`
- `ui/packages/nextjs/app/page.tsx`
- `ui/packages/nextjs/app/debug/page.tsx`
- `ui/packages/nextjs/components/Header.tsx`

Added project-specific files:

- `ui/Dockerfile`
- `ui/entrypoint.sh`
- `ui/generate-contracts.js`
- `ui/.dockerignore`
- `ui/packages/nextjs/contracts/externalContracts.ts`
- `ui/packages/nextjs/app/users/...`
- `ui/packages/nextjs/app/keys/...`
- `ui/packages/nextjs/app/bloom/...`

These modifications turn a generic SE2 scaffold into a specialized explorer and operator console for the image authentication ledger.
