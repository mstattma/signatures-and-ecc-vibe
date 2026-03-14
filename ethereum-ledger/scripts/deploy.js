/**
 * Deploy all contracts and register the EAS schema on Base Sepolia.
 *
 * EAS on Base Sepolia:
 *   EAS: 0x4200000000000000000000000000000000000021
 *   SchemaRegistry: 0x4200000000000000000000000000000000000020
 *
 * Usage:
 *   npx hardhat run scripts/deploy.js --network baseSepolia
 *   npx hardhat run scripts/deploy.js --network hardhat  (local testing)
 */

const { ethers } = require("hardhat");

// EAS addresses on Base Sepolia (pre-deployed system contracts)
const EAS_ADDRESS = "0x4200000000000000000000000000000000000021";
const SCHEMA_REGISTRY_ADDRESS = "0x4200000000000000000000000000000000000020";

// Our schema definition
const SCHEMA_STRING =
  "bytes16 sigPrefix, bytes signature, uint8 scheme, bytes publicKey, bytes24 pHash, bytes2 salt, bytes32 fileHash, bytes32 metadataCID";

async function main() {
  const [deployer] = await ethers.getSigners();
  console.log("Deploying with account:", deployer.address);
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
  console.log("--- Deploying ImageAuthResolver ---");
  const Resolver = await ethers.getContractFactory("ImageAuthResolver");
  const resolver = await Resolver.deploy(EAS_ADDRESS, keyRegistryAddr, bloomFilterAddr);
  await resolver.waitForDeployment();
  const resolverAddr = await resolver.getAddress();
  console.log("ImageAuthResolver deployed to:", resolverAddr);

  // 4. Authorize the resolver to add to the Bloom filter
  console.log("--- Authorizing resolver on BloomFilter ---");
  const authTx = await bloomFilter.authorizeAdder(resolverAddr);
  await authTx.wait();
  console.log("Resolver authorized on BloomFilter");

  // 5. Register schema on EAS SchemaRegistry
  console.log("--- Registering EAS Schema ---");
  const schemaRegistry = await ethers.getContractAt(
    ["function register(string calldata schema, address resolver, bool revocable) external returns (bytes32)"],
    SCHEMA_REGISTRY_ADDRESS
  );

  const schemaTx = await schemaRegistry.register(SCHEMA_STRING, resolverAddr, true);
  const schemaReceipt = await schemaTx.wait();

  // Extract schemaUID from the Registered event
  const schemaRegisteredEvent = schemaReceipt.logs.find(
    (log) => log.topics.length > 0
  );
  const schemaUID = schemaRegisteredEvent ? schemaRegisteredEvent.topics[1] : "unknown";
  console.log("Schema registered, UID:", schemaUID);

  // Print summary
  console.log("");
  console.log("=== DEPLOYMENT SUMMARY ===");
  console.log("Network:              Base Sepolia");
  console.log("Deployer:            ", deployer.address);
  console.log("KeyRegistry:         ", keyRegistryAddr);
  console.log("CrossChainBloomFilter:", bloomFilterAddr);
  console.log("ImageAuthResolver:   ", resolverAddr);
  console.log("Schema UID:          ", schemaUID);
  console.log("EAS:                 ", EAS_ADDRESS);
  console.log("SchemaRegistry:      ", SCHEMA_REGISTRY_ADDRESS);
  console.log("");
  console.log("Save these addresses in your .env file:");
  console.log(`KEY_REGISTRY=${keyRegistryAddr}`);
  console.log(`BLOOM_FILTER=${bloomFilterAddr}`);
  console.log(`RESOLVER=${resolverAddr}`);
  console.log(`SCHEMA_UID=${schemaUID}`);

  return {
    keyRegistry: keyRegistryAddr,
    bloomFilter: bloomFilterAddr,
    resolver: resolverAddr,
    schemaUID,
  };
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
