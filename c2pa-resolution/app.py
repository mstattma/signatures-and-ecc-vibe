"""
C2PA Soft Binding Resolution API.

Implements the C2PA Soft Binding Resolution API OpenAPI spec:
  - /matches/byBinding   -- Query by watermark value + algorithm -> manifest IDs
  - /matches/byContent   -- Upload image -> extract watermark (deferred)
  - /manifests/{id}      -- Fetch C2PA manifest by attestation UID from IPFS
  - /services/supportedAlgorithms -- List supported algorithms

Backend: Ethereum ledger (ImageAuthResolver) + IPFS (kubo).

Per C2PA spec, the `value` parameter is base64-encoded.
"""

import base64
import json
import os
from pathlib import Path
from typing import Optional

import httpx
from fastapi import FastAPI, HTTPException, Query
from pydantic import BaseModel

from web3 import Web3
from eth_abi import decode

app = FastAPI(
    title="C2PA Soft Binding Resolution API",
    description="Resolves C2PA soft binding queries against the image authentication ledger.",
    version="1.0.0",
)

# --- Configuration ---

IPFS_API_URL = os.environ.get("IPFS_API_URL", "http://localhost:5001")
LEDGER_RPC = os.environ.get("LEDGER_RPC", "http://localhost:8545")
DEPLOYMENT_JSON = os.environ.get("DEPLOYMENT_JSON", "/shared/deployment.json")

STARDUST_ALG_NAME = "castlabs.stardust"

# Updated attestation schema types (with c2paCertHash + c2paSig)
ATTESTATION_TYPES = [
    "bytes16", "bytes", "uint8", "bytes", "bytes24",
    "uint16", "bytes2", "bytes32", "bytes32", "bytes32", "bytes", "string",
]

RESOLVER_ABI = [
    {"name": "sigPrefixIndex", "type": "function", "stateMutability": "view",
     "inputs": [{"name": "", "type": "bytes16"}],
     "outputs": [{"name": "", "type": "bytes32"}]},
]

EAS_GET_ATTESTATION_ABI = [{
    "name": "getAttestation", "type": "function", "stateMutability": "view",
    "inputs": [{"name": "uid", "type": "bytes32"}],
    "outputs": [{"name": "", "type": "tuple", "components": [
        {"name": "uid", "type": "bytes32"},
        {"name": "schema", "type": "bytes32"},
        {"name": "time", "type": "uint64"},
        {"name": "expirationTime", "type": "uint64"},
        {"name": "revocationTime", "type": "uint64"},
        {"name": "refUID", "type": "bytes32"},
        {"name": "recipient", "type": "address"},
        {"name": "attester", "type": "address"},
        {"name": "revocable", "type": "bool"},
        {"name": "data", "type": "bytes"},
    ]}],
}]

# --- State ---

_w3: Optional[Web3] = None
_resolver = None
_eas = None
_deployment: Optional[dict] = None


def _load_deployment() -> dict:
    global _deployment
    if _deployment is None:
        p = Path(DEPLOYMENT_JSON)
        if p.exists():
            _deployment = json.loads(p.read_text())
        else:
            _deployment = {}
    return _deployment


def _get_w3() -> Web3:
    global _w3
    if _w3 is None:
        _w3 = Web3(Web3.HTTPProvider(LEDGER_RPC))
    return _w3


def _get_resolver():
    global _resolver
    if _resolver is None:
        d = _load_deployment()
        addr = d.get("resolver", "")
        if addr:
            _resolver = _get_w3().eth.contract(
                address=Web3.to_checksum_address(addr), abi=RESOLVER_ABI)
    return _resolver


def _get_eas():
    global _eas
    if _eas is None:
        d = _load_deployment()
        addr = d.get("eas", "")
        if addr:
            _eas = _get_w3().eth.contract(
                address=Web3.to_checksum_address(addr), abi=EAS_GET_ATTESTATION_ABI)
    return _eas


def _ipfs_fetch(cid: str, path: str = "") -> Optional[bytes]:
    try:
        arg = f"{cid}/{path}" if path else cid
        r = httpx.post(f"{IPFS_API_URL}/api/v0/cat", params={"arg": arg}, timeout=30)
        r.raise_for_status()
        return r.content
    except Exception:
        return None


def _bytes32_to_cid(b: bytes) -> Optional[str]:
    if b == b"\x00" * 32:
        return None
    try:
        import base58
        multihash = bytes([0x12, 0x20]) + b
        return base58.b58encode(multihash).decode()
    except Exception:
        return None


