"use client";

import { useMemo } from "react";
import Link from "next/link";
import { useScaffoldEventHistory, useScaffoldReadContract } from "~~/hooks/scaffold-eth";

import { SCHEME_NAMES } from "~~/utils/imageauth/constants";

function UserRow({ address }: { address: string }) {
  const { data: keyCount } = useScaffoldReadContract({
    contractName: "KeyRegistry",
    functionName: "keyCount",
    args: [address],
  });

  const { data: c2paKeyCount } = useScaffoldReadContract({
    contractName: "KeyRegistry",
    functionName: "c2paKeyCount",
    args: [address],
  });

  const { data: activeIndex } = useScaffoldReadContract({
    contractName: "KeyRegistry",
    functionName: "activeKeyIndex",
    args: [address],
  });

  const { data: activeKey } = useScaffoldReadContract({
    contractName: "KeyRegistry",
    functionName: "getKey",
    args: [address, activeIndex !== undefined ? activeIndex : 0n],
    // @ts-ignore
    query: { enabled: activeIndex !== undefined && keyCount !== undefined && keyCount > 0n },
  });

  const { data: repScore } = useScaffoldReadContract({
    contractName: "ReputationRegistry",
    functionName: "score",
    args: [address],
  });

  const count = keyCount ? Number(keyCount) : 0;
  const c2paCount = c2paKeyCount ? Number(c2paKeyCount) : 0;
  const schemeName = activeKey
    ? (SCHEME_NAMES[activeKey[1]] || "Unknown")
    : "-";
  const activatedAt = activeKey ? new Date(Number(activeKey[2]) * 1000) : null;

  return (
    <tr className="hover">
      <td className="font-mono text-sm">
        <Link href={`/keys?address=${address}`} className="link link-primary">
          {address.slice(0, 6)}...{address.slice(-4)}
        </Link>
      </td>
      <td className="font-mono text-xs break-all max-w-xs hidden md:table-cell">
        {address}
      </td>
      <td className="text-center">{count}</td>
      <td className="text-center">{c2paCount}</td>
      <td>{schemeName}</td>
      <td className="text-center">{repScore?.toString() ?? "-"}</td>
      <td className="text-xs">{activatedAt ? activatedAt.toLocaleString() : "-"}</td>
      <td>
        <Link href={`/keys?address=${address}`} className="btn btn-xs btn-primary">
          Keys
        </Link>
        <Link href={`/reputation?address=${address}`} className="btn btn-xs btn-ghost ml-1">
          Rep
        </Link>
      </td>
    </tr>
  );
}

export function UserList() {
  const { data: blsEvents, isLoading: blsLoading } = useScaffoldEventHistory({
    contractName: "KeyRegistry",
    eventName: "KeyActivated",
    fromBlock: 0n,
  });

  const { data: c2paEvents, isLoading: c2paLoading } = useScaffoldEventHistory({
    // @ts-ignore external contract
    contractName: "KeyRegistry",
    eventName: "C2PAKeyActivated",
    fromBlock: 0n,
  });

  const isLoading = blsLoading || c2paLoading;

  // Deduplicate: extract unique user addresses from both BLS and C2PA key events
  const uniqueUsers = useMemo(() => {
    const seen = new Set<string>();
    const users: string[] = [];
    const addUsers = (events: any[] | undefined) => {
      if (!events) return;
      for (const event of events) {
        const addr = event.args?.user as string | undefined;
        if (addr && !seen.has(addr.toLowerCase())) {
          seen.add(addr.toLowerCase());
          users.push(addr);
        }
      }
    };
    addUsers(blsEvents);
    addUsers(c2paEvents);
    return users;
  }, [blsEvents, c2paEvents]);

  return (
    <div className="flex flex-col gap-6 p-6">
      <div className="card bg-base-200 shadow-xl">
        <div className="card-body">
          <h2 className="card-title">
            Registered Users
            <span className="badge badge-neutral">{uniqueUsers.length}</span>
          </h2>
          {isLoading ? (
            <div className="flex justify-center p-8">
              <span className="loading loading-spinner loading-lg"></span>
            </div>
          ) : uniqueUsers.length === 0 ? (
            <p className="text-neutral">No users have registered keys yet.</p>
          ) : (
            <div className="overflow-x-auto">
              <table className="table table-sm">
                <thead>
                  <tr>
                    <th>Address</th>
                    <th className="hidden md:table-cell">Full Address</th>
                    <th>BLS Keys</th>
                    <th>C2PA Keys</th>
                    <th>Active Scheme</th>
                    <th>Reputation</th>
                    <th>Last Key Activated</th>
                    <th></th>
                  </tr>
                </thead>
                <tbody>
                  {uniqueUsers.map(addr => (
                    <UserRow key={addr} address={addr} />
                  ))}
                </tbody>
              </table>
            </div>
          )}
        </div>
      </div>
    </div>
  );
}
