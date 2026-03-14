import { BloomStatus } from "./_components/BloomStatus";
import type { NextPage } from "next";
import { getMetadata } from "~~/utils/scaffold-eth/getMetadata";

export const metadata = getMetadata({
  title: "Bloom Filter",
  description: "Cross-chain duplicate detection via Bloom filter",
});

const Bloom: NextPage = () => {
  return (
    <>
      <div className="text-center mt-8 mb-4">
        <h1 className="text-4xl font-bold">Cross-Chain Bloom Filter</h1>
        <p className="text-neutral mt-2">
          Check if a (pHash, salt) combination is already registered across any chain.
          <br />
          The Bloom filter provides trustless duplicate detection with zero false negatives.
        </p>
      </div>
      <BloomStatus />
    </>
  );
};

export default Bloom;
