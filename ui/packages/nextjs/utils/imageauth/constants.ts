/**
 * Shared constants for the image authentication UI.
 */

export const SCHEME_NAMES: Record<number, string> = {
  0: "UOV-80",
  1: "UOV-100",
  2: "BLS-BN158",
  3: "BLS12-381",
};

/**
 * Attestation schema types — must match ImageAuthResolver's onAttest decoding.
 * 12 fields: sigPrefix, signature, scheme, publicKey, pHash, pHashVersion, salt,
 *            fileHash, metadataCID, c2paCertHash, c2paSig, fileName
 */
export const IMAGE_ATTESTATION_TYPES = [
  { type: "bytes16", name: "sigPrefix" },
  { type: "bytes", name: "signature" },
  { type: "uint8", name: "scheme" },
  { type: "bytes", name: "publicKey" },
  { type: "bytes24", name: "pHash" },
  { type: "uint16", name: "pHashVersion" },
  { type: "bytes2", name: "salt" },
  { type: "bytes32", name: "fileHash" },
  { type: "bytes32", name: "metadataCID" },
  { type: "bytes32", name: "c2paCertHash" },
  { type: "bytes", name: "c2paSig" },
  { type: "string", name: "fileName" },
] as const;

/** Index mapping for decoded attestation data array */
export const ATT = {
  sigPrefix: 0,
  signature: 1,
  scheme: 2,
  publicKey: 3,
  pHash: 4,
  pHashVersion: 5,
  salt: 6,
  fileHash: 7,
  metadataCID: 8,
  c2paCertHash: 9,
  c2paSig: 10,
  fileName: 11,
} as const;
