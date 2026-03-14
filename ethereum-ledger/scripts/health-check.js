// Simple health check: try to get the block number
const { ethers } = require("hardhat");
async function main() {
  const blockNumber = await ethers.provider.getBlockNumber();
  if (blockNumber >= 0) process.exit(0);
  process.exit(1);
}
main().catch(() => process.exit(1));
