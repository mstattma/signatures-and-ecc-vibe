# Ethereum Ledger Implementation

Solidity smart contracts and scripts for on-chain image authentication registration, key lifecycle management, reputation tracking, and cross-chain duplicate detection. Uses the [Ethereum Attestation Service (EAS)](https://attest.org) for attestations.

See [docs/ethereum-ledger-proposal.md](../docs/ethereum-ledger-proposal.md) for the full design rationale.

## Contracts

| Contract | Purpose | Gas (deploy) | Gas (per call) |
|---|---|---|---|
| **KeyRegistry** | Signing key lifecycle (register, rotate, revoke, validity checks) | ~600K | ~140K (register), free (query) |
| **CrossChainBloomFilter** | Cross-chain `(pHash, salt)` duplicate detection via Bloom filter | ~1.3M | ~270K (add), free (query) |
| **ImageAuthResolver** | EAS resolver enforcing uniqueness, key validity, Bloom update, sig-prefix index | ~800K | ~200K (via EAS attest) |

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

### Local Testing (persistent, multi-session)

For interactive/user testing where state persists across multiple script runs:

**Terminal 1** — start the local blockchain node:
```bash
npx hardhat node
```

This starts a JSON-RPC server at `http://127.0.0.1:8545` with 20 pre-funded test accounts (10,000 ETH each). The node stays running and state persists as long as the process is alive.

**Terminal 2** — deploy contracts (once):
```bash
npx hardhat run scripts/deploy.js --network localhost
```

This deploys KeyRegistry, CrossChainBloomFilter (and ImageAuthResolver on testnet), saves all addresses to `deployment.json`, and authorizes the deployer on the Bloom filter.

**Terminal 2** — run the demo (as many times as you want):
```bash
npx hardhat run scripts/demo.js --network localhost
npx hardhat run scripts/demo.js --network localhost   # state persists!
```

The demo reads contract addresses from `deployment.json` and reuses the deployed contracts. Each run registers a new key and image, and the Bloom filter accumulates entries across runs. If `deployment.json` is stale (node restarted), the demo detects this and falls back to fresh deployment.

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

On testnet, the full EAS flow is used: schema registration, resolver enforcement, and attestation creation. The demo creates a real EAS attestation and looks it up by signature prefix.

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
5. **Ledger registration**: Create EAS attestation (testnet) or direct Bloom add (local)
6. **Verification**: Extract signature from payload, look up PK and pHash from ledger
7. **Duplicate rejection**: Re-registration blocked; salt retry succeeds

## Project Structure

```
ethereum-ledger/
├── README.md                   # This file
├── hardhat.config.js           # Hardhat config (localhost, baseSepolia)
├── package.json                # Dependencies
├── .env.example                # Template for private key and contract addresses
├── .gitignore                  # Excludes node_modules, artifacts, .env, deployment.json
├── contracts/
│   ├── KeyRegistry.sol         # Signing key lifecycle management
│   ├── CrossChainBloomFilter.sol  # Cross-chain duplicate detection
│   └── ImageAuthResolver.sol   # EAS resolver (uniqueness + key check + Bloom + index)
└── scripts/
    ├── deploy.js               # Deploy all contracts, register EAS schema, save deployment.json
    └── demo.js                 # End-to-end BLS-BN158 demo (local or testnet)
```

## Hardhat Network Modes

| Mode | Command | Persistence | Contracts reused | Use case |
|---|---|---|---|---|
| **Ephemeral** | `npx hardhat run script.js` | None | No (fresh each run) | Quick smoke tests |
| **Persistent node** | `npx hardhat node` + `--network localhost` | In-memory (while node runs) | Yes (via deployment.json) | User testing, interactive dev |
| **Testnet** | `--network baseSepolia` | On-chain (permanent) | Yes (via deployment.json or env) | Integration testing, production |

**Note:** The persistent node (`npx hardhat node`) keeps state in memory only. When you stop the node, all deployed contracts and state are lost. For permanent persistence, deploy to a testnet. If the demo detects stale addresses (contracts not found at deployment.json addresses), it falls back to a fresh ephemeral deployment automatically.
