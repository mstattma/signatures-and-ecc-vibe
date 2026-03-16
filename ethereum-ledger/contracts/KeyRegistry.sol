// SPDX-License-Identifier: MIT
pragma solidity ^0.8.19;

/**
 * @title KeyRegistry
 * @notice Tracks the lifecycle of signing keys:
 *         - BLS/UOV stego signing keys (variable-length public key bytes)
 *         - C2PA ES256 signing keys (P-256 public key coordinates)
 *
 *         Each user (Ethereum address) can have multiple signing keys per type,
 *         but only one active at a time per type. Registering a new key
 *         automatically revokes the previous one of the same type.
 */
contract KeyRegistry {
    // --- BLS/UOV signing keys ---

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

    // --- C2PA ES256 (P-256) signing keys ---

    struct C2PAKey {
        uint256 pubKeyX;      // P-256 public key X coordinate
        uint256 pubKeyY;      // P-256 public key Y coordinate
        bytes32 certHash;     // sha256(DER certificate bytes)
        bytes32 certCID;      // IPFS CID of full X.509 certificate
        uint64 activatedAt;
        uint64 revokedAt;     // 0 = currently active
    }

    mapping(address => C2PAKey[]) public c2paKeys;
    mapping(bytes32 => address) public certHashOwner;
    mapping(bytes32 => uint256) public certHashIndex;
    mapping(address => uint256) public activeC2PAKeyIndex;

    event C2PAKeyActivated(address indexed user, uint256 indexed keyIndex, bytes32 certHash);
    event C2PAKeyRevoked(address indexed user, uint256 indexed keyIndex, uint64 revokedAt);

    function registerC2PAKey(
        uint256 pubKeyX,
        uint256 pubKeyY,
        bytes32 certHash,
        bytes32 certCID
    ) external {
        require(certHashOwner[certHash] == address(0), "C2PA cert already registered");
        require(pubKeyX != 0 && pubKeyY != 0, "Invalid public key");

        if (c2paKeys[msg.sender].length > 0) {
            uint256 prevIdx = activeC2PAKeyIndex[msg.sender];
            if (c2paKeys[msg.sender][prevIdx].revokedAt == 0) {
                c2paKeys[msg.sender][prevIdx].revokedAt = uint64(block.timestamp);
                emit C2PAKeyRevoked(msg.sender, prevIdx, uint64(block.timestamp));
            }
        }

        uint256 newIdx = c2paKeys[msg.sender].length;
        c2paKeys[msg.sender].push(C2PAKey({
            pubKeyX: pubKeyX,
            pubKeyY: pubKeyY,
            certHash: certHash,
            certCID: certCID,
            activatedAt: uint64(block.timestamp),
            revokedAt: 0
        }));

        certHashOwner[certHash] = msg.sender;
        certHashIndex[certHash] = newIdx;
        activeC2PAKeyIndex[msg.sender] = newIdx;

        emit C2PAKeyActivated(msg.sender, newIdx, certHash);
    }

    function revokeC2PAKey(uint256 keyIndex) external {
        require(keyIndex < c2paKeys[msg.sender].length, "Invalid index");
        require(c2paKeys[msg.sender][keyIndex].revokedAt == 0, "Already revoked");
        c2paKeys[msg.sender][keyIndex].revokedAt = uint64(block.timestamp);
        emit C2PAKeyRevoked(msg.sender, keyIndex, uint64(block.timestamp));
    }

    /// @notice Look up a C2PA key by certificate hash. Returns (x, y, active).
    function getC2PAKeyByCertHash(bytes32 certHash) external view returns (
        uint256 pubKeyX, uint256 pubKeyY, bool active
    ) {
        address owner = certHashOwner[certHash];
        if (owner == address(0)) return (0, 0, false);
        C2PAKey storage k = c2paKeys[owner][certHashIndex[certHash]];
        return (k.pubKeyX, k.pubKeyY, k.revokedAt == 0);
    }

    function c2paKeyCount(address user) external view returns (uint256) {
        return c2paKeys[user].length;
    }

    function getC2PAKey(address user, uint256 index) external view returns (
        uint256 pubKeyX, uint256 pubKeyY, bytes32 certHash, bytes32 certCID,
        uint64 activatedAt, uint64 revokedAt
    ) {
        C2PAKey storage k = c2paKeys[user][index];
        return (k.pubKeyX, k.pubKeyY, k.certHash, k.certCID, k.activatedAt, k.revokedAt);
    }
}
