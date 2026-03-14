"use client";

import { useState } from "react";
import { useScaffoldReadContract, useScaffoldWriteContract } from "~~/hooks/scaffold-eth";

const SCHEME_NAMES: Record<number, string> = {
  0: "UOV-80",
  1: "UOV-100",
  2: "BLS-BN158",
  3: "BLS12-381",
};

function KeyRow({ user, index }: { user: string; index: number }) {
  const { data } = useScaffoldReadContract({
    contractName: "KeyRegistry",
    functionName: "getKey",
    args: [user, BigInt(index)],
  });

  if (!data) return null;

  const [publicKey, scheme, activatedAt, revokedAt] = data;
  const isActive = revokedAt === 0n;
  const activatedDate = new Date(Number(activatedAt) * 1000);
  const revokedDate = revokedAt > 0n ? new Date(Number(revokedAt) * 1000) : null;

  return (
    <tr className={isActive ? "bg-success/10" : "bg-error/10"}>
      <td className="text-center">{index}</td>
      <td>
        <span className={`badge ${isActive ? "badge-success" : "badge-error"}`}>
          {isActive ? "Active" : "Revoked"}
        </span>
      </td>
      <td>{SCHEME_NAMES[scheme] || `Unknown (${scheme})`}</td>
      <td className="font-mono text-xs break-all max-w-xs">
        {publicKey.slice(0, 20)}...{publicKey.slice(-8)}
      </td>
      <td className="text-xs">{activatedDate.toLocaleString()}</td>
      <td className="text-xs">{revokedDate ? revokedDate.toLocaleString() : "-"}</td>
    </tr>
  );
}

export function KeyList() {
  const [queryAddress, setQueryAddress] = useState("0xf39Fd6e51aad88F6F4ce6aB8827279cffFb92266");
  const [newPK, setNewPK] = useState("");
  const [newScheme, setNewScheme] = useState(2); // BLS-BN158 default
  const [revokeIndex, setRevokeIndex] = useState("");

  const { data: keyCount, refetch: refetchCount } = useScaffoldReadContract({
    contractName: "KeyRegistry",
    functionName: "keyCount",
    args: [queryAddress],
  });

  const { data: activeIndex } = useScaffoldReadContract({
    contractName: "KeyRegistry",
    functionName: "activeKeyIndex",
    args: [queryAddress],
  });

  const { writeContractAsync: registerKey } = useScaffoldWriteContract({
    contractName: "KeyRegistry",
  });

  const { writeContractAsync: revokeKey } = useScaffoldWriteContract({
    contractName: "KeyRegistry",
  });

  const count = keyCount ? Number(keyCount) : 0;

  return (
    <div className="flex flex-col gap-6 p-6">
      {/* Query section */}
      <div className="card bg-base-200 shadow-xl">
        <div className="card-body">
          <h2 className="card-title">Query Keys by Address</h2>
          <div className="flex gap-2">
            <input
              type="text"
              className="input input-bordered flex-1 font-mono text-sm"
              placeholder="0x..."
              value={queryAddress}
              onChange={e => setQueryAddress(e.target.value)}
            />
            <button className="btn btn-primary" onClick={() => refetchCount()}>
              Query
            </button>
          </div>
          <div className="flex gap-4 mt-2">
            <span>
              Total keys: <strong>{count}</strong>
            </span>
            <span>
              Active key index: <strong>{activeIndex !== undefined ? activeIndex.toString() : "-"}</strong>
            </span>
          </div>
        </div>
      </div>

      {/* Key list */}
      {count > 0 && (
        <div className="card bg-base-200 shadow-xl">
          <div className="card-body">
            <h2 className="card-title">
              Registered Keys for <code className="text-sm">{queryAddress.slice(0, 6)}...{queryAddress.slice(-4)}</code>
            </h2>
            <div className="overflow-x-auto">
              <table className="table table-sm">
                <thead>
                  <tr>
                    <th>#</th>
                    <th>Status</th>
                    <th>Scheme</th>
                    <th>Public Key</th>
                    <th>Activated</th>
                    <th>Revoked</th>
                  </tr>
                </thead>
                <tbody>
                  {Array.from({ length: count }, (_, i) => (
                    <KeyRow key={i} user={queryAddress} index={i} />
                  ))}
                </tbody>
              </table>
            </div>
          </div>
        </div>
      )}

      {/* Register new key */}
      <div className="card bg-base-200 shadow-xl">
        <div className="card-body">
          <h2 className="card-title">Register New Key</h2>
          <p className="text-sm text-neutral">
            Registering a new key automatically revokes the previous active key.
          </p>
          <div className="flex flex-col gap-2">
            <input
              type="text"
              className="input input-bordered font-mono text-sm"
              placeholder="Public key (hex bytes, e.g. 0xabcd...)"
              value={newPK}
              onChange={e => setNewPK(e.target.value)}
            />
            <select
              className="select select-bordered"
              value={newScheme}
              onChange={e => setNewScheme(Number(e.target.value))}
            >
              <option value={0}>UOV-80 (post-quantum, 80-bit)</option>
              <option value={1}>UOV-100 (post-quantum, 100-bit)</option>
              <option value={2}>BLS-BN158 (classical, ~78-bit)</option>
              <option value={3}>BLS12-381 (classical, ~120-bit)</option>
            </select>
            <button
              className="btn btn-primary"
              disabled={!newPK}
              onClick={async () => {
                try {
                  await registerKey({
                    functionName: "registerKey",
                    args: [newPK as `0x${string}`, newScheme],
                  });
                  setNewPK("");
                  refetchCount();
                } catch (e) {
                  console.error("registerKey failed:", e);
                }
              }}
            >
              Register Key
            </button>
          </div>
        </div>
      </div>

      {/* Revoke key */}
      <div className="card bg-base-200 shadow-xl">
        <div className="card-body">
          <h2 className="card-title">Revoke Key</h2>
          <div className="flex gap-2">
            <input
              type="number"
              className="input input-bordered w-32"
              placeholder="Key index"
              value={revokeIndex}
              onChange={e => setRevokeIndex(e.target.value)}
            />
            <button
              className="btn btn-error"
              disabled={revokeIndex === ""}
              onClick={async () => {
                try {
                  await revokeKey({
                    functionName: "revokeKey",
                    args: [BigInt(revokeIndex)],
                  });
                  setRevokeIndex("");
                  refetchCount();
                } catch (e) {
                  console.error("revokeKey failed:", e);
                }
              }}
            >
              Revoke
            </button>
          </div>
        </div>
      </div>
    </div>
  );
}
