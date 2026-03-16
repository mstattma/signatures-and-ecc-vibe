"use client";

import { useState, useEffect } from "react";
import { useSearchParams } from "next/navigation";
import { useScaffoldReadContract, useScaffoldWriteContract, useScaffoldEventHistory } from "~~/hooks/scaffold-eth";

export function ReputationView() {
  const searchParams = useSearchParams();
  const [queryAddress, setQueryAddress] = useState("0xf39Fd6e51aad88F6F4ce6aB8827279cffFb92266");
  const [endorseAddress, setEndorseAddress] = useState("");

  useEffect(() => {
    const addr = searchParams.get("address");
    if (addr) setQueryAddress(addr);
  }, [searchParams]);

  const { data: reputation, refetch: refetchRep } = useScaffoldReadContract({
    contractName: "ReputationRegistry",
    functionName: "getReputation",
    args: [queryAddress],
  });

  const { data: score } = useScaffoldReadContract({
    contractName: "ReputationRegistry",
    functionName: "score",
    args: [queryAddress],
  });

  const { writeContractAsync: endorseWrite } = useScaffoldWriteContract({
    contractName: "ReputationRegistry",
  });

  const { data: endorseEvents } = useScaffoldEventHistory({
    // @ts-ignore external contract
    contractName: "ReputationRegistry",
    eventName: "Endorsed",
    fromBlock: 0n,
  });

  const { data: attestCountEvents } = useScaffoldEventHistory({
    // @ts-ignore external contract
    contractName: "ReputationRegistry",
    eventName: "AttestationCounted",
    fromBlock: 0n,
  });

  const recentEndorsements = endorseEvents?.slice(-10).reverse() || [];
  const recentAttestations = attestCountEvents?.slice(-10).reverse() || [];

  return (
    <div className="flex flex-col gap-6 p-6">
      {/* Lookup */}
      <div className="card bg-base-200 shadow-xl">
        <div className="card-body">
          <h2 className="card-title">Reputation Lookup</h2>
          <div className="flex gap-2">
            <input
              type="text"
              className="input input-bordered flex-1 font-mono text-sm"
              placeholder="0x..."
              value={queryAddress}
              onChange={e => setQueryAddress(e.target.value)}
            />
            <button className="btn btn-primary" onClick={() => refetchRep()}>Query</button>
          </div>

          {reputation && (
            <div className="grid gap-4 md:grid-cols-3 mt-4">
              <div className="stat bg-base-100 rounded-xl shadow">
                <div className="stat-title">Score</div>
                <div className="stat-value text-primary">{score?.toString() ?? "0"}</div>
              </div>
              <div className="stat bg-base-100 rounded-xl shadow">
                <div className="stat-title">Attestations</div>
                <div className="stat-value">{reputation[0]?.toString()}</div>
              </div>
              <div className="stat bg-base-100 rounded-xl shadow">
                <div className="stat-title">Endorsement Score</div>
                <div className="stat-value">{reputation[1]?.toString()}</div>
              </div>
              <div className="stat bg-base-100 rounded-xl shadow">
                <div className="stat-title">Disputes</div>
                <div className="stat-value">{reputation[2]?.toString()}</div>
                <div className="stat-desc">Won: {reputation[3]?.toString()}</div>
              </div>
              <div className="stat bg-base-100 rounded-xl shadow">
                <div className="stat-title">First Seen</div>
                <div className="stat-value text-sm">
                  {reputation[4] > 0n ? new Date(Number(reputation[4]) * 1000).toLocaleDateString() : "Never"}
                </div>
              </div>
            </div>
          )}
        </div>
      </div>

      {/* Endorse / Downvote */}
      <div className="card bg-base-200 shadow-xl">
        <div className="card-body">
          <h2 className="card-title">Endorse or Downvote</h2>
          <p className="text-sm text-neutral">
            You must have a registered key in KeyRegistry to endorse. You cannot endorse yourself.
          </p>
          <div className="flex flex-col gap-2">
            <input
              type="text"
              className="input input-bordered font-mono text-sm"
              placeholder="Target address (0x...)"
              value={endorseAddress}
              onChange={e => setEndorseAddress(e.target.value)}
            />
            <div className="flex gap-2">
              <button
                className="btn btn-success flex-1"
                disabled={!endorseAddress}
                onClick={async () => {
                  try {
                    await endorseWrite({ functionName: "endorse", args: [endorseAddress, true] });
                    refetchRep();
                  } catch (e) { console.error("endorse failed:", e); }
                }}
              >
                Upvote
              </button>
              <button
                className="btn btn-error flex-1"
                disabled={!endorseAddress}
                onClick={async () => {
                  try {
                    await endorseWrite({ functionName: "endorse", args: [endorseAddress, false] });
                    refetchRep();
                  } catch (e) { console.error("downvote failed:", e); }
                }}
              >
                Downvote
              </button>
            </div>
          </div>
        </div>
      </div>

      {/* Recent Events */}
      <div className="card bg-base-200 shadow-xl">
        <div className="card-body">
          <h2 className="card-title">Recent Endorsements</h2>
          {recentEndorsements.length === 0 ? (
            <p className="text-sm text-base-content/70">No endorsements yet.</p>
          ) : (
            <div className="overflow-x-auto">
              <table className="table table-sm">
                <thead><tr><th>From</th><th>To</th><th>Type</th></tr></thead>
                <tbody>
                  {recentEndorsements.map((e: any, i: number) => (
                    <tr key={i}>
                      <td className="font-mono text-xs">{e.args?.from?.slice(0, 10)}...</td>
                      <td className="font-mono text-xs">{e.args?.to?.slice(0, 10)}...</td>
                      <td><span className={`badge ${e.args?.positive ? "badge-success" : "badge-error"}`}>{e.args?.positive ? "Upvote" : "Downvote"}</span></td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </div>
          )}
        </div>
      </div>

      <div className="card bg-base-200 shadow-xl">
        <div className="card-body">
          <h2 className="card-title">Recent Attestation Counts</h2>
          {recentAttestations.length === 0 ? (
            <p className="text-sm text-base-content/70">No attestation counts recorded yet.</p>
          ) : (
            <div className="overflow-x-auto">
              <table className="table table-sm">
                <thead><tr><th>Attester</th><th>New Count</th></tr></thead>
                <tbody>
                  {recentAttestations.map((e: any, i: number) => (
                    <tr key={i}>
                      <td className="font-mono text-xs">{e.args?.attester?.slice(0, 10)}...</td>
                      <td>{e.args?.newCount?.toString()}</td>
                    </tr>
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
