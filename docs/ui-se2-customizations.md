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

User-visible result:

- external contract transactions in the block explorer now show real function names instead of `⚠️ Unknown`

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

User-visible result:

- contract deployment transactions no longer appear as `Unknown`
- the transaction list now clearly distinguishes deployments from normal function calls

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

### Event-driven user discovery

The Users page intentionally does **not** depend on a dedicated user registry contract.
Instead, it derives the user list from historical `KeyActivated` events.

Implications:

- if an address has never emitted `KeyActivated`, it will not appear
- the page is indexed by event history, not by explicit contract storage iteration
- this keeps on-chain complexity low while still providing a useful user directory

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

### State-driven key details

Unlike the Users page, the Keys page reads live contract state:

- `keyCount(address)`
- `activeKeyIndex(address)`
- `getKey(address, index)`

So the UI combines:

- **event history** to discover which users exist
- **contract reads** to show each user's current and historical key state

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

### Images page

#### Added files

- `ui/packages/nextjs/app/images/page.tsx`
- `ui/packages/nextjs/app/images/_components/ImageSearch.tsx`
- `ui/packages/nextjs/app/images/[uid]/page.tsx`

#### Features

- **Recent Image Records**: lists the latest 20 `ImageRegistered` events, decoded from EAS attestations with full field display (signature, salt, scheme, attester, pHash, fileName)
- **Search by Signature**: takes full signature hex, computes sig prefix (first 16 bytes), queries `resolver.sigPrefixIndex()`
- **Search by pHash + Salt**: computes `keccak256(uint16(version) || bytes24(pHash) || bytes2(salt))`, queries `resolver.pHashSaltIndex()`
- **Search by Public Key**: filters loaded recent records by PK, linked from `/keys` page
- **Image Detail Page** (`/images/[uid]`): shows full decoded attestation: UID, attester, timestamp, scheme, signature, PK, pHash, pHash version, salt, fileHash, metadataCID, fileName
- Attester addresses link to `/keys?address=...`
- Attestation UIDs link to `/images/[uid]`
- Graceful fallback when ImageAuthResolver or EAS are not deployed on the current chain

### Local EAS deployment

The localhost Docker deployment now includes a real local EAS stack (`SchemaRegistry` + `EAS`), so the Images page works on localhost, not only on testnets. This required:

- `ethereum-ledger/contracts/LocalEASImports.sol` — forces Hardhat to compile EAS artifacts
- `ui/generate-contracts.js` — updated to export `EAS` and `SchemaRegistry` from the dependency package
- `docker-compose.yml` — mounts host `ethereum-ledger/artifacts` into the UI container so EAS ABIs are available

## URL-Driven Linking Between Pages

#### Modified file

- `ui/packages/nextjs/app/keys/_components/KeyList.tsx`

#### Change

Added support for `?address=` query parameters using `useSearchParams()`.

This allows:

- `Users` → `View Keys` → `/keys?address=...`

The Keys page automatically reads the address from the URL and pre-fills the query field.

### Keys → Images deep link

Public key cells on the Keys page now link to `/images?publicKey=0x...`. The Images page reads this parameter and filters loaded recent records by matching PK.

Flow: `Users` → `View Keys` → click a public key → `/images?publicKey=...` → see all images signed with that key.

## Generated External Contract File

#### Generated file

- `ui/packages/nextjs/contracts/externalContracts.ts`

#### Purpose

Contains the runtime addresses and ABIs for:

- `KeyRegistry`
- `CrossChainBloomFilter`
- `ImageAuthResolver` (when deployed)
- `EAS` (when deployed locally)
- `SchemaRegistry` (when deployed locally)

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
- `ui/packages/nextjs/app/images/...` (search + detail pages)
- `ui/packages/nextjs/app/images/[uid]/...` (dynamic detail route)

These modifications turn a generic SE2 scaffold into a specialized explorer and operator console for the image authentication ledger.

## Planned UI Updates (Not Yet Implemented)

The following updates are needed to support the contracts and attestation schema changes from the C2PA integration work. The contracts have been updated (and deployed on local Hardhat), but the UI has not yet been changed to reflect them.

### Priority: Critical

**1. Regenerate `externalContracts.ts`**

The current file has stale addresses and a stale `KeyRegistry` ABI missing all C2PA methods. After rebuilding the Docker stack, `generate-contracts.js` should pick up the new contracts automatically. The following must be verified:

