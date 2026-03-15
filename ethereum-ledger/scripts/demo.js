/**
 * End-to-end demo: BLS-BN158 image authentication via EAS on Base Sepolia.
 *
 * Flow:
 *   1. Register a BLS-BN158 signing key on KeyRegistry
 *   2. Simulate signing a pHash (BLS-BN158 signature)
 *   3. Check duplicate via Bloom filter
 *   4. Create EAS attestation (resolver enforces key validity + dedup)
 *   5. Look up attestation by signature prefix
 *   6. Verify: PK and pHash loaded from ledger (not embedded in payload)
 *
 * The stego payload in the image would contain ONLY: [salt || signature]
 * (no pHash, no PK). The verifier retrieves both from the ledger.
 *
 * Usage:
 *   npx hardhat run scripts/demo.js --network baseSepolia
 *   npx hardhat run scripts/demo.js --network hardhat
 */

const { ethers } = require("hardhat");

// Load contract addresses from deployment.json (written by deploy.js) or env
const fs = require("fs");
let deploymentAddrs = {};
try {
  deploymentAddrs = JSON.parse(fs.readFileSync("deployment.json", "utf8"));
} catch (e) {
  // No deployment.json -- will use env vars or deploy locally
}

const KEY_REGISTRY = process.env.KEY_REGISTRY || deploymentAddrs.keyRegistry || "";
const BLOOM_FILTER = process.env.BLOOM_FILTER || deploymentAddrs.bloomFilter || "";
const RESOLVER = process.env.RESOLVER || deploymentAddrs.resolver || "";
const SCHEMA_UID = process.env.SCHEMA_UID || deploymentAddrs.schemaUID || "";
const EAS_ADDRESS = "0x4200000000000000000000000000000000000021";

// Gas cost estimation
// You pay gas on ONE network only (either L2 or L1, not both).
// On L2 rollups, the gas price includes the L1 data posting fee amortized into
// the L2 execution cost. We show the cost for the target deployment network.
const ETH_PRICE_USD = parseFloat(process.env.ETH_PRICE_USD || "3000");

// Approximate effective gas prices (execution + L1 data fee combined)
const GAS_PRICES = {
  baseSepolia:  { gwei: 0.01,  label: "Base Sepolia" },
  base:         { gwei: 0.01,  label: "Base" },
  optimism:     { gwei: 0.01,  label: "Optimism" },
  arbitrumNova: { gwei: 0.002, label: "Arbitrum Nova" },
  l1:           { gwei: 30,    label: "Ethereum L1" },
  localhost:    { gwei: 0.01,  label: "localhost (Base-equivalent)" },
  hardhat:      { gwei: 0.01,  label: "hardhat (Base-equivalent)" },
};

let activeNetwork = "base"; // set in main()

function formatGas(gasUsed) {
  const price = GAS_PRICES[activeNetwork] || GAS_PRICES.base;
  const costETH = (Number(gasUsed) * price.gwei) / 1e9;
  const costUSD = costETH * ETH_PRICE_USD;
  const usdStr = costUSD < 0.001
    ? `$${costUSD.toExponential(1)}`
    : `$${costUSD.toFixed(4)}`;
  return `${gasUsed} gas (~${usdStr} on ${price.label} at ETH=$${ETH_PRICE_USD})`;
}

// Simulated BLS-BN158 key pair and signature
// In production these come from the C library (unified-api SCHEME=bls-bn158)
function simulateBLS() {
  // BLS-BN158: PK = 41 bytes (G2 compressed), sig = 21 bytes (G1 compressed), SK = 20 bytes
  const sk = ethers.randomBytes(20);
  const pk = ethers.randomBytes(41); // Simulated compressed G2 point
  // In reality: pk = g2 * sk, but we can't do pairing crypto in JS here

  return { sk, pk };
}

function simulateBLSSign(sk, message) {
  // Simulated: in production, this is cp_bls_sig(sig, message, len, sk)
  // We produce a deterministic "signature" by hashing sk + message
  const sig = ethers.dataSlice(
    ethers.keccak256(ethers.concat([sk, message])),
    0,
    21 // BLS-BN158 G1 compressed = 21 bytes
  );
  return sig;
}

