"use client";

import React from "react";
import Link from "next/link";
import { useParams } from "next/navigation";
import { decodeAbiParameters, parseAbi } from "viem";
import { usePublicClient } from "wagmi";
import externalContracts from "~~/contracts/externalContracts";
import { useTargetNetwork } from "~~/hooks/scaffold-eth";

const easAbi = parseAbi([
  "function getAttestation(bytes32 uid) view returns ((bytes32 uid, bytes32 schema, uint64 time, uint64 expirationTime, uint64 revocationTime, bytes32 refUID, address recipient, address attester, bool revocable, bytes data))",
]);

const IMAGE_ATTESTATION_TYPES = [
  { type: "bytes16", name: "sigPrefix" },
  { type: "bytes", name: "signature" },
  { type: "uint8", name: "scheme" },
  { type: "bytes", name: "publicKey" },
  { type: "bytes24", name: "pHash" },
  { type: "uint16", name: "pHashVersion" },
  { type: "bytes2", name: "salt" },
  { type: "bytes32", name: "fileHash" },
  { type: "bytes32", name: "metadataCID" },
] as const;

const SCHEME_NAMES: Record<number, string> = {
  0: "UOV-80",
  1: "UOV-100",
  2: "BLS-BN158",
  3: "BLS12-381",
};

export default function ImageDetailPage() {
  const params = useParams();
  const uid = params.uid as `0x${string}`;
  const publicClient = usePublicClient();
  const { targetNetwork } = useTargetNetwork();
  const eas = (externalContracts as any)?.[targetNetwork.id]?.EAS;

  const [state, setState] = React.useState<{ loading: boolean; error?: string; att?: any; decoded?: any }>({ loading: true });

  React.useEffect(() => {
    let cancelled = false;
    async function run() {
      if (!publicClient || !eas?.address || !uid) {
        setState({ loading: false, error: "EAS not available on this chain." });
        return;
      }
      try {
        const att = await publicClient.readContract({ address: eas.address, abi: easAbi, functionName: "getAttestation", args: [uid] });
        const decoded = decodeAbiParameters(IMAGE_ATTESTATION_TYPES as any, att.data as `0x${string}`);
        if (!cancelled) setState({ loading: false, att, decoded });
      } catch (e: any) {
        if (!cancelled) setState({ loading: false, error: e?.shortMessage || e?.message || "Failed to load attestation" });
      }
    }
    void run();
    return () => { cancelled = true; };
  }, [publicClient, eas?.address, uid]);

  if (state.loading) return <div className="p-8"><span className="loading loading-spinner loading-lg" /></div>;
  if (state.error) return <div className="p-8 alert alert-error"><span>{state.error}</span></div>;

  const { att, decoded } = state;

  return (
    <div className="p-6 max-w-5xl mx-auto">
      <h1 className="text-3xl font-bold mb-4">Image Attestation Detail</h1>
      <div className="card bg-base-200 shadow-xl">
        <div className="card-body grid gap-3 text-sm">
          <div><strong>UID:</strong> <code className="break-all">{uid}</code></div>
          <div><strong>Attester:</strong> <Link href={`/keys?address=${att.attester}`} className="link link-primary break-all">{att.attester}</Link></div>
          <div><strong>Timestamp:</strong> {new Date(Number(att.time) * 1000).toLocaleString()}</div>
          <div><strong>Scheme:</strong> {SCHEME_NAMES[Number(decoded[2])] || `Unknown (${decoded[2]})`}</div>
          <div><strong>Signature Prefix:</strong> <code className="break-all">{decoded[0]}</code></div>
          <div><strong>pHash Version:</strong> <code>{decoded[5].toString()}</code></div>
          <div><strong>Salt:</strong> <code>{decoded[6]}</code></div>
          <div><strong>Signature:</strong> <code className="break-all">{decoded[1]}</code></div>
          <div><strong>Public Key:</strong> <code className="break-all">{decoded[3]}</code></div>
          <div><strong>pHash:</strong> <code className="break-all">{decoded[4]}</code></div>
          <div><strong>fileHash:</strong> <code className="break-all">{decoded[7]}</code></div>
          <div><strong>metadataCID:</strong> <code className="break-all">{decoded[8]}</code></div>
        </div>
      </div>
    </div>
  );
}
