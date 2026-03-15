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

  // 1. Deploy EAS stack (local only) or use predeployed Base Sepolia system contracts
  let easAddress = EAS_ADDRESS;
  let schemaRegistryAddress = SCHEMA_REGISTRY_ADDRESS;

  if (isLocal) {
    console.log("--- Deploying local SchemaRegistry ---");
    const SchemaRegistry = await ethers.getContractFactory("SchemaRegistry");
    const schemaRegistry = await SchemaRegistry.deploy();
    await schemaRegistry.waitForDeployment();
    schemaRegistryAddress = await schemaRegistry.getAddress();
    console.log("SchemaRegistry deployed to:", schemaRegistryAddress);

    console.log("--- Deploying local EAS ---");
    const EAS = await ethers.getContractFactory("EAS");
    const eas = await EAS.deploy(schemaRegistryAddress);
    await eas.waitForDeployment();
    easAddress = await eas.getAddress();
    console.log("EAS deployed to:", easAddress);
  }

  // 2. Deploy KeyRegistry
  console.log("--- Deploying KeyRegistry ---");
  const KeyRegistry = await ethers.getContractFactory("KeyRegistry");
  const keyRegistry = await KeyRegistry.deploy();
  await keyRegistry.waitForDeployment();
  const keyRegistryAddr = await keyRegistry.getAddress();
  console.log("KeyRegistry deployed to:", keyRegistryAddr);

  // 3. Deploy CrossChainBloomFilter
  console.log("--- Deploying CrossChainBloomFilter ---");
  const BloomFilter = await ethers.getContractFactory("CrossChainBloomFilter");
  const bloomFilter = await BloomFilter.deploy();
  await bloomFilter.waitForDeployment();
  const bloomFilterAddr = await bloomFilter.getAddress();
  console.log("CrossChainBloomFilter deployed to:", bloomFilterAddr);

  // 4. Deploy ImageAuthResolver
  let resolverAddr = ethers.ZeroAddress;
  console.log("--- Deploying ImageAuthResolver ---");
  const Resolver = await ethers.getContractFactory("ImageAuthResolver");
  const resolver = await Resolver.deploy(easAddress, keyRegistryAddr, bloomFilterAddr);
  await resolver.waitForDeployment();
  resolverAddr = await resolver.getAddress();
  console.log("ImageAuthResolver deployed to:", resolverAddr);

  // 5. Authorize adders on the Bloom filter
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

  // 6. Register schema on EAS SchemaRegistry
  let schemaUID = "0x" + "00".repeat(32);
  console.log("--- Registering EAS Schema ---");
  const schemaRegistry = await ethers.getContractAt(
    ["function register(string calldata schema, address resolver, bool revocable) external returns (bytes32)"],
    schemaRegistryAddress
  );
  const schemaTx = await schemaRegistry.register(SCHEMA_STRING, resolverAddr, true);
  const schemaReceipt = await schemaTx.wait();
  const schemaRegisteredEvent = schemaReceipt.logs.find((log) => log.topics.length > 0);
  schemaUID = schemaRegisteredEvent ? schemaRegisteredEvent.topics[1] : "unknown";
  console.log("Schema registered, UID:", schemaUID);

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
    eas: easAddress,
    schemaRegistry: schemaRegistryAddress,
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
