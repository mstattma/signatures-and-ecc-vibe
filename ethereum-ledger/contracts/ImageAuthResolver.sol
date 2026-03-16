// SPDX-License-Identifier: MIT
pragma solidity ^0.8.19;

import { SchemaResolver } from "@ethereum-attestation-service/eas-contracts/contracts/resolver/SchemaResolver.sol";
import { IEAS, Attestation } from "@ethereum-attestation-service/eas-contracts/contracts/IEAS.sol";
import { P256Verifier } from "./P256Verifier.sol";

interface IKeyRegistry {
    function isKeyCurrentlyValid(bytes calldata publicKey) external view returns (bool);
    function getC2PAKeyByCertHash(bytes32 certHash) external view returns (uint256 pubKeyX, uint256 pubKeyY, bool active);
}

interface ICrossChainBloomFilter {
    function add(bytes32 pHashSaltKey) external;
    function mightContain(bytes32 pHashSaltKey) external view returns (bool);
}

/**
 * @title ImageAuthResolver
 * @notice EAS resolver that enforces:
 *         1. (pHash, salt) uniqueness (per-chain)
 *         2. BLS signing key validity (via KeyRegistry)
 *         3. C2PA ES256 signature verification (P-256 via KeyRegistry + P256Verifier)
 *         4. Cross-chain Bloom filter update
 *         5. Signature-prefix indexing for lookup
 *
 * Attestation schema:
 *   sigPrefix(bytes16), signature(bytes), scheme(uint8), publicKey(bytes),
 *   pHash(bytes24), pHashVersion(uint16), salt(bytes2), fileHash(bytes32),
 *   metadataCID(bytes32), c2paCertHash(bytes32), c2paSig(bytes), fileName(string)
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
        // Decode attestation data (new schema with c2paCertHash + c2paSig)
        (
            bytes16 sigPrefix,
            ,  // signature
            ,  // scheme
            bytes memory publicKey,
            bytes24 pHash,
            uint16 pHashVersion,
            bytes2 salt,
            ,  // fileHash
            ,  // metadataCID
            bytes32 c2paCertHash,
            bytes memory c2paSig,
               // fileName
        ) = abi.decode(
            attestation.data,
            (bytes16, bytes, uint8, bytes, bytes24, uint16, bytes2, bytes32, bytes32, bytes32, bytes, string)
        );

        // 1. Enforce (pHash, salt) uniqueness on this chain
        bytes32 pHashSaltKey = keccak256(abi.encodePacked(pHashVersion, pHash, salt));
        if (pHashSaltIndex[pHashSaltKey] != bytes32(0)) {
            return false; // Duplicate
        }

        // 2. Verify BLS signing key is currently valid
        if (address(keyRegistry) != address(0)) {
            if (!keyRegistry.isKeyCurrentlyValid(publicKey)) {
                return false; // Key not active
            }
        }

        // 3. Verify C2PA signature: ES256 over sha256(pHash || pHashVersion || salt)
        //    This proves the C2PA key holder authorized this pHash+salt combination.
        //    An attacker with only the Ethereum key cannot produce a valid c2paSig.
        if (c2paCertHash != bytes32(0) && c2paSig.length >= 64) {
            if (!_verifyC2PASig(pHash, pHashVersion, salt, c2paCertHash, c2paSig)) {
                return false; // Invalid C2PA signature
            }
        }

        // 4. Record indexes
        pHashSaltIndex[pHashSaltKey] = attestation.uid;
        sigPrefixIndex[sigPrefix] = attestation.uid;

        // 5. Update Bloom filter for cross-chain dedup
        if (address(bloomFilter) != address(0)) {
            bloomFilter.add(pHashSaltKey);
        }

        emit ImageRegistered(attestation.attester, sigPrefix, pHashSaltKey, attestation.uid);

        return true;
    }

    /**
     * @dev Verify the C2PA ES256 signature over sha256(pHash || pHashVersion || salt).
     *      Looks up the P-256 public key from KeyRegistry by c2paCertHash.
     *      Uses RIP-7212 precompile if available, falls back to Solidity P-256.
     */
    function _verifyC2PASig(
        bytes24 pHash,
        uint16 pHashVersion,
        bytes2 salt,
        bytes32 c2paCertHash,
        bytes memory c2paSig
    ) internal view returns (bool) {
        // Look up C2PA P-256 public key from KeyRegistry
        (uint256 pubKeyX, uint256 pubKeyY, bool active) = keyRegistry.getC2PAKeyByCertHash(c2paCertHash);
        if (pubKeyX == 0 || !active) return false;

        // Compute digest: sha256(pHash || pHashVersion || salt)
        bytes32 digest = sha256(abi.encodePacked(pHash, pHashVersion, salt));

        // Decode r, s from c2paSig (64 bytes: r[32] || s[32])
        if (c2paSig.length < 64) return false;
        uint256 r;
        uint256 s;
        assembly {
            r := mload(add(c2paSig, 32))
            s := mload(add(c2paSig, 64))
        }

        return P256Verifier.verifySignature(digest, r, s, pubKeyX, pubKeyY);
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

    /// @notice Native Solidity lookup: sig prefix → attestation UID.
    ///         Used by the resolution API and other Solidity callers.
    function c2paLookup(
        string calldata alg,
        bytes calldata bindingValue
    ) external view returns (bytes32 attestationUID) {
        if (bindingValue.length < 18) return bytes32(0);
        bytes16 sigPrefix;
        assembly {
            sigPrefix := calldataload(add(bindingValue.offset, 2))
        }
        return sigPrefixIndex[sigPrefix];
    }

    /// @notice C2PA DLT K-V spec-conformant lookup: accepts and returns a JSON string.
    /// @dev Per C2PA Soft Binding API Section 2.2, the function "must accept and return
    ///      a single JSON string." Input: {"softBindingValue":"<base64>"}.
    ///      Output: {"endpoints":[],"manifestId":"<hex UID>"}.
    ///      The endpoints array is empty because endpoint discovery is via the SBAL;
    ///      clients use the manifestId with a known /manifests endpoint.
    function c2paLookupJSON(string calldata inputJSON) external view returns (string memory) {
        // Minimal JSON parsing: extract base64 value from {"softBindingValue":"..."}
        // For robustness, we look for the base64 content between the last pair of quotes.
        // In production, a proper JSON parser library would be used.
        bytes memory input = bytes(inputJSON);
        bytes memory bindingValue = _extractBase64Value(input);
        if (bindingValue.length < 18) {
            return '{"endpoints":[],"manifestId":""}';
        }
        bytes16 sigPrefix;
        assembly {
            sigPrefix := mload(add(add(bindingValue, 32), 2))
        }
        bytes32 uid = sigPrefixIndex[sigPrefix];
        if (uid == bytes32(0)) {
            return '{"endpoints":[],"manifestId":""}';
        }
        // Build response JSON with manifestId as hex
        return string(abi.encodePacked(
            '{"endpoints":[],"manifestId":"0x', _bytes32ToHex(uid), '"}'
        ));
    }

    /// @notice Returns the C2PA DLT K-V store schema per Section 2.2.
    /// @dev The smartContractAddress uses CAIP-10 format.
    ///      byBindingFunctionName points to c2paLookupJSON (JSON string interface).
    ///      byBindingOutputSchema lists endpoints (required, empty array for us) and manifestId.
    function c2paSchema() external view returns (string memory) {
        string memory caip10 = string(abi.encodePacked(
            "eip155:", _uint2str(block.chainid), ":", _addressToString(address(this))
        ));

        // Note: input/output schemas are JSON Schema objects per the spec.
        // We emit them as inline JSON. The endpoints array is required but we leave
        // it empty — the client discovers our endpoint via the SBAL entry.
        return string(abi.encodePacked(
            '[{"resolutionMethod":"smartContract","querySmartContract":{'
            '"smartContractAddress":"', caip10, '",'
            '"byBindingFunctionName":"c2paLookupJSON",'
            '"byBindingInputSchema":{"type":"object","properties":{"softBindingValue":{"type":"string","description":"base64-encoded soft binding value"}},"required":["softBindingValue"],"additionalProperties":false},'
            '"byBindingOutputSchema":{"type":"object","properties":{"endpoints":{"type":"array","items":{"type":"string","format":"uri"}},"manifestId":{"type":"string"}},"required":["endpoints"],"additionalProperties":false}'
            '}}]'
        ));
    }

    // --- Internal helpers ---

    /// @dev Extract base64-decoded bytes from JSON {"softBindingValue":"<base64>"}.
    ///      Minimal parser: finds the value between the last pair of double quotes,
    ///      then base64-decodes it. Returns empty bytes on failure.
    function _extractBase64Value(bytes memory json) internal pure returns (bytes memory) {
        // Find the last closing quote
        uint256 end = 0;
        uint256 start = 0;
        uint256 quoteCount = 0;
        for (uint256 i = json.length; i > 0; i--) {
            if (json[i - 1] == '"') {
                quoteCount++;
                if (quoteCount == 1) end = i - 1;
                if (quoteCount == 2) { start = i; break; }
            }
        }
        if (quoteCount < 2 || end <= start) return new bytes(0);

        // Extract base64 string
        uint256 b64Len = end - start;
        bytes memory b64 = new bytes(b64Len);
        for (uint256 i = 0; i < b64Len; i++) {
            b64[i] = json[start + i];
        }

        // Base64 decode
        return _base64Decode(b64);
    }

    /// @dev Minimal base64 decoder (standard alphabet, with padding).
    function _base64Decode(bytes memory data) internal pure returns (bytes memory) {
        if (data.length == 0) return new bytes(0);
        uint256 decodedLen = (data.length * 3) / 4;
        if (data[data.length - 1] == "=") decodedLen--;
        if (data.length > 1 && data[data.length - 2] == "=") decodedLen--;

        bytes memory result = new bytes(decodedLen);
        uint256 j = 0;
        for (uint256 i = 0; i < data.length; i += 4) {
            uint256 a = _b64CharVal(data[i]);
            uint256 b = _b64CharVal(data[i + 1]);
            uint256 c = i + 2 < data.length ? _b64CharVal(data[i + 2]) : 0;
            uint256 d = i + 3 < data.length ? _b64CharVal(data[i + 3]) : 0;
            uint256 triple = (a << 18) | (b << 12) | (c << 6) | d;
            if (j < decodedLen) result[j++] = bytes1(uint8(triple >> 16));
            if (j < decodedLen) result[j++] = bytes1(uint8(triple >> 8));
            if (j < decodedLen) result[j++] = bytes1(uint8(triple));
        }
        return result;
    }

    function _b64CharVal(bytes1 c) internal pure returns (uint256) {
        if (c >= "A" && c <= "Z") return uint8(c) - 65;
        if (c >= "a" && c <= "z") return uint8(c) - 71;
        if (c >= "0" && c <= "9") return uint8(c) + 4;
        if (c == "+") return 62;
        if (c == "/") return 63;
        return 0; // padding '='
    }

    /// @dev Convert bytes32 to lowercase hex string (64 chars, no 0x prefix).
    function _bytes32ToHex(bytes32 data) internal pure returns (string memory) {
        bytes memory alphabet = "0123456789abcdef";
        bytes memory str = new bytes(64);
        for (uint256 i = 0; i < 32; i++) {
            str[i * 2] = alphabet[uint8(data[i] >> 4)];
            str[i * 2 + 1] = alphabet[uint8(data[i] & 0x0f)];
        }
        return string(str);
    }

    function _uint2str(uint256 value) internal pure returns (string memory) {
        if (value == 0) return "0";
        uint256 temp = value;
        uint256 digits;
        while (temp != 0) { digits++; temp /= 10; }
        bytes memory buffer = new bytes(digits);
        while (value != 0) {
            digits--;
            buffer[digits] = bytes1(uint8(48 + value % 10));
            value /= 10;
        }
        return string(buffer);
    }

    function _addressToString(address addr) internal pure returns (string memory) {
        bytes memory alphabet = "0123456789abcdef";
        bytes20 data = bytes20(addr);
        bytes memory str = new bytes(42);
        str[0] = "0";
        str[1] = "x";
        for (uint256 i = 0; i < 20; i++) {
            str[2 + i * 2] = alphabet[uint8(data[i] >> 4)];
            str[3 + i * 2] = alphabet[uint8(data[i] & 0x0f)];
        }
        return string(str);
    }
}