- `KeyRegistry` ABI includes C2PA methods (`registerC2PAKey`, `revokeC2PAKey`, `getC2PAKeyByCertHash`, `getC2PAKey`, `c2paKeyCount`, `C2PAKeyActivated`/`C2PAKeyRevoked` events)
- `ImageAuthResolver` ABI includes new functions (`c2paLookup`, `c2paLookupJSON`, `c2paSchema`, `isDuplicate`, `mightBeDuplicateCrossChain`)
- `ReputationRegistry` is present with full ABI
- All addresses match `deployment.json`

Also: add `ReputationRegistry: "reputationRegistry"` to the `CONTRACTS` map in `generate-contracts.js`, and align `export-abis.js` to include the same set of contracts.

**2. Update attestation schema in image pages**

Files: `ImageSearch.tsx`, `[uid]/page.tsx`

The `IMAGE_ATTESTATION_TYPES` array must be updated from 10 fields to 12:

```typescript
// Old (10 fields):
// sigPrefix, signature, scheme, publicKey, pHash, pHashVersion, salt, fileHash, metadataCID, fileName

// New (12 fields):
// sigPrefix, signature, scheme, publicKey, pHash, pHashVersion, salt, fileHash, metadataCID, c2paCertHash, c2paSig, fileName
```

All `decoded[N]` index references from index 8 onward must be shifted:
- `decoded[8]` = `metadataCID` (unchanged)
- `decoded[9]` = `c2paCertHash` (NEW)
- `decoded[10]` = `c2paSig` (NEW)
- `decoded[11]` = `fileName` (was `decoded[9]`)

Display the new fields:
- `c2paCertHash`: hex with link to KeyRegistry C2PA key lookup
- `c2paSig`: hex (truncated with expand, ~128 hex chars)

### Priority: Major

**3. C2PA key support on Keys page**

File: `KeyList.tsx`

Add a new section "C2PA Signing Keys" below the existing BLS/UOV keys section:

- Query `c2paKeyCount(address)` and `activeC2PAKeyIndex(address)` 
- For each C2PA key, call `getC2PAKey(address, index)` and render: `pubKeyX`, `pubKeyY` (hex), `certHash` (hex), `certCID` (with IPFS link), `activatedAt`, `revokedAt`, active/revoked status
- Add a "Register C2PA Key" form with fields: `pubKeyX`, `pubKeyY`, `certHash`, `certCID` → calls `registerC2PAKey`
- Add a "Revoke C2PA Key" form → calls `revokeC2PAKey(keyIndex)`

**4. Reputation page**

New files:
- `app/reputation/page.tsx`
- `app/reputation/_components/ReputationView.tsx`

Features:
- Address lookup showing all 5 `getReputation()` fields: `attestationCount`, `endorsementScore`, `disputeCount`, `disputesWon`, `firstSeenAt`
- Computed `score(address)` displayed prominently
- Endorse/downvote buttons calling `endorse(address, bool)` (requires connected wallet with registered key)
- Recent `Endorsed`, `DisputeRecorded`, `AttestationCounted` events
- Deep link support: `/reputation?address=0x...`

**5. Add nav link for Reputation**

File: `Header.tsx`

Add "Reputation" to the nav bar (e.g., `ShieldCheckIcon` from heroicons), between "Bloom Filter" and "Debug Contracts".

### Priority: Moderate

**6. Users page — reputation integration**

File: `UserList.tsx`

- Also listen for `C2PAKeyActivated` events to discover users who only have C2PA keys (currently only listens for `KeyActivated`)
- For each user, call `ReputationRegistry.score(address)` and display a reputation column
- Add attestation count column from `ReputationRegistry.getReputation(address)`

**7. Homepage updates**

File: `page.tsx`

- Add "Reputation" card alongside existing Users/Keys/Images/Bloom/Explorer cards
- Update "Debug Contracts" description to mention ImageAuthResolver and ReputationRegistry (currently only mentions KeyRegistry and CrossChainBloomFilter)
- Mention C2PA integration in the hero text

**8. Debug page text**

File: `debug/page.tsx`

- Add descriptions for `ImageAuthResolver` and `ReputationRegistry` alongside existing KeyRegistry and CrossChainBloomFilter descriptions

### Priority: Minor

**9. Extract `SCHEME_NAMES` to shared utility**

The same scheme name map is duplicated in 4 files: `KeyList.tsx`, `ImageSearch.tsx`, `[uid]/page.tsx`, `UserList.tsx`. Extract to a shared file (e.g., `utils/schemeNames.ts`). Also add a future entry for ES256/P-256 if C2PA attestations use a distinct scheme number.

**10. C2PA binding search on Images page**

Optional enhancement: add a "Search by C2PA Binding" form on the Images page that calls `c2paLookup(alg, bindingValue)` or `c2paLookupJSON(inputJSON)` and displays the result. This would demonstrate the DLT K-V lookup directly from the UI.
