// SPDX-License-Identifier: MIT
pragma solidity ^0.8.19;

import { SchemaResolver } from "@ethereum-attestation-service/eas-contracts/contracts/resolver/SchemaResolver.sol";
import { IEAS, Attestation } from "@ethereum-attestation-service/eas-contracts/contracts/IEAS.sol";

interface IKeyRegistry {
    function isKeyCurrentlyValid(bytes calldata publicKey) external view returns (bool);
}

interface ICrossChainBloomFilter {
    function add(bytes32 pHashSaltKey) external;
    function mightContain(bytes32 pHashSaltKey) external view returns (bool);
}

/**
 * @title ImageAuthResolver
 * @notice EAS resolver that enforces:
 *         1. (pHash, salt) uniqueness (per-chain)
 *         2. Signing key validity (via KeyRegistry)
 *         3. Cross-chain Bloom filter update
 *         4. Signature-prefix indexing for lookup
 */
contract ImageAuthResolver is SchemaResolver {
    // Per-chain dedup index
    mapping(bytes32 => bytes32) public pHashSaltIndex;

    // Signature prefix lookup
    mapping(bytes16 => bytes32) public sigPrefixIndex;

    IKeyRegistry public keyRegistry;
    ICrossChainBloomFilter public bloomFilter;

    event ImageRegistered(
        address indexed attester,
        bytes16 indexed sigPrefix,
        bytes32 pHashSaltKey,
        bytes32 attestationUID
    );

    constructor(
        IEAS eas,
        IKeyRegistry _keyRegistry,
        ICrossChainBloomFilter _bloomFilter
    ) SchemaResolver(eas) {
        keyRegistry = _keyRegistry;
        bloomFilter = _bloomFilter;
    }

    function onAttest(
        Attestation calldata attestation,
        uint256 /* value */
    ) internal override returns (bool) {
        // Decode attestation data
        // Schema: sigPrefix, signature, scheme, publicKey, pHash, pHashVersion, salt, fileHash, metadataCID
        (
            bytes16 sigPrefix,
            ,  // signature (not needed in resolver)
            ,  // scheme
            bytes memory publicKey,
            bytes24 pHash,
            uint16 pHashVersion,
            bytes2 salt,
            ,  // fileHash
               // metadataCID
        ) = abi.decode(
            attestation.data,
            (bytes16, bytes, uint8, bytes, bytes24, uint16, bytes2, bytes32, bytes32)
        );

        // 1. Enforce (pHash, salt) uniqueness on this chain
        bytes32 pHashSaltKey = keccak256(abi.encodePacked(pHashVersion, pHash, salt));
        if (pHashSaltIndex[pHashSaltKey] != bytes32(0)) {
            return false; // Duplicate
        }

        // 2. Verify signing key is currently valid
        if (address(keyRegistry) != address(0)) {
            if (!keyRegistry.isKeyCurrentlyValid(publicKey)) {
                return false; // Key not active
            }
        }

        // 3. Record indexes
        pHashSaltIndex[pHashSaltKey] = attestation.uid;
        sigPrefixIndex[sigPrefix] = attestation.uid;

        // 4. Update Bloom filter for cross-chain dedup
        if (address(bloomFilter) != address(0)) {
            bloomFilter.add(pHashSaltKey);
        }

        emit ImageRegistered(attestation.attester, sigPrefix, pHashSaltKey, attestation.uid);

        return true;
    }

    function onRevoke(
        Attestation calldata /* attestation */,
        uint256 /* value */
    ) internal pure override returns (bool) {
        return true;
    }

    /// @notice Check if a (pHash, salt) is already registered on this chain.
    function isDuplicate(uint16 pHashVersion, bytes24 pHash, bytes2 salt) external view returns (bool) {
        bytes32 key = keccak256(abi.encodePacked(pHashVersion, pHash, salt));
        return pHashSaltIndex[key] != bytes32(0);
    }

    /// @notice Check cross-chain Bloom filter for potential duplicate.
    function mightBeDuplicateCrossChain(uint16 pHashVersion, bytes24 pHash, bytes2 salt) external view returns (bool) {
        if (address(bloomFilter) == address(0)) return false;
        bytes32 key = keccak256(abi.encodePacked(pHashVersion, pHash, salt));
        return bloomFilter.mightContain(key);
    }
}
