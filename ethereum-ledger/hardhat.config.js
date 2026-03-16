require("@nomicfoundation/hardhat-ethers");
require("dotenv").config();

const PRIVATE_KEY = process.env.PRIVATE_KEY || "0x" + "00".repeat(32);
const BASE_SEPOLIA_RPC = process.env.BASE_SEPOLIA_RPC || "https://sepolia.base.org";

module.exports = {
  solidity: {
    version: "0.8.27",
    settings: {
      optimizer: { enabled: true, runs: 200 },
      evmVersion: "cancun",
      viaIR: true,
    },
  },
  networks: {
    // Default local network (chainId 31337)
    hardhat: {},

    // Persistent local node (start with: npx hardhat node)
    localhost: {
      url: "http://127.0.0.1:8545",
    },

    // Simulate Arbitrum Nova locally (start with: npx hardhat node --port 8546)
    // Uses a separate port so you can run Base-like and Nova-like nodes simultaneously
    localNova: {
      url: "http://127.0.0.1:8546",
    },

    // Testnets
    baseSepolia: {
      url: BASE_SEPOLIA_RPC,
      accounts: [PRIVATE_KEY],
      chainId: 84532,
    },
  },
};
