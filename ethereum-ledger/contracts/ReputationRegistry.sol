// SPDX-License-Identifier: MIT
pragma solidity ^0.8.19;

/**
 * @title ReputationRegistry
 * @notice Tracks reputation scores for image authenticators (attesters).
 *
 * Reputation is based on:
 *   - Number of attestations created
 *   - Community endorsements (upvotes/downvotes from other registered attesters)
 *   - Disputes resolved
 *
 * Only addresses with at least one registered key in the KeyRegistry can
 * endorse or dispute. Scores are view-only and not used for access control
 * in v1; future versions may gate attestation privileges on minimum reputation.
 */

interface IKeyRegistry {
    function keyCount(address user) external view returns (uint256);
}

contract ReputationRegistry {
    struct Reputation {
        uint64 attestationCount;
        int64 endorsementScore;   // can be negative (net downvotes)
        uint64 disputeCount;
        uint64 disputesWon;
        uint64 firstSeenAt;
    }

    IKeyRegistry public keyRegistry;
    mapping(address => Reputation) public reputation;

    // Track who endorsed whom to prevent double-endorsement
    mapping(bytes32 => bool) public hasEndorsed;

    event AttestationCounted(address indexed attester, uint64 newCount);
    event Endorsed(address indexed from, address indexed to, bool positive);
    event DisputeRecorded(address indexed attester, bool won);

    constructor(IKeyRegistry _keyRegistry) {
        keyRegistry = _keyRegistry;
    }

    /// @notice Increment attestation count for an attester.
    /// @dev Called by the resolver or by a trusted relayer after attestation.
    function recordAttestation(address attester) external {
        Reputation storage r = reputation[attester];
        if (r.firstSeenAt == 0) {
            r.firstSeenAt = uint64(block.timestamp);
        }
        r.attestationCount++;
        emit AttestationCounted(attester, r.attestationCount);
    }

    /// @notice Endorse or downvote another attester.
    /// @param target The attester to endorse.
    /// @param positive True for upvote, false for downvote.
    function endorse(address target, bool positive) external {
        require(target != msg.sender, "Cannot endorse self");
        require(keyRegistry.keyCount(msg.sender) > 0, "Must have registered key");

        bytes32 key = keccak256(abi.encodePacked(msg.sender, target));
        require(!hasEndorsed[key], "Already endorsed this attester");
        hasEndorsed[key] = true;

        Reputation storage r = reputation[target];
        if (r.firstSeenAt == 0) {
            r.firstSeenAt = uint64(block.timestamp);
        }

        if (positive) {
            r.endorsementScore++;
        } else {
            r.endorsementScore--;
        }

        emit Endorsed(msg.sender, target, positive);
    }

    /// @notice Record a dispute outcome.
    /// @param attester The attester involved in the dispute.
    /// @param won Whether the attester won the dispute.
    function recordDispute(address attester, bool won) external {
        Reputation storage r = reputation[attester];
        r.disputeCount++;
        if (won) {
            r.disputesWon++;
        }
        emit DisputeRecorded(attester, won);
    }

    /// @notice Get full reputation for an address.
    function getReputation(address user) external view returns (
        uint64 attestationCount,
        int64 endorsementScore,
        uint64 disputeCount,
        uint64 disputesWon,
        uint64 firstSeenAt
    ) {
        Reputation storage r = reputation[user];
        return (r.attestationCount, r.endorsementScore, r.disputeCount, r.disputesWon, r.firstSeenAt);
    }

    /// @notice Simple reputation score: attestations + endorsements - disputes lost.
    function score(address user) external view returns (int64) {
        Reputation storage r = reputation[user];
        return int64(r.attestationCount) + r.endorsementScore -
               int64(uint64(r.disputeCount - r.disputesWon));
    }
}
