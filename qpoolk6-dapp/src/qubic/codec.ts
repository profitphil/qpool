// Low-level byte helpers mirroring qpoolk6_lib.py's u64/ru64/rid/name_u64 etc.
// Every QPoolK6 payload is little-endian, fixed-width fields -- no varints,
// no padding beyond what each struct already declares.
import { QubicHelper } from './lib'

const qHelper = new QubicHelper()

export function u64(n: bigint | number): Uint8Array {
  const out = new Uint8Array(8)
  new DataView(out.buffer).setBigUint64(0, BigInt(n), true)
  return out
}

export function u32(n: number): Uint8Array {
  const out = new Uint8Array(4)
  new DataView(out.buffer).setUint32(0, n, true)
  return out
}

export function i64(n: bigint | number): Uint8Array {
  const out = new Uint8Array(8)
  new DataView(out.buffer).setBigInt64(0, BigInt(n), true)
  return out
}

// identity string (60 uppercase chars) -> raw 32-byte public key. Synchronous
// -- getIdentityBytes does the checksum-stripping decode directly, no async
// derivation involved (that's only needed going the OTHER way).
export function identityToBytes(identity: string): Uint8Array {
  if (identity === '' || identity == null) return new Uint8Array(32)
  return qHelper.getIdentityBytes(identity)
}

// raw 32-byte public key -> identity string. Async: derives the checksum.
export async function bytesToIdentity(bytes: Uint8Array): Promise<string> {
  if (bytes.every((b) => b === 0)) return 'AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAFXIB'
  return await qHelper.getIdentity(bytes)
}

// A contract index as a "destination": low 8 bytes = index (little-endian),
// rest zero -- matches qtx.cpp's `memcpy(destPublicKey, &idx, 8)` exactly.
export function contractIndexToBytes(index: number): Uint8Array {
  const out = new Uint8Array(32)
  new DataView(out.buffer).setBigUint64(0, BigInt(index), true)
  return out
}

// Qubic asset/pool names: up to 8 ASCII chars, packed little-endian into one
// u64, short names zero-padded. Matches name_u64() exactly.
export function nameToU64(label: string): bigint {
  if (label.length < 1 || label.length > 8) {
    throw new Error(`name must be 1-8 chars, got "${label}" (${label.length})`)
  }
  const bytes = new Uint8Array(8)
  for (let i = 0; i < label.length; i++) bytes[i] = label.charCodeAt(i)
  return new DataView(bytes.buffer).getBigUint64(0, true)
}

export function u64ToName(n: bigint): string {
  const bytes = new Uint8Array(8)
  new DataView(bytes.buffer).setBigUint64(0, n, true)
  let s = ''
  for (const b of bytes) {
    if (b === 0) break
    s += String.fromCharCode(b)
  }
  return s
}

export function concatBytes(...chunks: Uint8Array[]): Uint8Array {
  const total = chunks.reduce((n, c) => n + c.length, 0)
  const out = new Uint8Array(total)
  let offset = 0
  for (const c of chunks) {
    out.set(c, offset)
    offset += c.length
  }
  return out
}

// ---- reading a query response buffer ----
export function ru32(buf: Uint8Array, offset: number): number {
  return new DataView(buf.buffer, buf.byteOffset, buf.byteLength).getUint32(offset, true)
}
export function ru64(buf: Uint8Array, offset: number): bigint {
  return new DataView(buf.buffer, buf.byteOffset, buf.byteLength).getBigUint64(offset, true)
}
export function ri64(buf: Uint8Array, offset: number): bigint {
  return new DataView(buf.buffer, buf.byteOffset, buf.byteLength).getBigInt64(offset, true)
}
export async function rid(buf: Uint8Array, offset: number): Promise<string> {
  return await bytesToIdentity(buf.slice(offset, offset + 32))
}

export function hexToBytes(hex: string): Uint8Array {
  const out = new Uint8Array(hex.length / 2)
  for (let i = 0; i < out.length; i++) out[i] = parseInt(hex.slice(i * 2, i * 2 + 2), 16)
  return out
}

export function bytesToBase64(bytes: Uint8Array): string {
  let binary = ''
  for (const b of bytes) binary += String.fromCharCode(b)
  return btoa(binary)
}

export function base64ToBytes(b64: string): Uint8Array {
  const binary = atob(b64)
  const out = new Uint8Array(binary.length)
  for (let i = 0; i < binary.length; i++) out[i] = binary.charCodeAt(i)
  return out
}
