"use client";

import Link from "next/link";
import { Address } from "@scaffold-ui/components";
import type { NextPage } from "next";
import { hardhat } from "viem/chains";
import { useAccount } from "wagmi";
import { BugAntIcon, MagnifyingGlassIcon, KeyIcon, UsersIcon, FunnelIcon, PhotoIcon } from "@heroicons/react/24/outline";
import { useTargetNetwork } from "~~/hooks/scaffold-eth";

const Home: NextPage = () => {
  const { address: connectedAddress } = useAccount();
  const { targetNetwork } = useTargetNetwork();

  return (
    <>
      <div className="flex items-center flex-col grow pt-10">
        <div className="px-5">
          <h1 className="text-center">
            <span className="block text-2xl mb-2">Welcome to the</span>
            <span className="block text-4xl font-bold">Image Authentication Ledger</span>
          </h1>
          <div className="flex justify-center items-center space-x-2 flex-col">
            <p className="my-2 font-medium">Connected Address:</p>
            <Address
              address={connectedAddress}
              chain={targetNetwork}
              blockExplorerAddressLink={
                targetNetwork.id === hardhat.id ? `/blockexplorer/address/${connectedAddress}` : undefined
              }
            />
          </div>
          <p className="text-center text-lg max-w-2xl">
            Explore registered users, signing keys, on-chain image records, cross-chain duplicate
            detection, and transaction history for the stego-backed image authentication system.
          </p>
          <p className="text-center text-lg max-w-2xl">
            Use the navigation above to inspect registered users, key lifecycles, image attestations,
            the Bloom filter, raw contract functions, and local chain activity.
          </p>
        </div>

        <div className="grow bg-base-300 w-full mt-16 px-8 py-12">
          <div className="max-w-7xl mx-auto grid gap-6 md:grid-cols-2 xl:grid-cols-5">
            <Link
              href="/users"
              className="flex flex-col bg-base-100 px-8 py-8 rounded-3xl shadow-sm hover:shadow-lg hover:-translate-y-1 transition-all"
            >
              <UsersIcon className="h-8 w-8 fill-secondary mb-4" />
              <h2 className="text-xl font-bold mb-2">Users</h2>
              <p className="text-sm text-left text-base-content/80">
                Browse all addresses that have registered signing keys and jump directly into their key history.
              </p>
            </Link>

            <Link
              href="/keys"
              className="flex flex-col bg-base-100 px-8 py-8 rounded-3xl shadow-sm hover:shadow-lg hover:-translate-y-1 transition-all"
            >
              <KeyIcon className="h-8 w-8 fill-secondary mb-4" />
              <h2 className="text-xl font-bold mb-2">Keys</h2>
              <p className="text-sm text-left text-base-content/80">
                Inspect key lifecycle history, see which keys are active or revoked, and register or revoke keys.
              </p>
            </Link>

            <Link
              href="/images"
              className="flex flex-col bg-base-100 px-8 py-8 rounded-3xl shadow-sm hover:shadow-lg hover:-translate-y-1 transition-all"
            >
              <PhotoIcon className="h-8 w-8 fill-secondary mb-4" />
              <h2 className="text-xl font-bold mb-2">Images</h2>
              <p className="text-sm text-left text-base-content/80">
                Search registered image attestations by signature or by `(pHash, salt)` and inspect ledger-backed metadata.
              </p>
            </Link>

            <Link
              href="/bloom"
              className="flex flex-col bg-base-100 px-8 py-8 rounded-3xl shadow-sm hover:shadow-lg hover:-translate-y-1 transition-all"
            >
              <FunnelIcon className="h-8 w-8 fill-secondary mb-4" />
              <h2 className="text-xl font-bold mb-2">Bloom Filter</h2>
              <p className="text-sm text-left text-base-content/80">
                Check cross-chain duplicate detection status and test whether a `(pHash, salt)` pair might already exist.
              </p>
            </Link>

            <Link
              href="/blockexplorer"
              className="flex flex-col bg-base-100 px-8 py-8 rounded-3xl shadow-sm hover:shadow-lg hover:-translate-y-1 transition-all"
            >
              <MagnifyingGlassIcon className="h-8 w-8 fill-secondary mb-4" />
              <h2 className="text-xl font-bold mb-2">Block Explorer</h2>
              <p className="text-sm text-left text-base-content/80">
                Review local transactions, decoded contract calls, contract deployments, and address activity.
              </p>
            </Link>
          </div>

          <div className="max-w-4xl mx-auto mt-10 bg-base-100 rounded-3xl px-8 py-8 shadow-sm">
            <div className="flex items-start gap-4">
              <BugAntIcon className="h-8 w-8 fill-secondary shrink-0 mt-1" />
              <div>
                <h2 className="text-xl font-bold mb-2">Debug Contracts</h2>
                <p className="text-sm text-base-content/80">
                  Need direct access to every contract function? Use the{" "}
                  <Link href="/debug" className="link link-primary">
                    Debug Contracts
                  </Link>{" "}
                  page for raw read/write interaction with `KeyRegistry` and `CrossChainBloomFilter`.
                </p>
              </div>
            </div>
          </div>
        </div>
      </div>
    </>
  );
};

export default Home;