def _lookup_by_wm_id(wm_id_bytes: bytes) -> Optional[dict]:
    """Look up attestation by WM-ID payload (salt || sig)."""
    resolver = _get_resolver()
    eas = _get_eas()
    if not resolver or not eas:
        return None

    if len(wm_id_bytes) < 18:
        return None
    sig_prefix = wm_id_bytes[2:18]

    zero = b"\x00" * 32
    uid = resolver.functions.sigPrefixIndex(sig_prefix).call()
    if uid == zero:
        return None

    att = eas.functions.getAttestation(uid).call()
    decoded = decode(ATTESTATION_TYPES, att[9])

    metadata_cid_bytes = decoded[8]
    cid = _bytes32_to_cid(metadata_cid_bytes)

    return {
        "uid": "0x" + uid.hex(),
        "attester": att[7],
        "time": att[2],
        "sig_prefix": decoded[0].hex(),
        "metadata_cid": cid,
        "phash": decoded[4].hex(),
        "file_name": decoded[11],
    }


# --- Models (per C2PA OpenAPI spec) ---

class MatchResult(BaseModel):
    manifestId: str
    endpoint: Optional[str] = None
    similarityScore: Optional[int] = 100


class MatchesResponse(BaseModel):
    matches: list[MatchResult]


class SoftBindingAlgList(BaseModel):
    watermarks: list[dict]
    fingerprints: list[dict]


class BindingQuery(BaseModel):
    alg: str
    value: str  # base64-encoded


# --- Endpoints ---

@app.get("/services/supportedAlgorithms")
def supported_algorithms() -> SoftBindingAlgList:
    return SoftBindingAlgList(
        watermarks=[{"alg": STARDUST_ALG_NAME}],
        fingerprints=[],
    )


@app.get("/matches/byBinding")
def matches_by_binding_get(
    alg: str = Query(..., description="Algorithm name"),
    value: str = Query(..., description="Base64-encoded binding value (WM-ID)"),
    maxResults: int = Query(10, ge=1),
) -> MatchesResponse:
    return _resolve_binding(alg, value, maxResults)


@app.post("/matches/byBinding")
def matches_by_binding_post(query: BindingQuery) -> MatchesResponse:
    return _resolve_binding(query.alg, query.value, 10)


def _resolve_binding(alg: str, value_b64: str, max_results: int) -> MatchesResponse:
    if alg != STARDUST_ALG_NAME:
        raise HTTPException(400, f"Unsupported algorithm: {alg}")

    try:
        wm_id = base64.b64decode(value_b64)
    except Exception:
        raise HTTPException(400, "Invalid base64 value")

    info = _lookup_by_wm_id(wm_id)
    if not info:
        return MatchesResponse(matches=[])

    return MatchesResponse(matches=[
        MatchResult(
            manifestId=info["uid"],
            similarityScore=100,
        )
    ])


@app.get("/manifests/{manifest_id}")
def get_manifest(
    manifest_id: str,
    returnActiveManifest: bool = Query(False),
):
    """Fetch C2PA manifest by attestation UID from IPFS."""
    eas = _get_eas()
    if not eas:
        raise HTTPException(503, "Ledger not available")

    try:
        uid = bytes.fromhex(manifest_id.replace("0x", ""))
    except ValueError:
        raise HTTPException(400, "Invalid manifest ID")

    zero = b"\x00" * 32
    att = eas.functions.getAttestation(uid).call()
    if att[0] == zero:
        raise HTTPException(404, "Attestation not found")

    decoded = decode(ATTESTATION_TYPES, att[9])
    cid = _bytes32_to_cid(decoded[8])
    if not cid:
        raise HTTPException(404, "No C2PA manifest CID stored for this attestation")

    data = _ipfs_fetch(cid, "manifest.c2pa")
    if not data:
        raise HTTPException(502, f"Failed to fetch manifest from IPFS (CID: {cid})")

    from fastapi.responses import Response
    return Response(content=data, media_type="application/c2pa")


@app.post("/matches/byContent")
async def matches_by_content():
    """Upload image, extract watermark, query. (Not yet implemented.)"""
    raise HTTPException(501, "byContent requires server-side Stardust extraction (not yet implemented)")


@app.get("/health")
def health():
    w3 = _get_w3()
    return {
        "ledger": w3.is_connected() if w3 else False,
        "deployment": bool(_load_deployment()),
    }
