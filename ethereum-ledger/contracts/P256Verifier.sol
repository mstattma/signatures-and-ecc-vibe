// SPDX-License-Identifier: MIT
pragma solidity ^0.8.19;

/**
 * @title P256Verifier
 * @notice Verifies ECDSA signatures on the P-256 (secp256r1) curve.
 *
 * Strategy:
 *   1. Try the RIP-7212 precompile at 0x100 (available on Base, Optimism, Arbitrum)
 *   2. Fall back to a pure Solidity implementation using Jacobian coordinates
 *
 * The precompile costs ~3,450 gas. The Solidity fallback costs ~200-300K gas.
 */
library P256Verifier {
    uint256 constant N = 0xFFFFFFFF00000000FFFFFFFFFFFFFFFFBCE6FAADA7179E84F3B9CAC2FC632551;
    uint256 constant P = 0xFFFFFFFF00000001000000000000000000000000FFFFFFFFFFFFFFFFFFFFFFFF;
    uint256 constant A = 0xFFFFFFFF00000001000000000000000000000000FFFFFFFFFFFFFFFFFFFFFFFC;
    uint256 constant GX = 0x6B17D1F2E12C4247F8BCE6E563A440F277037D812DEB33A0F4A13945D898C296;
    uint256 constant GY = 0x4FE342E2FE1A7F9B8EE7EB4A7C0F9E162BCE33576B315ECECBB6406837BF51F5;

    address constant RIP7212_PRECOMPILE = address(0x100);

    function verifySignature(
        bytes32 hash,
        uint256 r,
        uint256 s,
        uint256 x,
        uint256 y
    ) internal view returns (bool) {
        if (r == 0 || r >= N || s == 0 || s >= N) return false;
        if (x == 0 || x >= P || y == 0 || y >= P) return false;

        // Try RIP-7212 precompile first
        {
            bytes memory input = abi.encode(hash, r, s, x, y);
            (bool ok, bytes memory output) = RIP7212_PRECOMPILE.staticcall(input);
            if (ok && output.length == 32) {
                uint256 val = abi.decode(output, (uint256));
                return val == 1;
            }
        }

        // Fallback: pure Solidity verification
        return _verifySolidity(uint256(hash), r, s, x, y);
    }

    function _verifySolidity(
        uint256 hash_, uint256 r, uint256 s, uint256 qx, uint256 qy
    ) private view returns (bool) {
        uint256 sInv = _modExp(s, N - 2, N);
        uint256 u1 = mulmod(hash_, sInv, N);
        uint256 u2 = mulmod(r, sInv, N);

        // Compute u1*G + u2*Q using separate scalar muls then addition
        uint256[3] memory p1 = _ecMul(GX, GY, 1, u1);
        uint256[3] memory p2 = _ecMul(qx, qy, 1, u2);
        uint256[3] memory result = _ecAddJac(p1, p2);

        // Convert to affine
        if (result[2] == 0) return false;
        uint256 zInv = _modExp(result[2], P - 2, P);
        uint256 zInv2 = mulmod(zInv, zInv, P);
        uint256 rx = mulmod(result[0], zInv2, P);

        return (rx % N) == r;
    }

    /// @dev Scalar multiplication using double-and-add in Jacobian coordinates
    function _ecMul(uint256 px, uint256 py, uint256 pz, uint256 k)
        private view returns (uint256[3] memory)
    {
        uint256[3] memory result = [uint256(0), uint256(1), uint256(0)]; // point at infinity
        uint256[3] memory current = [px, py, pz];

        while (k > 0) {
            if (k & 1 == 1) {
                result = _ecAddJac(result, current);
            }
            current = _ecDoubleJac(current);
            k >>= 1;
        }
        return result;
    }

    /// @dev Jacobian point addition
    function _ecAddJac(uint256[3] memory p1, uint256[3] memory p2)
        private view returns (uint256[3] memory)
    {
        if (p1[2] == 0) return p2;
        if (p2[2] == 0) return p1;

        uint256 z1z1 = mulmod(p1[2], p1[2], P);
        uint256 z2z2 = mulmod(p2[2], p2[2], P);
        uint256 u1 = mulmod(p1[0], z2z2, P);
        uint256 u2 = mulmod(p2[0], z1z1, P);
        uint256 s1 = mulmod(p1[1], mulmod(p2[2], z2z2, P), P);
        uint256 s2 = mulmod(p2[1], mulmod(p1[2], z1z1, P), P);

        if (u1 == u2) {
            if (s1 == s2) return _ecDoubleJac(p1);
            return [uint256(0), uint256(1), uint256(0)];
        }

        uint256 h = addmod(u2, P - u1, P);
        uint256 r = addmod(s2, P - s1, P);
        uint256 hh = mulmod(h, h, P);
        uint256 hhh = mulmod(h, hh, P);

        uint256 x3 = addmod(
            addmod(mulmod(r, r, P), P - hhh, P),
            P - mulmod(2, mulmod(u1, hh, P), P),
            P
        );
        uint256 y3 = addmod(
            mulmod(r, addmod(mulmod(u1, hh, P), P - x3, P), P),
            P - mulmod(s1, hhh, P),
            P
        );
        uint256 z3 = mulmod(h, mulmod(p1[2], p2[2], P), P);

        return [x3, y3, z3];
    }

    /// @dev Jacobian point doubling
    function _ecDoubleJac(uint256[3] memory p)
        private view returns (uint256[3] memory)
    {
        if (p[2] == 0) return p;

        uint256 yy = mulmod(p[1], p[1], P);
        uint256 s = mulmod(4, mulmod(p[0], yy, P), P);
        uint256 zz = mulmod(p[2], p[2], P);
        uint256 m = addmod(
            mulmod(3, mulmod(p[0], p[0], P), P),
            mulmod(A, mulmod(zz, zz, P), P),
            P
        );
        uint256 x3 = addmod(mulmod(m, m, P), P - mulmod(2, s, P), P);
        uint256 y3 = addmod(
            mulmod(m, addmod(s, P - x3, P), P),
            P - mulmod(8, mulmod(yy, yy, P), P),
            P
        );
        uint256 z3 = mulmod(2, mulmod(p[1], p[2], P), P);

        return [x3, y3, z3];
    }

    function _modExp(uint256 base, uint256 exp, uint256 mod) private view returns (uint256 result) {
        bytes memory input = abi.encodePacked(
            uint256(32), uint256(32), uint256(32), base, exp, mod
        );
        assembly {
            let success := staticcall(gas(), 0x05, add(input, 32), 192, 0x00, 32)
            if iszero(success) { revert(0, 0) }
            result := mload(0x00)
        }
    }
}
