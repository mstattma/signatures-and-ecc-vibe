/**
 * Deploy all contracts and optionally register the EAS schema.
 *
 * EAS on Base Sepolia:
 *   EAS: 0x4200000000000000000000000000000000000021
 *   SchemaRegistry: 0x4200000000000000000000000000000000000020
 *
 * Usage:
 *   npx hardhat node                                            # start local node (terminal 1)
 *   npx hardhat run scripts/deploy.js --network localhost       # deploy to local node
 *   npx hardhat run scripts/deploy.js --network baseSepolia     # deploy to testnet
 *   npx hardhat run scripts/deploy.js --network hardhat         # ephemeral (dies after script)
 */

const { ethers } = require("hardhat");
const fs = require("fs");

const EAS_ADDRESS = "0x4200000000000000000000000000000000000021";
const SCHEMA_REGISTRY_ADDRESS = "0x4200000000000000000000000000000000000020";

const SCHEMA_STRING =
  "bytes16 sigPrefix, bytes signature, uint8 scheme, bytes publicKey, bytes24 pHash, bytes2 salt, bytes32 fileHash, bytes32 metadataCID";

async function main() {
  const [deployer] = await ethers.getSigners();
  const network = await ethers.provider.getNetwork();
  const isLocal = network.chainId === 31337n;

  console.log("Deploying with account:", deployer.address);
  console.log("Network:", isLocal ? `local (chainId ${network.chainId})` : `chainId ${network.chainId}`);
  console.log("Balance:", ethers.formatEther(await ethers.provider.getBalance(deployer.address)), "ETH");
  console.log("");

  // 1. Deploy KeyRegistry
  console.log("--- Deploying KeyRegistry ---");
  const KeyRegistry = await ethers.getContractFactory("KeyRegistry");
  const keyRegistry = await KeyRegistry.deploy();
  await keyRegistry.waitForDeployment();
  const keyRegistryAddr = await keyRegistry.getAddress();
  console.log("KeyRegistry deployed to:", keyRegistryAddr);

  // 2. Deploy CrossChainBloomFilter
  console.log("--- Deploying CrossChainBloomFilter ---");
  const BloomFilter = await ethers.getContractFactory("CrossChainBloomFilter");
  const bloomFilter = await BloomFilter.deploy();
  await bloomFilter.waitForDeployment();
  const bloomFilterAddr = await bloomFilter.getAddress();
  console.log("CrossChainBloomFilter deployed to:", bloomFilterAddr);

  // 3. Deploy ImageAuthResolver
  // On local network, skip resolver deployment (EAS not available, SchemaResolver rejects address(0))
  let resolverAddr = ethers.ZeroAddress;
  if (!isLocal) {
    console.log("--- Deploying ImageAuthResolver ---");
    const Resolver = await ethers.getContractFactory("ImageAuthResolver");
    const resolver = await Resolver.deploy(EAS_ADDRESS, keyRegistryAddr, bloomFilterAddr);
    await resolver.waitForDeployment();
    resolverAddr = await resolver.getAddress();
    console.log("ImageAuthResolver deployed to:", resolverAddr);
  } else {
    console.log("--- Skipping ImageAuthResolver (no EAS on local network) ---");
    console.log("  KeyRegistry and BloomFilter can be tested directly.");
  }

  // 4. Authorize adders on the Bloom filter
  console.log("--- Authorizing on BloomFilter ---");
  if (resolverAddr !== ethers.ZeroAddress) {
    const authTx = await bloomFilter.authorizeAdder(resolverAddr);
    await authTx.wait();
    console.log("  Resolver authorized");
  }
  // Authorize deployer for direct testing (local and testnet)
  const authTx2 = await bloomFilter.authorizeAdder(deployer.address);
  await authTx2.wait();
  console.log("  Deployer authorized");

  // 5. Register schema on EAS SchemaRegistry (testnet only)
  let schemaUID = "0x" + "00".repeat(32);
  if (!isLocal) {
    console.log("--- Registering EAS Schema ---");
    const schemaRegistry = await ethers.getContractAt(
      ["function register(string calldata schema, address resolver, bool revocable) external returns (bytes32)"],
      SCHEMA_REGISTRY_ADDRESS
    );
    const schemaTx = await schemaRegistry.register(SCHEMA_STRING, resolverAddr, true);
    const schemaReceipt = await schemaTx.wait();
    const schemaRegisteredEvent = schemaReceipt.logs.find((log) => log.topics.length > 0);
    schemaUID = schemaRegisteredEvent ? schemaRegisteredEvent.topics[1] : "unknown";
    console.log("Schema registered, UID:", schemaUID);
  } else {
    console.log("--- Skipping EAS Schema Registration (local network) ---");
  }

  // Print summary
  console.log("");
  console.log("=== DEPLOYMENT SUMMARY ===");
  console.log("Network:              ", isLocal ? "localhost / hardhat" : `chainId ${network.chainId}`);
  console.log("Deployer:            ", deployer.address);
  console.log("KeyRegistry:         ", keyRegistryAddr);
  console.log("CrossChainBloomFilter:", bloomFilterAddr);
  console.log("ImageAuthResolver:   ", resolverAddr);
  console.log("Schema UID:          ", schemaUID);

  // Save addresses to deployment.json
  const deployment = {
    network: network.chainId.toString(),
    deployer: deployer.address,
    keyRegistry: keyRegistryAddr,
    bloomFilter: bloomFilterAddr,
    resolver: resolverAddr,
    schemaUID,
    eas: isLocal ? ethers.ZeroAddress : EAS_ADDRESS,
    schemaRegistry: isLocal ? ethers.ZeroAddress : SCHEMA_REGISTRY_ADDRESS,
    timestamp: new Date().toISOString(),
  };
  fs.writeFileSync("deployment.json", JSON.stringify(deployment, null, 2));
  console.log("\nSaved to deployment.json");

  return deployment;
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
