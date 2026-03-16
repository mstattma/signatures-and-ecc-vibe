import { DebugContracts } from "./_components/DebugContracts";
import type { NextPage } from "next";
import { getMetadata } from "~~/utils/scaffold-eth/getMetadata";

export const metadata = getMetadata({
  title: "Debug Contracts",
  description: "Debug your deployed 🏗 Scaffold-ETH 2 contracts in an easy way",
});

const Debug: NextPage = () => {
  return (
    <>
      <DebugContracts />
      <div className="text-center mt-8 bg-secondary p-10">
        <h1 className="text-4xl my-0">Image Authentication Ledger</h1>
        <p className="text-neutral">
          Interact with the on-chain contracts for image authentication.
          <br />
          <strong>KeyRegistry</strong>: Register, rotate, and revoke BLS and C2PA (ES256/P-256) signing keys.
          <br />
          <strong>CrossChainBloomFilter</strong>: Check and manage cross-chain duplicate detection.
          <br />
          <strong>ImageAuthResolver</strong>: EAS resolver with P-256 signature verification, C2PA lookup, and dedup indexes.
          <br />
          <strong>ReputationRegistry</strong>: Attester reputation scores, endorsements, and dispute records.
        </p>
      </div>
    </>
  );
};

export default Debug;
