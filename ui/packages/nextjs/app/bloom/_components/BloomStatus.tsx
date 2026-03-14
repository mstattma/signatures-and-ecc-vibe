"use client";

import { useState } from "react";
import { keccak256, encodePacked } from "viem";
import { useScaffoldReadContract } from "~~/hooks/scaffold-eth";

export function BloomStatus() {
  const [pHash, setPHash] = useState("");
  const [salt, setSalt] = useState("");
  const [queryKey, setQueryKey] = useState<`0x${string}` | undefined>();

  const { data: entryCount } = useScaffoldReadContract({
    contractName: "CrossChainBloomFilter",
    functionName: "entryCount",
  });

  const { data: mightExist } = useScaffoldReadContract({
    contractName: "CrossChainBloomFilter",
    functionName: "mightContain",
    args: [queryKey || "0x0000000000000000000000000000000000000000000000000000000000000000"],
    // @ts-ignore - only query when we have a key
    query: { enabled: !!queryKey },
  });

  const computeKey = () => {
    if (!pHash || !salt) return;
    try {
      // Pad pHash to bytes24 (24 bytes)
      let phashHex = pHash.startsWith("0x") ? pHash : "0x" + pHash;
      // Pad to 48 hex chars (24 bytes) if shorter
      while (phashHex.length < 50) phashHex += "0";
      // Salt as bytes2
      let saltHex = salt.startsWith("0x") ? salt : "0x" + salt;
      while (saltHex.length < 6) saltHex += "0";

      const key = keccak256(
        encodePacked(["bytes24", "bytes2"], [phashHex as `0x${string}`, saltHex as `0x${string}`])
      );
      setQueryKey(key);
    } catch (e) {
      console.error("Invalid input:", e);
    }
  };

  return (
    <div className="flex flex-col gap-6 p-6">
      {/* Status */}
      <div className="card bg-base-200 shadow-xl">
        <div className="card-body">
          <h2 className="card-title">Bloom Filter Status</h2>
          <div className="stats shadow">
            <div className="stat">
              <div className="stat-title">Total Entries</div>
              <div className="stat-value">{entryCount?.toString() || "0"}</div>
              <div className="stat-desc">Registered (pHash, salt) pairs</div>
            </div>
            <div className="stat">
              <div className="stat-title">Filter Size</div>
              <div className="stat-value">16,384</div>
              <div className="stat-desc">bits (2 KB)</div>
            </div>
            <div className="stat">
              <div className="stat-title">Hash Functions</div>
              <div className="stat-value">10</div>
              <div className="stat-desc">per entry</div>
            </div>
          </div>
          <p className="text-sm text-neutral mt-2">
            Capacity: ~5,000 entries at &lt;1% false positive rate.
            False positives cause a harmless salt retry.
          </p>
        </div>
      </div>

      {/* Duplicate check */}
      <div className="card bg-base-200 shadow-xl">
        <div className="card-body">
          <h2 className="card-title">Check for Duplicates</h2>
          <p className="text-sm text-neutral">
            Check if a (pHash, salt) combination might already be registered.
          </p>
          <div className="flex flex-col gap-2">
            <input
              type="text"
              className="input input-bordered font-mono text-sm"
              placeholder="pHash (hex, e.g. 0xdeef001122...)"
              value={pHash}
              onChange={e => setPHash(e.target.value)}
            />
            <input
              type="text"
              className="input input-bordered font-mono text-sm"
              placeholder="Salt (hex, e.g. 0xf0a1)"
              value={salt}
              onChange={e => setSalt(e.target.value)}
            />
            <button
              className="btn btn-primary"
              disabled={!pHash || !salt}
              onClick={computeKey}
            >
              Check Bloom Filter
            </button>
          </div>

          {queryKey && (
            <div className="mt-4">
              <p className="text-xs font-mono break-all">
                Key: {queryKey}
              </p>
              <div className={`alert mt-2 ${mightExist ? "alert-warning" : "alert-success"}`}>
                {mightExist ? (
                  <span>
                    <strong>Might exist!</strong> This (pHash, salt) may already be registered
                    (or this is a false positive). Try a different salt.
                  </span>
                ) : (
                  <span>
                    <strong>Unique.</strong> This (pHash, salt) is definitely not registered.
                    Safe to proceed with registration.
                  </span>
                )}
              </div>
            </div>
          )}
        </div>
      </div>
    </div>
  );
}
