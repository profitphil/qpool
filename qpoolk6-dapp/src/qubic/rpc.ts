// Talks to core-lite's RPC through a same-origin proxy -- core-lite itself
// sends no CORS headers (confirmed directly: curl with an Origin header gets
// back a response with no Access-Control-* at all), so a browser can't call
// it cross-origin no matter how the tunnel/domain is set up. In dev, Vite's
// own server proxy (vite.config.ts) forwards /api/rpc/* to the backend. In
// production, a Netlify Function (netlify/functions/rpc-proxy) does the same
// job, reading the current backend URL from an env var the site owner can
// update without touching any frontend code.
import { bytesToBase64, base64ToBytes } from './codec'

const RPC_BASE = '/api/rpc'

// A free Cloudflare quick tunnel (the default backend-exposure path for this
// app) rate-limits request bursts with plain HTTP 429s -- confirmed live.
// Production has many visitors sharing the same tunnel, not just one
// browser's own scan concurrency, so retrying with backoff belongs here,
// not just in whatever scan happened to trigger it first.
async function rpcFetch(path: string, init?: RequestInit, attempt = 0): Promise<any> {
  const res = await fetch(`${RPC_BASE}${path}`, init)
  if (res.status === 429 && attempt < 5) {
    await new Promise((r) => setTimeout(r, 300 * 2 ** attempt))
    return rpcFetch(path, init, attempt + 1)
  }
  if (!res.ok) {
    const body = await res.text().catch(() => '')
    throw new Error(`RPC ${path} -> HTTP ${res.status}${body ? `: ${body}` : ''}`)
  }
  return res.json()
}

export interface TickInfo {
  tick: number
  epoch: number
}

export async function getTick(): Promise<TickInfo> {
  const json = await rpcFetch('/query/v1/getLastProcessedTick')
  return { tick: json.tickNumber, epoch: json.epoch }
}

export async function getBalance(identity: string): Promise<bigint> {
  const json = await rpcFetch(`/live/v1/balances/${identity}`)
  return BigInt(json.balance?.balance ?? 0)
}

export interface OwnedAsset {
  name: string
  managingContractIndex: number
  numberOfUnits: bigint
  issuerIdentity: string
}

export async function getOwnedAssets(identity: string): Promise<OwnedAsset[]> {
  const json = await rpcFetch(`/live/v1/assets/${identity}/owned`)
  const rows = json.ownedAssets ?? []
  return rows.map((r: any) => ({
    name: r.data.issuedAsset.name,
    managingContractIndex: r.data.managingContractIndex,
    numberOfUnits: BigInt(r.data.numberOfUnits ?? 0),
    issuerIdentity: r.data.issuedAsset.issuerIdentity,
  }))
}

export async function querySmartContract(
  contractIndex: number,
  inputType: number,
  payload: Uint8Array = new Uint8Array(0),
): Promise<Uint8Array> {
  const json = await rpcFetch('/live/v1/querySmartContract', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({
      contractIndex,
      inputType,
      inputSize: payload.length,
      requestData: payload.length > 0 ? bytesToBase64(payload) : '',
    }),
  })
  if (!json.responseData) return new Uint8Array(0)
  return base64ToBytes(json.responseData)
}

export interface BroadcastResult {
  peersBroadcasted?: number
  encodedTransaction?: string
  transactionId?: string
  [key: string]: unknown
}

export async function broadcastTransaction(signedBytes: Uint8Array): Promise<BroadcastResult> {
  return rpcFetch('/live/v1/broadcast-transaction', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ encodedTransaction: bytesToBase64(signedBytes) }),
  })
}

export async function waitUntilTick(targetTick: number, pollMs = 2000, timeoutMs = 120_000): Promise<void> {
  const deadline = Date.now() + timeoutMs
  while (Date.now() < deadline) {
    const { tick } = await getTick()
    if (tick >= targetTick) return
    await new Promise((r) => setTimeout(r, pollMs))
  }
  throw new Error(`Timed out waiting for tick ${targetTick}`)
}
