"use client";

import { useMemo } from "react";
import Link from "next/link";
import { useScaffoldEventHistory, useScaffoldReadContract } from "~~/hooks/scaffold-eth";

function UserRow({ address }: { address: string }) {
  const { data: keyCount } = useScaffoldReadContract({
    contractName: "KeyRegistry",
    functionName: "keyCount",
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

  const count = keyCount ? Number(keyCount) : 0;
  const schemeName = activeKey
    ? ({ 0: "UOV-80", 1: "UOV-100", 2: "BLS-BN158", 3: "BLS12-381" }[activeKey[1]] || "Unknown")
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
      <td>{schemeName}</td>
      <td className="text-xs">{activatedAt ? activatedAt.toLocaleString() : "-"}</td>
      <td>
        <Link href={`/keys?address=${address}`} className="btn btn-xs btn-primary">
          View Keys
        </Link>
      </td>
    </tr>
  );
}

export function UserList() {
  const { data: events, isLoading } = useScaffoldEventHistory({
    contractName: "KeyRegistry",
    eventName: "KeyActivated",
    fromBlock: 0n,
  });

  // Deduplicate: extract unique user addresses from KeyActivated events
  const uniqueUsers = useMemo(() => {
    if (!events) return [];
    const seen = new Set<string>();
    const users: string[] = [];
    for (const event of events) {
      const addr = event.args?.user as string | undefined;
      if (addr && !seen.has(addr.toLowerCase())) {
        seen.add(addr.toLowerCase());
        users.push(addr);
      }
    }
    return users;
  }, [events]);

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
                    <th>Keys</th>
                    <th>Active Scheme</th>
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
