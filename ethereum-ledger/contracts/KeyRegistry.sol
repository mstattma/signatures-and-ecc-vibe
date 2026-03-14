// SPDX-License-Identifier: MIT
pragma solidity ^0.8.19;

/**
 * @title KeyRegistry
 * @notice Tracks the lifecycle of stego signing keys (UOV/BLS).
 *         Each user (Ethereum address) can have multiple signing keys,
 *         but only one active at a time. Registering a new key automatically
 *         revokes the previous one.
 */
contract KeyRegistry {
    struct SigningKey {
        bytes publicKey;
        uint8 scheme;        // 0=UOV-80, 1=UOV-100, 2=BLS-BN158, 3=BLS12-381
        uint64 activatedAt;
        uint64 revokedAt;    // 0 = currently active
        bytes32 pkHash;
    }

    mapping(address => SigningKey[]) public signingKeys;
    mapping(bytes32 => address) public pkOwner;
    mapping(bytes32 => uint256) public pkIndex;
    mapping(address => uint256) public activeKeyIndex;

    event KeyActivated(address indexed user, uint256 indexed keyIndex, bytes publicKey, uint8 scheme);
    event KeyRevoked(address indexed user, uint256 indexed keyIndex, uint64 revokedAt);

    function registerKey(bytes calldata publicKey, uint8 scheme) external {
        bytes32 pkh = keccak256(publicKey);
        require(pkOwner[pkh] == address(0), "PK already registered");

        // Revoke previous active key
        if (signingKeys[msg.sender].length > 0) {
            uint256 prevIdx = activeKeyIndex[msg.sender];
            if (signingKeys[msg.sender][prevIdx].revokedAt == 0) {
                signingKeys[msg.sender][prevIdx].revokedAt = uint64(block.timestamp);
                emit KeyRevoked(msg.sender, prevIdx, uint64(block.timestamp));
            }
        }

        uint256 newIdx = signingKeys[msg.sender].length;
        signingKeys[msg.sender].push(SigningKey({
            publicKey: publicKey,
            scheme: scheme,
            activatedAt: uint64(block.timestamp),
            revokedAt: 0,
            pkHash: pkh
        }));

        pkOwner[pkh] = msg.sender;
        pkIndex[pkh] = newIdx;
        activeKeyIndex[msg.sender] = newIdx;

        emit KeyActivated(msg.sender, newIdx, publicKey, scheme);
    }

    function revokeKey(uint256 keyIndex) external {
        require(keyIndex < signingKeys[msg.sender].length, "Invalid index");
        require(signingKeys[msg.sender][keyIndex].revokedAt == 0, "Already revoked");
        signingKeys[msg.sender][keyIndex].revokedAt = uint64(block.timestamp);
        emit KeyRevoked(msg.sender, keyIndex, uint64(block.timestamp));
    }

    function isKeyValidAt(bytes calldata publicKey, uint64 timestamp) external view returns (bool) {
        bytes32 pkh = keccak256(publicKey);
        address owner = pkOwner[pkh];
        if (owner == address(0)) return false;
        SigningKey storage k = signingKeys[owner][pkIndex[pkh]];
        return timestamp >= k.activatedAt && (k.revokedAt == 0 || timestamp < k.revokedAt);
    }

    function isKeyCurrentlyValid(bytes calldata publicKey) external view returns (bool) {
        bytes32 pkh = keccak256(publicKey);
        address owner = pkOwner[pkh];
        if (owner == address(0)) return false;
        SigningKey storage k = signingKeys[owner][pkIndex[pkh]];
        return k.revokedAt == 0;
    }

    function keyCount(address user) external view returns (uint256) {
        return signingKeys[user].length;
    }

    function getKey(address user, uint256 index) external view returns (
        bytes memory publicKey, uint8 scheme, uint64 activatedAt, uint64 revokedAt
    ) {
        SigningKey storage k = signingKeys[user][index];
        return (k.publicKey, k.scheme, k.activatedAt, k.revokedAt);
    }
}
