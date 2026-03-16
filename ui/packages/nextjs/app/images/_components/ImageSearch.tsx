"use client";

import { useEffect, useMemo, useState } from "react";
import Link from "next/link";
import { useSearchParams } from "next/navigation";
import { decodeAbiParameters, encodePacked, keccak256, parseAbi } from "viem";
import { usePublicClient } from "wagmi";
import externalContracts from "~~/contracts/externalContracts";
import { useScaffoldEventHistory, useTargetNetwork } from "~~/hooks/scaffold-eth";

const resolverAbi = parseAbi([
  "function pHashSaltIndex(bytes32) view returns (bytes32)",
  "function sigPrefixIndex(bytes16) view returns (bytes32)",
]);

const easAbi = parseAbi([
  "function getAttestation(bytes32 uid) view returns ((bytes32 uid, bytes32 schema, uint64 time, uint64 expirationTime, uint64 revocationTime, bytes32 refUID, address recipient, address attester, bool revocable, bytes data))",
]);

import { IMAGE_ATTESTATION_TYPES, SCHEME_NAMES, ATT } from "~~/utils/imageauth/constants";

function normalizeHex(input: string) {
  return input.startsWith("0x") ? input : `0x${input}`;
}

export function ImageSearch() {
  const searchParams = useSearchParams();
  const publicClient = usePublicClient();
  const { targetNetwork } = useTargetNetwork();
  const chainId = targetNetwork.id;
  const resolver = (externalContracts as any)?.[chainId]?.ImageAuthResolver;
  const eas = (externalContracts as any)?.[chainId]?.EAS;

  const [signature, setSignature] = useState("");
  const [sigSalt, setSigSalt] = useState("");
  const [phash, setPhash] = useState("");
  const [phashVersion, setPhashVersion] = useState("1");
  const [phashSalt, setPhashSalt] = useState("");
  const [publicKeySearch, setPublicKeySearch] = useState("");
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [result, setResult] = useState<any | null>(null);
  const [recentRecords, setRecentRecords] = useState<any[]>([]);
  const [recentLoading, setRecentLoading] = useState(false);

  const resolverAddress = resolver?.address as `0x${string}` | undefined;
  const easAddress = eas?.address as `0x${string}` | undefined;

  const canSearch = useMemo(() => !!publicClient && !!resolverAddress && !!easAddress, [publicClient, resolverAddress, easAddress]);

  const { data: imageEvents } = useScaffoldEventHistory({
    // @ts-ignore external contract supported in runtime
    contractName: "ImageAuthResolver",
    eventName: "ImageRegistered",
    fromBlock: 0n,
  });

  const recentEvents = useMemo(() => {
    if (!imageEvents) return [];
    return [...imageEvents].slice(-20).reverse();
  }, [imageEvents]);

  useEffect(() => {
    const pk = searchParams.get("publicKey");
    if (pk) setPublicKeySearch(pk);
  }, [searchParams]);

  // Use a stable key to prevent infinite loops if useScaffoldEventHistory returns a new array reference
  const recentEventsKey = useMemo(() => {
    return recentEvents.map((e: any) => e.transactionHash + "-" + e.logIndex).join(",");
  }, [recentEvents]);

  useEffect(() => {
    let cancelled = false;
    async function loadRecent() {
      if (!publicClient || !easAddress || recentEvents.length === 0) {
        setRecentRecords(prev => prev.length === 0 ? prev : []);
        return;
      }
      setRecentLoading(true);
      try {
        const records = await Promise.all(
          recentEvents.map(async (event: any) => {
            const uid = (event.args?.attestationUID || event.args?.attestationUid || event.args?.uid) as `0x${string}`;
            if (!uid || /^0x0+$/.test(uid)) return null;
            const att = await publicClient.readContract({
              address: easAddress,
              abi: easAbi,
              functionName: "getAttestation",
              args: [uid],
            });
            const decoded = decodeAbiParameters(IMAGE_ATTESTATION_TYPES as any, att.data as `0x${string}`);
            return { uid, att, decoded, event };
          }),
        );
        if (!cancelled) setRecentRecords(records.filter(Boolean));
      } catch (e) {
        if (!cancelled) console.error("Failed loading recent image records", e);
      } finally {
        if (!cancelled) setRecentLoading(false);
      }
    }
    void loadRecent();
    return () => {
      cancelled = true;
    };
  }, [publicClient, easAddress, recentEventsKey]); // use string key instead of array ref

  const lookupBySignature = async () => {
    if (!canSearch || !resolverAddress || !publicClient || !easAddress) return;
    setLoading(true);
    setError(null);
    setResult(null);
    try {
      const sigHex = normalizeHex(signature) as `0x${string}`;
      const prefix = (`0x${sigHex.slice(2, 34)}`) as `0x${string}`;

      const uid = await publicClient.readContract({
        address: resolverAddress,
        abi: resolverAbi,
        functionName: "sigPrefixIndex",
        args: [prefix as `0x${string}`],
      });

      if (uid === "0x0000000000000000000000000000000000000000000000000000000000000000") {
        setError("No image record found for that signature prefix on this chain.");
        return;
      }

      const att = await publicClient.readContract({
        address: easAddress,
        abi: easAbi,
        functionName: "getAttestation",
        args: [uid],
      });

      const decoded = decodeAbiParameters(IMAGE_ATTESTATION_TYPES as any, att.data as `0x${string}`);
      setResult({
        lookup: "signature",
        uid,
        attestation: att,
        decoded,
        queriedSalt: sigSalt || null,
      });
    } catch (e: any) {
      setError(e?.shortMessage || e?.message || "Signature lookup failed");
    } finally {
      setLoading(false);
    }
  };

  const lookupByPhash = async () => {
    if (!canSearch || !resolverAddress || !publicClient) return;
    setLoading(true);
    setError(null);
    setResult(null);
    try {
      const phashHex = normalizeHex(phash);
      const saltHex = normalizeHex(phashSalt || "0x0000");
      let pHash24 = phashHex;
      while (pHash24.length < 50) pHash24 += "0";
      const paddedSalt = saltHex.length === 4 ? `${saltHex}00` : saltHex;
      const key = keccak256(encodePacked(["uint16", "bytes24", "bytes2"], [Number(phashVersion), pHash24 as `0x${string}`, paddedSalt as `0x${string}`]));

      const uid = await publicClient.readContract({
        address: resolverAddress,
        abi: resolverAbi,
        functionName: "pHashSaltIndex",
        args: [key],
      });

      if (uid === "0x0000000000000000000000000000000000000000000000000000000000000000") {
        setError("No image record found for that (pHash, salt) on this chain.");
        return;
      }

      const att = await publicClient.readContract({
        address: easAddress!,
        abi: easAbi,
        functionName: "getAttestation",
        args: [uid],
      });

      const decoded = decodeAbiParameters(IMAGE_ATTESTATION_TYPES as any, att.data as `0x${string}`);
      setResult({
        lookup: "phash",
        uid,
        attestation: att,
        decoded,
        computedKey: key,
      });
    } catch (e: any) {
      setError(e?.shortMessage || e?.message || "pHash lookup failed");
    } finally {
      setLoading(false);
    }
  };

  if (!resolverAddress) {
    return (
      <div className="alert alert-info mt-6">
        <span>
          No <code>ImageAuthResolver</code> is deployed for the current chain ({targetNetwork.name}).
          This view is only active on chains where the resolver and EAS are available.
        </span>
      </div>
    );
  }

  return (
    <div className="flex flex-col gap-6 p-6">
      <div className="card bg-base-200 shadow-xl">
        <div className="card-body">
          <h2 className="card-title">Recent Image Records</h2>
          <p className="text-sm text-base-content/70">
            “Recent” currently means the latest <strong>20</strong> `ImageRegistered` events on the active chain,
            sorted newest-first by block/log order.
          </p>
          {recentLoading ? (
            <div className="flex justify-center p-4"><span className="loading loading-spinner loading-md" /></div>
          ) : recentRecords.length === 0 ? (
            <div className="text-sm text-base-content/70">No image registrations found on this chain yet.</div>
          ) : (
            <div className="overflow-x-auto">
              <table className="table table-sm">
                <thead>
                  <tr>
                    <th>UID</th>
                    <th>Attester</th>
                    <th>Sig Prefix</th>
                    <th>Salt</th>
                    <th>Signature</th>
                    <th>Scheme</th>
                    <th>pHashSaltKey</th>
                  </tr>
                </thead>
                <tbody>
                  {recentRecords.map((record: any, idx: number) => (
                    <tr key={idx}>
                      <td><Link href={`/images/${record.uid}`} className="link link-primary text-xs break-all">{record.uid}</Link></td>
                      <td>
                        <Link href={`/keys?address=${record.att.attester}`} className="link link-primary text-xs break-all">
                          {record.att.attester}
                        </Link>
                      </td>
                      <td><code className="text-xs break-all">{record.decoded[ATT.sigPrefix]}</code></td>
                      <td><code className="text-xs break-all">{record.decoded[ATT.salt]}</code></td>
                      <td><code className="text-xs break-all">{record.decoded[ATT.signature]}</code></td>
                      <td>{SCHEME_NAMES[Number(record.decoded[ATT.scheme])] || `Unknown (${record.decoded[ATT.scheme]})`}</td>
                      <td><code className="text-xs break-all">{record.event.args?.pHashSaltKey || "-"}</code></td>
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
          <h2 className="card-title">Search by Signature</h2>
          <p className="text-sm text-base-content/70">Search uses the first 16 bytes of the signature (sig prefix) via the resolver index. Salt is optional here and only used as an operator note.</p>
          <div className="flex flex-col gap-2">
            <input className="input input-bordered font-mono text-sm" placeholder="Signature hex (0x...)" value={signature} onChange={e => setSignature(e.target.value)} />
            <input className="input input-bordered font-mono text-sm" placeholder="Salt hex (optional, 0x...)" value={sigSalt} onChange={e => setSigSalt(e.target.value)} />
            <button className="btn btn-primary" disabled={!signature || loading || !canSearch} onClick={lookupBySignature}>Search by Signature</button>
          </div>
        </div>
      </div>

      <div className="card bg-base-200 shadow-xl">
        <div className="card-body">
          <h2 className="card-title">Search by pHash + Salt</h2>
          <p className="text-sm text-base-content/70">Computes <code>keccak256(uint16(pHashVersion) || bytes24(pHash) || bytes2(salt))</code> and queries the resolver&apos;s authoritative per-chain dedup index.</p>
          <div className="flex flex-col gap-2">
            <input className="input input-bordered font-mono text-sm" placeholder="pHash hex (0x...)" value={phash} onChange={e => setPhash(e.target.value)} />
            <input className="input input-bordered font-mono text-sm" placeholder="pHash version (uint16, default 1)" value={phashVersion} onChange={e => setPhashVersion(e.target.value)} />
            <input className="input input-bordered font-mono text-sm" placeholder="Salt hex (0x..., usually 1-2 bytes)" value={phashSalt} onChange={e => setPhashSalt(e.target.value)} />
            <button className="btn btn-primary" disabled={!phash || !phashSalt || loading || !canSearch} onClick={lookupByPhash}>Search by pHash + Salt</button>
          </div>
        </div>
      </div>

      <div className="card bg-base-200 shadow-xl">
        <div className="card-body">
          <h2 className="card-title">Search by Public Key</h2>
          <p className="text-sm text-base-content/70">
            List all recently loaded image records whose attestation contains the specified public key.
            This is useful when navigating from the Keys page.
          </p>
          <div className="flex flex-col gap-2">
            <input
              className="input input-bordered font-mono text-sm"
              placeholder="Public key hex (0x...)"
              value={publicKeySearch}
              onChange={e => setPublicKeySearch(e.target.value)}
            />
          </div>
        </div>
      </div>

      {loading && <div className="flex justify-center"><span className="loading loading-spinner loading-lg" /></div>}
      {error && <div className="alert alert-error"><span>{error}</span></div>}

      {result && (
        <div className="card bg-base-200 shadow-xl">
          <div className="card-body">
            <h2 className="card-title">Image Record</h2>
            <div className="grid gap-3 md:grid-cols-2 text-sm">
              <div><strong>Lookup mode:</strong> {result.lookup}</div>
              <div><strong>Attestation UID:</strong> <Link href={`/images/${result.uid}`} className="link link-primary break-all">{result.uid}</Link></div>
              <div><strong>Attester:</strong> <Link href={`/keys?address=${result.attestation.attester}`} className="link link-primary break-all">{result.attestation.attester}</Link></div>
              <div><strong>Timestamp:</strong> {new Date(Number(result.attestation.time) * 1000).toLocaleString()}</div>
              <div><strong>Scheme:</strong> {SCHEME_NAMES[Number(result.decoded[ATT.scheme])] || `Unknown (${result.decoded[ATT.scheme]})`}</div>
              <div><strong>pHash Version:</strong> <code>{result.decoded[ATT.pHashVersion].toString()}</code></div>
              <div><strong>Salt:</strong> <code>{result.decoded[ATT.salt]}</code></div>
              <div className="md:col-span-2"><strong>Signature prefix:</strong> <code className="break-all">{result.decoded[ATT.sigPrefix]}</code></div>
              <div className="md:col-span-2"><strong>Signature:</strong> <code className="break-all">{result.decoded[ATT.signature]}</code></div>
              <div className="md:col-span-2"><strong>Public key:</strong> <code className="break-all">{result.decoded[ATT.publicKey]}</code></div>
              <div className="md:col-span-2"><strong>pHash:</strong> <code className="break-all">{result.decoded[ATT.pHash]}</code></div>
              <div className="md:col-span-2"><strong>fileHash:</strong> <code className="break-all">{result.decoded[ATT.fileHash]}</code></div>
              <div className="md:col-span-2"><strong>metadataCID:</strong> <code className="break-all">{result.decoded[ATT.metadataCID]}</code></div>
              <div className="md:col-span-2"><strong>c2paCertHash:</strong> <code className="break-all">{result.decoded[ATT.c2paCertHash]}</code></div>
              <div className="md:col-span-2"><strong>c2paSig:</strong> <code className="break-all text-xs">{String(result.decoded[ATT.c2paSig]).slice(0, 40)}...</code></div>
              <div className="md:col-span-2"><strong>fileName:</strong> <code className="break-all">{result.decoded[ATT.fileName]}</code></div>
            </div>
          </div>
        </div>
      )}

      {publicKeySearch && (
        <div className="card bg-base-200 shadow-xl">
          <div className="card-body">
            <h2 className="card-title">Images for Public Key</h2>
            {recentRecords.filter(r => String(r.decoded?.[ATT.publicKey]).toLowerCase() === publicKeySearch.toLowerCase()).length === 0 ? (
              <div className="text-sm text-base-content/70">No loaded image records match this public key. Recent records are searched client-side from the currently loaded chain data.</div>
            ) : (
              <div className="overflow-x-auto">
                <table className="table table-sm">
                  <thead>
                    <tr>
                      <th>UID</th>
                      <th>Salt</th>
                      <th>Scheme</th>
                      <th>pHash</th>
                      <th>Attester</th>
                    </tr>
                  </thead>
                  <tbody>
                    {recentRecords
                      .filter(r => String(r.decoded?.[ATT.publicKey]).toLowerCase() === publicKeySearch.toLowerCase())
                      .map((record, idx) => (
                        <tr key={idx}>
                          <td><Link href={`/images/${record.uid}`} className="link link-primary text-xs break-all">{record.uid}</Link></td>
                          <td><code className="text-xs break-all">{record.decoded[ATT.salt]}</code></td>
                          <td>{SCHEME_NAMES[Number(record.decoded[ATT.scheme])] || `Unknown (${record.decoded[ATT.scheme]})`}</td>
                          <td><code className="text-xs break-all">{record.decoded[ATT.pHash]}</code></td>
                          <td><Link href={`/keys?address=${record.att.attester}`} className="link link-primary text-xs break-all">{record.att.attester}</Link></td>
                        </tr>
                      ))}
                  </tbody>
                </table>
              </div>
            )}
          </div>
        </div>
      )}
    </div>
  );
}
