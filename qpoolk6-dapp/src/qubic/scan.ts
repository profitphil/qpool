// Frontend-only convenience scans. QPoolK6 has no "list holdings/proposals
// for this pool" query, so the only way to find them is a linear scan --
// exactly what the Python test suite's own find_proposal_id() does. Capped
// scan ranges, not exhaustive: fine for a demo/testing UI, not for a pool
// with thousands of entries.
import {
  getPool, getHolding, getProposal, getPoolOffers, getAssetAskOrders,
  type Pool, type Holding, type Proposal, type PoolOffer,
} from './decode'

// Concurrency-capped, not Promise.all(everything) -- a free Cloudflare quick
// tunnel (the recommended backend-exposure option for this app) rate-limits
// bursts; confirmed live, 100 simultaneous queries triggered HTTP 429s. Still
// well ahead of doing these one at a time.
export async function mapLimited<T, R>(items: T[], limit: number, fn: (item: T) => Promise<R>): Promise<R[]> {
  const out: R[] = new Array(items.length)
  let next = 0
  async function worker() {
    while (next < items.length) {
      const i = next++
      out[i] = await fn(items[i])
    }
  }
  await Promise.all(Array.from({ length: Math.min(limit, items.length) }, worker))
  return out
}

export const SCAN_CONCURRENCY = 3

export async function scanPools(maxScan = 100): Promise<{ id: number; pool: Pool }[]> {
  const ids = Array.from({ length: maxScan }, (_, i) => i)
  const pools = await mapLimited(ids, SCAN_CONCURRENCY, (id) => getPool(id))
  return ids.map((id, i) => ({ id, pool: pools[i] })).filter((p) => p.pool.found === 1)
}

export async function scanHoldingsForPool(poolId: number, maxScan = 200): Promise<{ id: number; holding: Holding }[]> {
  const ids = Array.from({ length: maxScan }, (_, i) => i)
  const holdings = await mapLimited(ids, SCAN_CONCURRENCY, (id) => getHolding(id))
  return ids
    .map((id, i) => ({ id, holding: holdings[i] }))
    .filter((h) => h.holding.found === 1 && h.holding.poolId === BigInt(poolId))
}

export async function scanProposalsForPool(poolId: number, maxScan = 60): Promise<{ id: number; proposal: Proposal }[]> {
  const ids = Array.from({ length: maxScan }, (_, i) => i)
  const proposals = await mapLimited(ids, SCAN_CONCURRENCY, (id) => getProposal(id))
  return ids
    .map((id, i) => ({ id, proposal: proposals[i] }))
    .filter((p) => p.proposal.found === 1 && p.proposal.poolId === BigInt(poolId))
}

// Same global holding array as scanHoldingsForPool, just unfiltered -- the
// only way to find every token any pool currently holds (deposits are never
// listed anywhere else).
export async function scanAllHoldings(maxScan = 60): Promise<{ id: number; holding: Holding }[]> {
  const ids = Array.from({ length: maxScan }, (_, i) => i)
  const holdings = await mapLimited(ids, SCAN_CONCURRENCY, (id) => getHolding(id))
  return ids.map((id, i) => ({ id, holding: holdings[i] })).filter((h) => h.holding.found === 1)
}

export interface PoolOfferWithPool extends PoolOffer {
  poolId: number
  poolName: string
}
// QPoolK6's seat market, across every pool at once -- GetPoolOffers only
// answers for one pool at a time.
export async function scanAllPoolOffers(pools: { id: number; pool: Pool }[]): Promise<PoolOfferWithPool[]> {
  const offerLists = await mapLimited(pools, SCAN_CONCURRENCY, (p) => getPoolOffers(p.id))
  const out: PoolOfferWithPool[] = []
  pools.forEach((p, i) => {
    offerLists[i].forEach((o) => out.push({ ...o, poolId: p.id, poolName: p.pool.poolNameStr }))
  })
  return out
}

export interface AssetListing {
  issuer: string
  assetName: bigint
  assetNameStr: string
  asks: Awaited<ReturnType<typeof getAssetAskOrders>>
}
// QX's raw order book has no "list every traded asset" query either -- the
// only candidates worth probing are tokens we've already seen in a holding.
export async function scanAssetAsks(
  candidates: { issuer: string; assetName: bigint; assetNameStr: string }[],
): Promise<AssetListing[]> {
  const seen = new Map<string, { issuer: string; assetName: bigint; assetNameStr: string }>()
  for (const c of candidates) seen.set(`${c.issuer}:${c.assetName}`, c)
  const unique = Array.from(seen.values())
  const asks = await mapLimited(unique, SCAN_CONCURRENCY, (c) => getAssetAskOrders(c.issuer, c.assetName))
  return unique.map((c, i) => ({ ...c, asks: asks[i] })).filter((l) => l.asks.length > 0)
}
