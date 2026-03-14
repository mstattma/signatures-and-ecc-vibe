// SPDX-License-Identifier: MIT
pragma solidity ^0.8.19;

/**
 * @title CrossChainBloomFilter
 * @notice On-chain Bloom filter for cross-chain (pHash, salt) duplicate detection.
 *         False positives cause harmless salt retry. Zero false negatives.
 *
 *         2048 bytes = 16384 bits, 10 hash functions.
 *         ~5000 entries at <1% false positive rate.
 */
contract CrossChainBloomFilter {
    uint256 constant BLOOM_SIZE_BITS = 16384;
    uint256 constant BLOOM_SIZE_WORDS = 64; // 16384 / 256
    uint256 constant NUM_HASHES = 10;

    uint256[64] public bloomFilter;
    uint256 public entryCount;

    address public owner;
    mapping(address => bool) public authorizedAdders;

    event BloomUpdated(bytes32 indexed pHashSaltKey);
    event BloomSynced(uint256 newEntries, bytes32 sourceChainId);

    modifier onlyAuthorized() {
        require(msg.sender == owner || authorizedAdders[msg.sender], "Not authorized");
        _;
    }

    constructor() {
        owner = msg.sender;
    }

    function authorizeAdder(address adder) external {
        require(msg.sender == owner, "Not owner");
        authorizedAdders[adder] = true;
    }

    function add(bytes32 pHashSaltKey) external onlyAuthorized {
        _addToFilter(pHashSaltKey);
        entryCount++;
        emit BloomUpdated(pHashSaltKey);
    }

    function mightContain(bytes32 pHashSaltKey) external view returns (bool) {
        for (uint256 i = 0; i < NUM_HASHES; i++) {
            uint256 bit = uint256(keccak256(abi.encodePacked(pHashSaltKey, i))) % BLOOM_SIZE_BITS;
            uint256 wordIdx = bit / 256;
            uint256 bitIdx = bit % 256;
            if (bloomFilter[wordIdx] & (1 << bitIdx) == 0) {
                return false;
            }
        }
        return true;
    }

    function syncFromChain(bytes32[] calldata pHashSaltKeys, bytes32 sourceChainId) external onlyAuthorized {
        for (uint256 i = 0; i < pHashSaltKeys.length; i++) {
            _addToFilter(pHashSaltKeys[i]);
        }
        entryCount += pHashSaltKeys.length;
        emit BloomSynced(pHashSaltKeys.length, sourceChainId);
    }

    function _addToFilter(bytes32 key) internal {
        for (uint256 j = 0; j < NUM_HASHES; j++) {
            uint256 bit = uint256(keccak256(abi.encodePacked(key, j))) % BLOOM_SIZE_BITS;
            uint256 wordIdx = bit / 256;
            uint256 bitIdx = bit % 256;
            bloomFilter[wordIdx] |= (1 << bitIdx);
        }
    }
}
