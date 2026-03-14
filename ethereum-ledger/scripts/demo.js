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
  console.log("Network:", isLocal ? "hardhat (local)" : `chainId ${network.chainId}`);
  console.log("");

  let keyRegistry, bloomFilter, resolver, schemaUID, eas;

  if (isLocal) {
    // Deploy everything locally for testing
    console.log("--- Local deployment ---");
    
    // Deploy a minimal EAS mock for local testing
    // We'll deploy our contracts with address(0) for EAS (skip resolver EAS integration)
    // and test the key registry + bloom filter + attestation encoding directly
    
    const KR = await ethers.getContractFactory("KeyRegistry");
    keyRegistry = await KR.deploy();
    await keyRegistry.waitForDeployment();
    console.log("KeyRegistry:", await keyRegistry.getAddress());

    const BF = await ethers.getContractFactory("CrossChainBloomFilter");
    bloomFilter = await BF.deploy();
    await bloomFilter.waitForDeployment();
    console.log("BloomFilter:", await bloomFilter.getAddress());

    // For local testing, we skip the EAS attestation (no EAS on hardhat)
    // and test the contracts directly
    console.log("(Skipping EAS attestation on local network - testing contracts directly)\n");
  } else {
    // Use deployed contracts on testnet
    if (!KEY_REGISTRY || !BLOOM_FILTER || !RESOLVER || !SCHEMA_UID) {
      console.error("Missing contract addresses. Run deploy.js first and set env vars.");
      console.error("Required: KEY_REGISTRY, BLOOM_FILTER, RESOLVER, SCHEMA_UID");
      process.exit(1);
    }
    keyRegistry = await ethers.getContractAt("KeyRegistry", KEY_REGISTRY);
    bloomFilter = await ethers.getContractAt("CrossChainBloomFilter", BLOOM_FILTER);
    resolver = await ethers.getContractAt("ImageAuthResolver", RESOLVER);
    console.log("Using deployed contracts");
    console.log("  KeyRegistry:", KEY_REGISTRY);
    console.log("  BloomFilter:", BLOOM_FILTER);
    console.log("  Resolver:", RESOLVER);
    console.log("  Schema UID:", SCHEMA_UID);
    console.log("");
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
  console.log("  Key registered! Gas used:", regReceipt.gasUsed.toString());

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

  if (isLocal) {
    // Local: directly add to bloom filter and show what the attestation would contain
    const authTx = await bloomFilter.authorizeAdder(signer.address);
    await authTx.wait();
    const addTx = await bloomFilter.add(pHashSaltKey);
    const addReceipt = await addTx.wait();
    console.log("  Added to Bloom filter. Gas:", addReceipt.gasUsed.toString());

    // Encode the attestation data as it would be on-chain
    const attestationData = ethers.AbiCoder.defaultAbiCoder().encode(
      ["bytes16", "bytes", "uint8", "bytes", "bytes24", "bytes2", "bytes32", "bytes32"],
      [sigPrefix, signature, SCHEME_BLS_BN158, pk, pHash24, salt, fileHash, metadataCID]
    );
    console.log("  Attestation data size:", ethers.getBytes(attestationData).length, "bytes");
    console.log("  (On testnet, this would be an EAS attestation via the resolver)");

    // Verify Bloom filter now contains the entry
    const nowExists = await bloomFilter.mightContain(pHashSaltKey);
    console.log("  Bloom filter now contains entry:", nowExists);

    // Verify duplicate detection works
    const dupCheck = await bloomFilter.mightContain(pHashSaltKey);
    console.log("  Duplicate re-check:", dupCheck, "(should be true)");
  } else {
    // Testnet: create EAS attestation
    const easAbi = [
      "function attest((bytes32 schema, (address recipient, uint64 expirationTime, bool revocable, bytes32 refUID, bytes data, uint256 value) data)) external payable returns (bytes32)"
    ];
    eas = new ethers.Contract(EAS_ADDRESS, easAbi, signer);

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
    console.log("  Attestation created! Gas used:", attestReceipt.gasUsed.toString());

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

  if (!isLocal && resolver) {
    // Look up on ledger
    const uid = await resolver.sigPrefixIndex(extractedSigPrefix);
    console.log("  Ledger lookup by sig prefix → UID:", uid);

    if (uid !== ethers.ZeroHash) {
      // Read the attestation from EAS
      const easReadAbi = [
        "function getAttestation(bytes32 uid) external view returns ((bytes32 uid, bytes32 schema, uint64 time, uint64 expirationTime, uint64 revocationTime, bytes32 refUID, address attester, address recipient, bool revocable, bytes data))"
      ];
      const easRead = new ethers.Contract(EAS_ADDRESS, easReadAbi, signer);
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
  } else {
    console.log("  (On local network: simulating ledger lookup)");
    console.log("  Verifier would query EAS by sig prefix to get PK and pHash");
    console.log("  Then verify: BLS_verify(sig, pHash || salt, PK)");
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
  console.log("=================================================================");
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