async function main() {
  const [signer] = await ethers.getSigners();
  console.log("=================================================================");
  console.log("  End-to-End Demo: BLS-BN158 Image Auth via EAS");
  console.log("=================================================================\n");
  console.log("Signer:", signer.address);

  // --- Check if we're on a local network or testnet ---
  const network = await ethers.provider.getNetwork();
  const isLocal = network.chainId === 31337n;

  // Set the active network for gas cost estimation.
  // Use SIMULATE_NETWORK env var to override (e.g., simulate Arbitrum Nova costs locally).
  const chainIdToNetwork = {
    31337n: "localhost",
    84532n: "baseSepolia",
    8453n: "base",
    10n: "optimism",
    42170n: "arbitrumNova",
    1n: "l1",
  };
  const simulatedNetwork = process.env.SIMULATE_NETWORK;
  if (simulatedNetwork && GAS_PRICES[simulatedNetwork]) {
    activeNetwork = simulatedNetwork;
  } else {
    activeNetwork = chainIdToNetwork[network.chainId] || "base";
  }

  const priceInfo = GAS_PRICES[activeNetwork] || GAS_PRICES.base;
  if (simulatedNetwork) {
    console.log(`Network: local (simulating ${priceInfo.label})`);
  } else {
    console.log("Network:", isLocal ? "hardhat (local)" : `chainId ${network.chainId}`);
  }
  console.log("");

  let keyRegistry, bloomFilter, resolver, schemaUID, eas;

  // Try to use existing deployed contracts (from deployment.json or env)
  // Verify the contracts actually exist at the addresses (handles ephemeral hardhat restarts)
  let hasDeployedContracts = KEY_REGISTRY && BLOOM_FILTER;
  let easAddress = deploymentAddrs.eas || EAS_ADDRESS;

  if (hasDeployedContracts) {
    const code = await ethers.provider.getCode(KEY_REGISTRY);
    if (code === "0x") {
      console.log("  deployment.json found but contracts not deployed at addresses (stale?).");
      console.log("  Falling back to fresh deployment.\n");
      hasDeployedContracts = false;
    }
  }

  if (hasDeployedContracts) {
    // Reuse contracts from a previous deploy (works for localhost, testnet, etc.)
    console.log("--- Using deployed contracts ---");
    keyRegistry = await ethers.getContractAt("KeyRegistry", KEY_REGISTRY);
    bloomFilter = await ethers.getContractAt("CrossChainBloomFilter", BLOOM_FILTER);

    if (RESOLVER && RESOLVER !== ethers.ZeroAddress) {
      resolver = await ethers.getContractAt("ImageAuthResolver", RESOLVER);
      console.log("  Resolver:", RESOLVER);
    }
    schemaUID = SCHEMA_UID;

    console.log("  KeyRegistry:", KEY_REGISTRY);
    console.log("  BloomFilter:", BLOOM_FILTER);
    if (schemaUID && schemaUID !== ethers.ZeroHash) {
      console.log("  Schema UID:", schemaUID);
    }
    if (deploymentAddrs.eas && deploymentAddrs.eas !== ethers.ZeroAddress) {
      easAddress = deploymentAddrs.eas;
      console.log("  EAS:", easAddress);
    }
    console.log("");
  } else if (isLocal) {
    // No deployment.json and on local network: deploy fresh for ephemeral testing
    console.log("--- No deployment.json found, deploying fresh (ephemeral) ---");
    console.log("  Tip: run 'npx hardhat run scripts/deploy.js --network localhost'");
    console.log("  first for persistent local testing.\n");

    console.log("  Fresh ephemeral localhost deployment is not supported anymore.");
    console.log("  Run deploy.js first so local EAS + resolver are available.");
    process.exit(1);

    const KR = await ethers.getContractFactory("KeyRegistry");
    keyRegistry = await KR.deploy();
    await keyRegistry.waitForDeployment();
    console.log("  KeyRegistry:", await keyRegistry.getAddress());

    const BF = await ethers.getContractFactory("CrossChainBloomFilter");
    bloomFilter = await BF.deploy();
    await bloomFilter.waitForDeployment();
    console.log("  BloomFilter:", await bloomFilter.getAddress());

    // Authorize the signer to add to bloom filter
    const authTx = await bloomFilter.authorizeAdder(signer.address);
    await authTx.wait();
    console.log("  (Deployed fresh, EAS skipped)\n");
  } else {
    console.error("Missing contract addresses. Run deploy.js first.");
    console.error("  For localhost: npx hardhat run scripts/deploy.js --network localhost");
    console.error("  For testnet:   npx hardhat run scripts/deploy.js --network baseSepolia");
    console.error("Alternatively set env vars: KEY_REGISTRY, BLOOM_FILTER, RESOLVER, SCHEMA_UID");
    process.exit(1);
  }

  // ============================================================
  // Step 1: Generate BLS-BN158 key pair and register on KeyRegistry
  // ============================================================
  console.log("--- Step 1: Generate BLS-BN158 Key Pair & Register ---");
  const { sk, pk } = simulateBLS();
  const SCHEME_BLS_BN158 = 2;

  console.log("  PK (41 bytes):", ethers.hexlify(pk));
  console.log("  SK (20 bytes):", ethers.hexlify(sk));

  const regTx = await keyRegistry.registerKey(pk, SCHEME_BLS_BN158);
  const regReceipt = await regTx.wait();
  console.log("  Key registered!", formatGas(regReceipt.gasUsed));

  // Verify key is valid
  const isValid = await keyRegistry.isKeyCurrentlyValid(pk);
  console.log("  Key currently valid:", isValid);
  console.log("");

  // ============================================================
  // Step 2: "Sign" a perceptual hash
  // ============================================================
  console.log("--- Step 2: Sign a Perceptual Hash ---");

  // Simulate a 144-bit (18-byte) pHash
  const pHash = ethers.hexlify(ethers.randomBytes(18));
  // Pad to bytes24 for the schema
  const pHash24 = pHash + "0".repeat(12); // pad to 24 bytes

  // Generate salt (2 bytes)
  const salt = ethers.hexlify(ethers.randomBytes(2));

  // Sign: pHash || salt
  const signInput = ethers.concat([pHash, salt]);
  const signature = simulateBLSSign(sk, signInput);

  // Signature prefix (first 16 bytes for indexing)
  const sigPrefix = ethers.dataSlice(signature, 0, 16);

  console.log("  pHash (144 bits):", pHash);
  console.log("  Salt (16 bits):", salt);
  console.log("  Signature (168 bits):", ethers.hexlify(signature));
  console.log("  Sig prefix (128 bits):", sigPrefix);
  console.log("");

  // ============================================================
  // Step 3: Stego payload (what goes in the image)
  // ============================================================
  console.log("--- Step 3: Stego Payload (embedded in image) ---");
  const stegoPayload = ethers.concat([salt, signature]);
  console.log("  Payload: [salt || signature] (no pHash, no PK)");
  console.log("  Payload:", ethers.hexlify(stegoPayload));
  console.log("  Payload size:", ethers.getBytes(stegoPayload).length, "bytes =",
              ethers.getBytes(stegoPayload).length * 8, "bits");
  console.log("");

  // ============================================================
  // Step 4: Check Bloom filter for cross-chain duplicate
  // ============================================================
  console.log("--- Step 4: Cross-Chain Duplicate Check (Bloom Filter) ---");
  const pHashSaltKey = ethers.keccak256(ethers.concat([pHash24, salt]));
  const mightExist = await bloomFilter.mightContain(pHashSaltKey);
  console.log("  pHashSaltKey:", pHashSaltKey);
  console.log("  Bloom filter says might exist:", mightExist);
  if (mightExist) {
    console.log("  → Would retry with new salt in production");
  } else {
    console.log("  → Unique! Proceeding with registration");
  }
  console.log("");

  // ============================================================
  // Step 5: Register on ledger (EAS attestation or direct)
  // ============================================================
  console.log("--- Step 5: Register on Ledger ---");

  // Compute a simulated file hash
  const fileHash = ethers.keccak256(ethers.toUtf8Bytes("simulated-image-file-content"));
  const metadataCID = ethers.keccak256(ethers.toUtf8Bytes("ipfs://Qm.../metadata.json"));

  {
    // Create EAS attestation (works on local deployed EAS and on testnet)
    const easAbi = [
      "function attest((bytes32 schema, (address recipient, uint64 expirationTime, bool revocable, bytes32 refUID, bytes data, uint256 value) data)) external payable returns (bytes32)"
    ];
    eas = new ethers.Contract(easAddress, easAbi, signer);

    const attestationData = ethers.AbiCoder.defaultAbiCoder().encode(
      ["bytes16", "bytes", "uint8", "bytes", "bytes24", "bytes2", "bytes32", "bytes32"],
      [sigPrefix, signature, SCHEME_BLS_BN158, pk, pHash24, salt, fileHash, metadataCID]
    );

    console.log("  Creating EAS attestation...");
    const attestTx = await eas.attest({
      schema: SCHEMA_UID,
      data: {
        recipient: ethers.ZeroAddress,
        expirationTime: 0n,
        revocable: true,
        refUID: ethers.ZeroHash,
        data: attestationData,
        value: 0n,
      },
    });
    const attestReceipt = await attestTx.wait();
    console.log("  Attestation created!", formatGas(attestReceipt.gasUsed));

    // Get the attestation UID from the event
    const attestedEvent = attestReceipt.logs.find(l => l.topics.length > 0);
    const attestUID = attestedEvent ? attestedEvent.topics[1] : "unknown";
    console.log("  Attestation UID:", attestUID);

    // Check resolver indexes
    const storedUID = await resolver.sigPrefixIndex(sigPrefix);
    console.log("  Resolver sigPrefixIndex:", storedUID);
  }
  console.log("");

  // ============================================================
  // Step 6: Simulate verification (extract from image → lookup on ledger)
  // ============================================================
  console.log("--- Step 6: Verification (Simulate) ---");
  console.log("  Verifier extracts from image: [salt || signature]");
  console.log("  Verifier computes pHash from the received image");
  console.log("");

  // Verifier has: stegoPayload (from image), and computes their own pHash
  const extractedSalt = ethers.dataSlice(stegoPayload, 0, 2);
  const extractedSig = ethers.dataSlice(stegoPayload, 2);
  const extractedSigPrefix = ethers.dataSlice(extractedSig, 0, 16);

  console.log("  Extracted salt:", extractedSalt);
  console.log("  Extracted signature:", ethers.hexlify(extractedSig));
  console.log("  Extracted sig prefix:", extractedSigPrefix);

  if (resolver) {
    // Look up on ledger
    const uid = await resolver.sigPrefixIndex(extractedSigPrefix);
    console.log("  Ledger lookup by sig prefix → UID:", uid);

    if (uid !== ethers.ZeroHash) {
      // Read the attestation from EAS
      const easReadAbi = [
        "function getAttestation(bytes32 uid) external view returns ((bytes32 uid, bytes32 schema, uint64 time, uint64 expirationTime, uint64 revocationTime, bytes32 refUID, address attester, address recipient, bool revocable, bytes data))"
      ];
      const easRead = new ethers.Contract(easAddress, easReadAbi, signer);
      const att = await easRead.getAttestation(uid);

      // Decode the attestation data
      const decoded = ethers.AbiCoder.defaultAbiCoder().decode(
        ["bytes16", "bytes", "uint8", "bytes", "bytes24", "bytes2", "bytes32", "bytes32"],
        att.data
      );

      const ledgerPK = decoded[3];
      const ledgerPHash = ethers.dataSlice(decoded[4], 0, 18); // First 18 bytes (144 bits)
      const ledgerSalt = decoded[5];
      const ledgerFileHash = decoded[6];

      console.log("");
      console.log("  === FROM LEDGER ===");
      console.log("  Public key:", ethers.hexlify(ledgerPK));
      console.log("  pHash:", ledgerPHash);
      console.log("  Salt:", ethers.hexlify(ledgerSalt));
      console.log("  File hash:", ledgerFileHash);
      console.log("  Attester:", att.attester);
      console.log("  Timestamp:", new Date(Number(att.time) * 1000).toISOString());
      console.log("");
      console.log("  Verifier now has PK and pHash from ledger.");
      console.log("  Would verify: BLS_verify(extractedSig, pHash || salt, ledgerPK)");
      console.log("  Would compare: pHash(received_image) ≈ ledgerPHash");
      console.log("  Would check: SHA-256(received_image) == ledgerFileHash");

      // Check key was valid at attestation time
      const keyValid = await keyRegistry.isKeyValidAt(ledgerPK, att.time);
      console.log("  Key valid at attestation time:", keyValid);
    }
  }

  // ============================================================
  // Step 7: Test duplicate rejection
  // ============================================================
  console.log("");
  console.log("--- Step 7: Duplicate Rejection Test ---");
  const dupExists = await bloomFilter.mightContain(pHashSaltKey);
  console.log("  Same (pHash, salt) in Bloom filter:", dupExists);
  if (dupExists) {
    console.log("  → Registration would be rejected (duplicate detected)");
    console.log("  → Client retries with new salt...");
    const newSalt = ethers.hexlify(ethers.randomBytes(2));
    const newPHashSaltKey = ethers.keccak256(ethers.concat([pHash24, newSalt]));
    const newMightExist = await bloomFilter.mightContain(newPHashSaltKey);
    console.log("  New salt:", newSalt, "→ Bloom says:", newMightExist ? "might exist (retry again)" : "unique!");
  }

  // ============================================================
  // Summary
  // ============================================================
  console.log("");
  console.log("=================================================================");
  console.log("  DEMO COMPLETE");
  console.log("=================================================================");
  console.log("  Scheme: BLS-BN158 (78-bit classical security, NOT quantum-safe)");
  console.log("  Stego payload: salt || sig = 23 bytes = 184 bits");
  console.log("  pHash: NOT in payload (loaded from ledger)");
  console.log("  PK: NOT in payload (loaded from ledger)");
  console.log("  Key lifecycle: tracked on KeyRegistry with validity windows");
  console.log("  Duplicate detection: Bloom filter (cross-chain) + per-chain index");
  console.log("");
  const price = GAS_PRICES[activeNetwork] || GAS_PRICES.base;
  console.log(`  Cost estimate: ${price.label}, ${price.gwei} gwei, ETH=$${ETH_PRICE_USD}`);
  console.log("  Override ETH price: ETH_PRICE_USD=...");
  console.log("=================================================================");
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
