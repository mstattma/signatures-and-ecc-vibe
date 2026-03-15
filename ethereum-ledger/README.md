# Ethereum Ledger Implementation

Solidity smart contracts and scripts for on-chain image authentication registration, key lifecycle management, reputation tracking, and cross-chain duplicate detection. Uses the [Ethereum Attestation Service (EAS)](https://attest.org) for attestations.

See [docs/ethereum-ledger-proposal.md](../docs/ethereum-ledger-proposal.md) for the full design rationale.

## Contracts

| Contract | Purpose | Gas (deploy) | Gas (per call) |
|---|---|---|---|
| **KeyRegistry** | Signing key lifecycle (register, rotate, revoke, validity checks) | ~600K | ~140K (register), free (query) |
| **CrossChainBloomFilter** | Cross-chain `(pHash, salt)` duplicate detection via Bloom filter | ~1.3M | ~270K (add), free (query) |
| **ImageAuthResolver** | EAS resolver enforcing uniqueness, key validity, Bloom update, sig-prefix index | ~800K | ~200K (via EAS attest) |
| **ReputationRegistry** | User/image reputation scoring (0-1000 points) | ~1.2M | ~50-60K (rate), free (score query) |

The ReputationRegistry is defined in the [Ethereum Ledger Proposal](../docs/ethereum-ledger-proposal.md#reputation-system) but not yet deployed in the demo scripts (planned for Phase 2). The other three contracts are fully implemented and tested.

### Attestation schema

The current EAS attestation schema is:

```
bytes16 sigPrefix, bytes signature, uint8 scheme, bytes publicKey,
bytes24 pHash, uint16 pHashVersion, bytes2 salt,
bytes32 fileHash, bytes32 metadataCID, string fileName
```

- `pHashVersion` (added later) enables evolving the perceptual hash algorithm without breaking existing attestations
- `fileName` (added later) stores the original file name for identification in the UI and records
- Dedup key: `keccak256(pHashVersion || pHash || salt)`

## Quick Start

### Prerequisites

```bash
npm install    # Install dependencies (hardhat, EAS contracts, ethers, etc.)
```

### Local Testing (ephemeral, single command)

```bash
npx hardhat run scripts/demo.js --network hardhat
```

This deploys fresh contracts in a temporary Hardhat VM, runs the full demo, and exits. State is lost after the script finishes. Good for quick smoke tests.

### Local Testing with Docker (recommended)

The easiest way to run a persistent local node is via Docker. The container starts a Hardhat node, auto-deploys all contracts, and exports `deployment.json` to the host.

```bash
# From the project root:
docker compose up -d           # Start in background
docker compose logs -f node    # Follow logs (see deployment output)
```

The node is available at `http://localhost:8545`. Contracts are auto-deployed on startup. `deployment.json` is written to `ethereum-ledger/` on the host.

The local deployment now includes a real EAS stack:

- `SchemaRegistry`
- `EAS`
- `ImageAuthResolver`

This means localhost now supports the same attestation/resolver flow as testnet, including image lookup by signature prefix and by `(pHash, salt)`.

Run scripts against the containerized node:
```bash
cd ethereum-ledger
npx hardhat run scripts/demo.js --network localhost
npx hardhat run scripts/demo.js --network localhost   # state persists!
```

Manage the container:
```bash
docker compose down            # Stop and remove
docker compose up -d           # Restart (fresh state, re-deploys)
docker compose logs -f node    # Watch logs
```

**Note:** Container state is in-memory. `docker compose down` + `up` resets everything (new contract addresses). The `deployment.json` on the host is updated automatically on each restart.

### Local Testing without Docker (manual)

If you prefer not to use Docker, start the Hardhat node manually:

**Terminal 1** — start the local blockchain node:
```bash
npx hardhat node
```

This starts a JSON-RPC server at `http://127.0.0.1:8545` with 20 pre-funded test accounts (10,000 ETH each). The node stays running and state persists as long as the process is alive.

**Terminal 2** — deploy contracts (once):
```bash
npx hardhat run scripts/deploy.js --network localhost
```

**Terminal 2** — run the demo (as many times as you want):
```bash
npx hardhat run scripts/demo.js --network localhost
```

The demo reads contract addresses from `deployment.json` and reuses the deployed contracts. Each run registers a new key and image, and the Bloom filter accumulates entries across runs. If `deployment.json` is stale (node restarted), the demo detects it and tells you to re-run `deploy.js`.

### Testnet Deployment (Base Sepolia)

1. Get testnet ETH from a [Base Sepolia faucet](https://www.alchemy.com/faucets/base-sepolia)
2. Configure `.env`:
   ```bash
   cp .env.example .env
   # Edit .env: set PRIVATE_KEY to your funded wallet's private key
   ```
3. Deploy:
   ```bash
   npx hardhat run scripts/deploy.js --network baseSepolia
   ```
4. Run the demo:
   ```bash
   npx hardhat run scripts/demo.js --network baseSepolia
   ```

On testnet, the same full EAS flow is used: schema registration, resolver enforcement, and attestation creation. The demo creates a real EAS attestation and looks it up by signature prefix.

## Demo Output

The demo simulates a BLS-BN158 image authentication flow:

```
Scheme: BLS-BN158 (78-bit classical security, NOT quantum-safe)
Stego payload: salt || sig = 23 bytes = 184 bits
pHash: NOT in payload (loaded from ledger)
PK: NOT in payload (loaded from ledger)
```

Steps:
1. **Key registration**: Generate a BLS-BN158 key pair, register on KeyRegistry
2. **Sign a pHash**: Simulate BLS signing of a 144-bit perceptual hash with 16-bit salt
3. **Stego payload**: Show the 184-bit payload (salt + signature only, no pHash, no PK)
4. **Bloom filter check**: Verify `(pHash, salt)` is unique across chains
5. **Ledger registration**: Create EAS attestation through the resolver (localhost or testnet)
6. **Verification**: Extract signature from payload, look up PK and pHash from the ledger via the resolver/EAS
7. **Duplicate rejection**: Re-registration blocked; salt retry succeeds

## Project Structure

```
ethereum-ledger/
├── README.md                   # This file
├── Dockerfile                  # Hardhat node container (auto-deploys on startup)
├── entrypoint.sh               # Container entrypoint (start node + deploy)
├── hardhat.config.js           # Hardhat config (localhost, baseSepolia, localNova)
├── package.json                # Dependencies
├── .env.example                # Template for private key and contract addresses
├── .gitignore                  # Excludes node_modules, artifacts, .env, deployment.json
├── .dockerignore               # Excludes node_modules from Docker build context
├── contracts/
│   ├── KeyRegistry.sol         # Signing key lifecycle management
│   ├── CrossChainBloomFilter.sol  # Cross-chain duplicate detection
│   └── ImageAuthResolver.sol   # EAS resolver (uniqueness + key check + Bloom + index)
└── scripts/
    ├── deploy.js               # Deploy all contracts, register EAS schema, save deployment.json
    ├── demo.js                 # End-to-end BLS-BN158 demo (local or testnet)
    ├── health-check.js         # Simple node health check
    └── export-abis.js          # Export ABIs for Scaffold-ETH 2 UI
```

## Hardhat Network Modes

| Mode | Command | Persistence | Contracts reused | Use case |
|---|---|---|---|---|
| **Ephemeral** | `npx hardhat run script.js` | None | No (fresh each run) | Quick smoke tests |
| **Persistent node** | `npx hardhat node` + `--network localhost` | In-memory (while node runs) | Yes (via deployment.json) | User testing, interactive dev |
| **Testnet** | `--network baseSepolia` | On-chain (permanent) | Yes (via deployment.json or env) | Integration testing, production |

**Note:** The persistent node (`npx hardhat node`) keeps state in memory only. When you stop the node, all deployed contracts and state are lost. For permanent persistence, deploy to a testnet. If the demo detects stale addresses (contracts not found at deployment.json addresses), it falls back to a fresh ephemeral deployment automatically.

## Gas Cost Estimation

The demo prints USD cost estimates alongside every gas usage figure. The network is auto-detected from the chainId and the corresponding gas price is used:

| Network | ChainId | Gas price (gwei) | Example: 200K gas | Notes |
|---|---|---|---|---|
| **Base** | 8453 | 0.01 | ~$0.006 | Optimistic rollup (OP Stack), Coinbase |
| **Base Sepolia** | 84532 | 0.01 | ~$0.006 | Testnet |
| **Optimism** | 10 | 0.01 | ~$0.006 | Optimistic rollup (OP Stack) |
| **Arbitrum Nova** | 42170 | 0.002 | ~$0.0012 | AnyTrust (DAC), cheapest |
| **Ethereum L1** | 1 | 30 | ~$18.00 | Mainnet; too expensive for per-image use |
| **localhost / hardhat** | 31337 | 0.01 | ~$0.006 | Local testing; uses Base-equivalent pricing |

Default ETH price: **$3,000**. Override with environment variables:

```bash
# Custom ETH price
ETH_PRICE_USD=2500 npx hardhat run scripts/demo.js --network localhost

# Example output:
#   Key registered! 200353 gas (~$0.0050 on localhost (Base-equivalent) at ETH=$2500)
```

### Simulating Different Networks Locally

You can test any network's gas pricing against a local Hardhat node using the `SIMULATE_NETWORK` environment variable. The contracts and logic are identical — only the cost display changes.

```bash
# Start the local node (terminal 1)
npx hardhat node

# Deploy once (terminal 2)
npx hardhat run scripts/deploy.js --network localhost

# Run as Base (default)
npx hardhat run scripts/demo.js --network localhost
#   Key registered! 200353 gas (~$0.0060 on localhost (Base-equivalent) at ETH=$3000)

# Simulate Arbitrum Nova pricing (5x cheaper)
SIMULATE_NETWORK=arbitrumNova npx hardhat run scripts/demo.js --network localhost
#   Key registered! 200353 gas (~$0.0012 on Arbitrum Nova at ETH=$3000)

# Simulate Ethereum L1 pricing (3000x more expensive)
SIMULATE_NETWORK=l1 npx hardhat run scripts/demo.js --network localhost
#   Key registered! 200353 gas (~$18.03 on Ethereum L1 at ETH=$3000)

# Simulate Optimism pricing
SIMULATE_NETWORK=optimism npx hardhat run scripts/demo.js --network localhost
```

Available `SIMULATE_NETWORK` values: `base`, `optimism`, `arbitrumNova`, `l1`, `baseSepolia`, `localhost`.

**Note on L2 gas pricing:** The gas prices above are approximate effective rates that include both L2 execution cost and the amortized L1 data posting fee (which L2s charge as part of the L2 gas price). Actual costs vary with L1 gas prices and network congestion. Arbitrum Nova is 5-10x cheaper than Base/Optimism because it uses a Data Availability Committee (DAC) instead of posting all data to L1.

## Web UI (Scaffold-ETH 2)

A [Scaffold-ETH 2](https://scaffoldeth.io/) UI is included in the `ui/` directory at the project root. It provides auto-generated interactive pages for all contracts (read/write every function, wallet connection, event logs).

The UI is fully containerized — `docker compose up` starts both the Hardhat node and the UI:

```bash
# From the project root:
docker compose up -d           # Starts node (port 8545) + UI (port 3000)
docker compose logs -f         # Follow all logs
```

Open **http://localhost:3000/debug** to interact with the contracts. The debug page shows:
- **KeyRegistry**: register/rotate/revoke keys, query validity
- **CrossChainBloomFilter**: add entries, check duplicates, view filter state

### Custom UI pages

In addition to the generic SE2 debug page, the UI exposes project-specific routes:

| Route | Purpose |
|---|---|
| `/users` | Shows all users discovered from `KeyActivated` events |
| `/keys` | Lists key history for a selected address; register/revoke keys |
| `/bloom` | Shows Bloom filter status and lets you query `(pHash, salt)` duplicates |
| `/debug` | Raw SE2 contract interaction page |
| `/blockexplorer` | Transaction explorer with decoded external contract function calls |

Notes:

- **Users page is event-driven.** It discovers users by scanning `KeyActivated` events from block 0, not from a dedicated on-chain user registry.
- **Keys page is state-driven.** It reads `keyCount`, `activeKeyIndex`, and `getKey(address, index)` from `KeyRegistry`.
- **Deep linking works** from the Users page to the Keys page via `/keys?address=0x...`.
- The block explorer now labels contract deployments as **`📄 Contract Creation`** and decodes our external contract calls instead of showing `Unknown`.

The UI container automatically:
1. Waits for the node container to be healthy (contracts deployed)
2. Reads `deployment.json` and contract artifacts from a shared volume
3. Generates `externalContracts.ts` with the correct addresses and ABIs
4. Starts the Next.js dev server

**Next.js cache persistence:** The `.next/` compilation cache is stored in a Docker volume (`nextjs-cache`) that survives container restarts. First page load takes ~40 seconds (full compilation); subsequent restarts serve pages in ~2-3 seconds. When contract ABIs change (new deployment), Next.js automatically recompiles only affected pages (~5-10 seconds). To force a full recompile: `docker volume rm signatures-and-ecc-vibe_nextjs-cache`.

### Cross-chain duplicate detection

The `CrossChainBloomFilter` contract provides trustless cross-chain `(pHash, salt)` deduplication. Each chain maintains a Bloom filter that is periodically synced from other chains by a relayer. A complementary off-chain indexer provides fast-path lookups across all chains.

- **Bloom filter**: 2 KB (16,384 bits), 10 hash functions, ~5,000 entries at <1% false positive rate
- **False positive**: causes a harmless salt retry (client picks a new salt and retries)
- **False negative**: guaranteed impossible (if an entry was added, it will be detected)
- **Sync**: relayer reads `BloomUpdated` events from each chain and calls `syncFromChain()` on all others

See the [Ethereum Ledger Proposal](../docs/ethereum-ledger-proposal.md#cross-chain-duplicate-detection) for the full design, Solidity contract, and cost analysis.

### Manual UI setup (without Docker)

If you prefer to run the UI outside Docker:

```bash
# 1. Start the Hardhat node (Docker or manual)
docker compose up -d node      # only the node container

# 2. Export contract ABIs to the UI
cd ethereum-ledger
node scripts/export-abis.js

# 3. Install UI dependencies and start
cd ../ui
yarn install
yarn workspace @se-2/nextjs dev
```

### Updating contract ABIs

After modifying Solidity contracts, rebuild the node container:

```bash
docker compose build node
docker compose up -d           # Redeploys contracts and regenerates ABIs in UI
```

Or manually:
```bash
cd ethereum-ledger
npx hardhat compile
node scripts/export-abis.js    # Regenerates ui/packages/nextjs/contracts/externalContracts.ts
```

### SE2 modification for external contract support

Scaffold-ETH 2's block explorer only decodes transaction function names for contracts deployed via its built-in deploy system (`deployedContracts.ts`). Since our contracts are deployed by `ethereum-ledger/scripts/deploy.js` and registered as external contracts (`externalContracts.ts`), the block explorer would show "Unknown" for all function calls.

We patched `ui/packages/nextjs/utils/scaffold-eth/decodeTxData.ts` to also load ABIs from `externalContracts.ts`:

```typescript
// Original: only loads deployedContracts
import contractData from "~~/contracts/deployedContracts";

// Patched: also loads externalContracts
import contractData from "~~/contracts/deployedContracts";
import externalContractData from "~~/contracts/externalContracts";
```

Both sets of ABIs are merged into the `interfaces` lookup table used by `decodeTransactionData()`. This enables the block explorer to decode function names like `registerKey`, `authorizeAdder`, `add`, `mightContain`, etc. for our external contracts.

**Why external contracts?** Our contracts live in a separate Hardhat project (`ethereum-ledger/`) with its own deploy scripts, rather than in SE2's `packages/hardhat/`. The `export-abis.js` script bridges the two by reading Hardhat artifacts and `deployment.json` to generate `externalContracts.ts`. This keeps `ethereum-ledger/` as a standalone project that can be used independently of the UI.
