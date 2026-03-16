"use client";

import Link from "next/link";
import { useState, useEffect } from "react";
import { useSearchParams } from "next/navigation";
import { useScaffoldReadContract, useScaffoldWriteContract } from "~~/hooks/scaffold-eth";
import { SCHEME_NAMES } from "~~/utils/imageauth/constants";

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
        <Link href={`/images?publicKey=${encodeURIComponent(publicKey)}`} className="link link-primary">
          {publicKey.slice(0, 20)}...{publicKey.slice(-8)}
        </Link>
      </td>
      <td className="text-xs">{activatedDate.toLocaleString()}</td>
      <td className="text-xs">{revokedDate ? revokedDate.toLocaleString() : "-"}</td>
    </tr>
  );
}

function C2PAKeyRow({ user, index }: { user: string; index: number }) {
  const { data } = useScaffoldReadContract({
    contractName: "KeyRegistry",
    functionName: "getC2PAKey",
    args: [user, BigInt(index)],
  });

  if (!data) return null;

  const [pubKeyX, pubKeyY, certHash, certCID, activatedAt, revokedAt] = data;
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
      <td className="font-mono text-xs break-all max-w-xs">{String(certHash).slice(0, 18)}...</td>
      <td className="font-mono text-xs break-all max-w-xs">X: {pubKeyX.toString(16).slice(0, 12)}...</td>
      <td className="text-xs">{activatedDate.toLocaleString()}</td>
      <td className="text-xs">{revokedDate ? revokedDate.toLocaleString() : "-"}</td>
    </tr>
  );
}

export function KeyList() {
  const searchParams = useSearchParams();
  const [queryAddress, setQueryAddress] = useState("0xf39Fd6e51aad88F6F4ce6aB8827279cffFb92266");

  // Read address from URL query parameter (?address=0x...)
  useEffect(() => {
    const addr = searchParams.get("address");
    if (addr) setQueryAddress(addr);
  }, [searchParams]);
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

  const { data: c2paKeyCount, refetch: refetchC2PACount } = useScaffoldReadContract({
    contractName: "KeyRegistry",
    functionName: "c2paKeyCount",
    args: [queryAddress],
  });

  const { data: activeC2PAIndex } = useScaffoldReadContract({
    contractName: "KeyRegistry",
    functionName: "activeC2PAKeyIndex",
    args: [queryAddress],
  });

  const { writeContractAsync: registerC2PAKey } = useScaffoldWriteContract({
    contractName: "KeyRegistry",
  });

  const { writeContractAsync: revokeC2PAKey } = useScaffoldWriteContract({
    contractName: "KeyRegistry",
  });

  const [c2paPubKeyX, setC2paPubKeyX] = useState("");
  const [c2paPubKeyY, setC2paPubKeyY] = useState("");
  const [c2paCertHash, setC2paCertHash] = useState("");
  const [c2paCertCID, setC2paCertCID] = useState("");
  const [revokeC2PAIndex, setRevokeC2PAIndex] = useState("");

  const count = keyCount ? Number(keyCount) : 0;
  const c2paCount = c2paKeyCount ? Number(c2paKeyCount) : 0;

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

      {/* Revoke BLS key */}
      <div className="card bg-base-200 shadow-xl">
        <div className="card-body">
          <h2 className="card-title">Revoke BLS Key</h2>
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

      {/* C2PA Signing Keys section */}
      <div className="divider text-lg font-bold">C2PA Signing Keys (ES256 / P-256)</div>

      {c2paCount > 0 && (
        <div className="card bg-base-200 shadow-xl">
          <div className="card-body">
            <h2 className="card-title">
              C2PA Keys for <code className="text-sm">{queryAddress.slice(0, 6)}...{queryAddress.slice(-4)}</code>
              <span className="badge badge-info ml-2">{c2paCount} keys</span>
              {activeC2PAIndex !== undefined && <span className="text-sm font-normal ml-2">Active index: {activeC2PAIndex.toString()}</span>}
            </h2>
            <div className="overflow-x-auto">
              <table className="table table-sm">
                <thead>
                  <tr>
                    <th>#</th>
                    <th>Status</th>
                    <th>Cert Hash</th>
                    <th>Public Key</th>
                    <th>Activated</th>
                    <th>Revoked</th>
                  </tr>
                </thead>
                <tbody>
                  {Array.from({ length: c2paCount }, (_, i) => (
                    <C2PAKeyRow key={i} user={queryAddress} index={i} />
                  ))}
                </tbody>
              </table>
            </div>
          </div>
        </div>
      )}

      {/* Register C2PA Key */}
      <div className="card bg-base-200 shadow-xl">
        <div className="card-body">
          <h2 className="card-title">Register C2PA Key</h2>
          <p className="text-sm text-neutral">
            Register a P-256 public key for C2PA manifest signing. The on-chain resolver verifies c2paSig against this key.
          </p>
          <div className="flex flex-col gap-2">
            <input type="text" className="input input-bordered font-mono text-sm" placeholder="pubKeyX (uint256, decimal or hex)" value={c2paPubKeyX} onChange={e => setC2paPubKeyX(e.target.value)} />
            <input type="text" className="input input-bordered font-mono text-sm" placeholder="pubKeyY (uint256, decimal or hex)" value={c2paPubKeyY} onChange={e => setC2paPubKeyY(e.target.value)} />
            <input type="text" className="input input-bordered font-mono text-sm" placeholder="certHash (bytes32, sha256 of DER cert)" value={c2paCertHash} onChange={e => setC2paCertHash(e.target.value)} />
            <input type="text" className="input input-bordered font-mono text-sm" placeholder="certCID (bytes32, IPFS CID of certificate, optional)" value={c2paCertCID} onChange={e => setC2paCertCID(e.target.value)} />
            <button
              className="btn btn-primary"
              disabled={!c2paPubKeyX || !c2paPubKeyY || !c2paCertHash}
              onClick={async () => {
                try {
                  await registerC2PAKey({
                    functionName: "registerC2PAKey",
                    args: [
                      BigInt(c2paPubKeyX),
                      BigInt(c2paPubKeyY),
                      c2paCertHash as `0x${string}`,
                      (c2paCertCID || "0x0000000000000000000000000000000000000000000000000000000000000000") as `0x${string}`,
                    ],
                  });
                  setC2paPubKeyX(""); setC2paPubKeyY(""); setC2paCertHash(""); setC2paCertCID("");
                  refetchC2PACount();
                } catch (e) {
                  console.error("registerC2PAKey failed:", e);
                }
              }}
            >
              Register C2PA Key
            </button>
          </div>
        </div>
      </div>

      {/* Revoke C2PA Key */}
      <div className="card bg-base-200 shadow-xl">
        <div className="card-body">
          <h2 className="card-title">Revoke C2PA Key</h2>
          <div className="flex gap-2">
            <input type="number" className="input input-bordered w-32" placeholder="Key index" value={revokeC2PAIndex} onChange={e => setRevokeC2PAIndex(e.target.value)} />
            <button
              className="btn btn-error"
              disabled={revokeC2PAIndex === ""}
              onClick={async () => {
                try {
                  await revokeC2PAKey({ functionName: "revokeC2PAKey", args: [BigInt(revokeC2PAIndex)] });
                  setRevokeC2PAIndex("");
                  refetchC2PACount();
                } catch (e) {
                  console.error("revokeC2PAKey failed:", e);
                }
              }}
            >
              Revoke C2PA Key
            </button>
          </div>
        </div>
      </div>
    </div>
  );
}
