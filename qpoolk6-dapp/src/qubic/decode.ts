// Response decoders -- mirrors tools-bin/qpoolk6_lib.py's get_* functions
// byte-for-byte. Identity fields decode async (checksum derivation), so
// every decoder here is async too.
import { querySmartContract } from './rpc'
import { ru32, ru64, rid, u64ToName } from './codec'
import {
  QPOOLK6, F_GET_POOL, F_GET_MEMBER, F_GET_HOLDING, F_GET_POSITION, F_GET_PROPOSAL,
  F_GET_STATS, F_GET_SOURCE, F_GET_IPO_TARGET, F_GET_IPO_BIDS, F_GET_POOL_OFFERS,
  MAX_OFFERS_PER_POOL,
} from './config'
import {
  payGetPool, payGetMember, payGetHolding, payGetPosition, payGetProposal,
  payGetSource, payGetIpoTarget, payGetIpoBids, payGetPoolOffers,
} from './encode'

export interface Pool {
  found: number
  creator: string
  poolName: bigint
  poolNameStr: string
  uncommittedTotal: bigint
  committedTotal: bigint
  contributorCount: bigint
  holdingCount: bigint
  unattributedQu: bigint
  permissioned: bigint
  offerCount: bigint
  admittedCount: bigint
  ownershipRows: bigint
  depositCount: bigint
  ipoHoldings: bigint
  admin0: string
  admin1: string
  admin2: string
}

export async function getPool(poolId: number): Promise<Pool> {
  const raw = await querySmartContract(QPOOLK6, F_GET_POOL, payGetPool(poolId))
  const poolName = ru64(raw, 40)
  const [creator, admin0, admin1, admin2] = await Promise.all([
    rid(raw, 8), rid(raw, 136), rid(raw, 168), rid(raw, 200),
  ])
  return {
    found: ru32(raw, 0), creator, poolName, poolNameStr: u64ToName(poolName),
    uncommittedTotal: ru64(raw, 48), committedTotal: ru64(raw, 56),
    contributorCount: ru64(raw, 64), holdingCount: ru64(raw, 72),
    unattributedQu: ru64(raw, 80), permissioned: ru64(raw, 88),
    offerCount: ru64(raw, 96), admittedCount: ru64(raw, 104),
    ownershipRows: ru64(raw, 112), depositCount: ru64(raw, 120), ipoHoldings: ru64(raw, 128),
    admin0, admin1, admin2,
  }
}

export interface Member {
  found: number
  uncommitted: bigint
  committed: bigint
  locked: bigint
}
export async function getMember(poolId: number, memberId: string): Promise<Member> {
  const raw = await querySmartContract(QPOOLK6, F_GET_MEMBER, payGetMember(poolId, memberId))
  return { found: ru32(raw, 0), uncommitted: ru64(raw, 8), committed: ru64(raw, 16), locked: ru64(raw, 24) }
}

export interface Holding {
  found: number
  poolId: bigint
  issuer: string
  assetName: bigint
  assetNameStr: string
  shares: bigint
  costBasis: bigint
  totalPayouts: bigint
  pendingShares: bigint
  ipoContractIndex: bigint
  active: bigint
  ipoMaxPrice: bigint
  ipoAtRisk: bigint
  ipoAvailable: bigint
  deposited: bigint
  ipoSettled: bigint
  sourceEscrow: bigint
  payerIndex: bigint
  rows: bigint
  qxBidPrice: bigint
  qxBidTick: bigint
  askShares: bigint
  askPrice: bigint
  askTick: bigint
}

export async function getHolding(holdingId: number): Promise<Holding> {
  const raw = await querySmartContract(QPOOLK6, F_GET_HOLDING, payGetHolding(holdingId))
  const assetName = ru64(raw, 48)
  const issuer = await rid(raw, 16)
  return {
    found: ru32(raw, 0), poolId: ru64(raw, 8), issuer, assetName, assetNameStr: u64ToName(assetName),
    shares: ru64(raw, 56), costBasis: ru64(raw, 64), totalPayouts: ru64(raw, 72),
    pendingShares: ru64(raw, 80), ipoContractIndex: ru64(raw, 88), active: ru64(raw, 96),
    ipoMaxPrice: ru64(raw, 104), ipoAtRisk: ru64(raw, 112), ipoAvailable: ru64(raw, 120),
    deposited: ru64(raw, 128), ipoSettled: ru64(raw, 136), sourceEscrow: ru64(raw, 144),
    payerIndex: ru64(raw, 152), rows: ru64(raw, 160),
    qxBidPrice: ru64(raw, 168), qxBidTick: ru64(raw, 176),
    askShares: ru64(raw, 184), askPrice: ru64(raw, 192), askTick: ru64(raw, 200),
  }
}

export interface Position {
  found: number
  cost: bigint
  costBasis: bigint
  owed: bigint
  offeredCostUnits: bigint
  askPrice: bigint
  offeredShares: bigint
  voteLockUntilTick: bigint
  holdingAskShares: bigint
}
export async function getPosition(holdingId: number, memberId: string): Promise<Position> {
  const raw = await querySmartContract(QPOOLK6, F_GET_POSITION, payGetPosition(holdingId, memberId))
  return {
    found: ru32(raw, 0), cost: ru64(raw, 8), costBasis: ru64(raw, 16), owed: ru64(raw, 24),
    offeredCostUnits: ru64(raw, 32), askPrice: ru64(raw, 40), offeredShares: ru64(raw, 48),
    voteLockUntilTick: ru64(raw, 56), holdingAskShares: ru64(raw, 64),
  }
}

export interface Proposal {
  found: number
  poolId: bigint
  kind: bigint
  proposer: string
  issuer: string
  assetName: bigint
  assetNameStr: string
  quAmount: bigint
  shares: bigint
  price: bigint
  holdingId: bigint
  yesWeight: bigint
  noWeight: bigint
  eligibleWeight: bigint
  openTick: bigint
  status: bigint
}
export async function getProposal(proposalId: number): Promise<Proposal> {
  const raw = await querySmartContract(QPOOLK6, F_GET_PROPOSAL, payGetProposal(proposalId))
  const assetName = ru64(raw, 88)
  const [proposer, issuer] = await Promise.all([rid(raw, 24), rid(raw, 56)])
  return {
    found: ru32(raw, 0), poolId: ru64(raw, 8), kind: ru64(raw, 16), proposer, issuer,
    assetName, assetNameStr: u64ToName(assetName),
    quAmount: ru64(raw, 96), shares: ru64(raw, 104), price: ru64(raw, 112),
    holdingId: ru64(raw, 120), yesWeight: ru64(raw, 128), noWeight: ru64(raw, 136),
    eligibleWeight: ru64(raw, 144), openTick: ru64(raw, 152), status: ru64(raw, 160),
  }
}

const STATS_FIELDS = [
  'poolCount', 'holdingCount', 'proposalCount', 'totalContributed', 'totalDeployed',
  'totalPayoutsDistributed', 'globalUnattributed', 'ipoRefundEscrow', 'totalIpoRefunded',
  'pendingRevenue', 'totalRevenue', 'totalManagementFees', 'totalDistributedToShareholders',
  'totalBurned', 'dividendEscrowTotal', 'totalUnownedSwept', 'qpayBuyPot', 'qpayUnburned',
  'totalQpayhubDeposited', 'totalQpaySpent', 'totalQpayBurned',
  'readyUnsettled', 'autoSettleEpoch', 'autoSettleDone', 'autoSettleSkipped', 'autoSettleTotal',
  'restingAsks', 'restingBids',
  'totalQtreatDeposited', 'totalQdogeSpent', 'totalQdogeBought', 'totalQdogeBurned',
] as const
export type Stats = Record<(typeof STATS_FIELDS)[number], bigint>

export async function getStats(): Promise<Stats> {
  const raw = await querySmartContract(QPOOLK6, F_GET_STATS)
  const out = {} as Stats
  STATS_FIELDS.forEach((name, i) => { out[name] = ru64(raw, 8 * i) })
  return out
}

export interface SourceLedger {
  found: number
  assetName: bigint
  holdings: bigint
  recognized: bigint
  unsettled: bigint
  escrow: bigint
  pendingQx: bigint
  accHigh: bigint
  accLow: bigint
  qxCount: bigint
  askCount: bigint
  askTotal: bigint
  unresolved: bigint
}
export async function getSource(sourceId: string, kind: number): Promise<SourceLedger> {
  const raw = await querySmartContract(QPOOLK6, F_GET_SOURCE, payGetSource(sourceId, kind))
  return {
    found: ru32(raw, 0), assetName: ru64(raw, 8), holdings: ru64(raw, 16),
    recognized: ru64(raw, 24), unsettled: ru64(raw, 32), escrow: ru64(raw, 40),
    pendingQx: ru64(raw, 48), accHigh: ru64(raw, 56), accLow: ru64(raw, 64),
    qxCount: ru64(raw, 72), askCount: ru64(raw, 80), askTotal: ru64(raw, 88), unresolved: ru64(raw, 96),
  }
}

export interface IpoTarget {
  found: number
  unsettled: bigint
  bidEpoch: bigint
  snapshotEpoch: bigint
  finalPrice: bigint
  seats: bigint
  spent: bigint
}
export async function getIpoTarget(ipoContractIndex: number, price: bigint | number = 0): Promise<IpoTarget> {
  const raw = await querySmartContract(QPOOLK6, F_GET_IPO_TARGET, payGetIpoTarget(ipoContractIndex, price))
  return {
    found: ru32(raw, 0), unsettled: ru64(raw, 8), bidEpoch: ru64(raw, 16),
    snapshotEpoch: ru64(raw, 24), finalPrice: ru64(raw, 32), seats: ru64(raw, 40), spent: ru64(raw, 48),
  }
}

export interface IpoBids {
  found: number
  ipoContractIndex: bigint
  budget: bigint
  maxPrice: bigint
  seats: bigint
  freeFunds: bigint
  atRisk: bigint
  refundPot: bigint
  count: number
  prices: bigint[]
  seatsAt: bigint[]
}
export async function getIpoBids(holdingId: number): Promise<IpoBids> {
  const raw = await querySmartContract(QPOOLK6, F_GET_IPO_BIDS, payGetIpoBids(holdingId))
  const count = Number(ru64(raw, 64))
  const prices = Array.from({ length: 16 }, (_, i) => ru64(raw, 72 + 8 * i))
  const seatsAt = Array.from({ length: 16 }, (_, i) => ru64(raw, 200 + 8 * i))
  return {
    found: ru32(raw, 0), ipoContractIndex: ru64(raw, 8), budget: ru64(raw, 16),
    maxPrice: ru64(raw, 24), seats: ru64(raw, 32), freeFunds: ru64(raw, 40),
    atRisk: ru64(raw, 48), refundPot: ru64(raw, 56), count,
    prices: prices.slice(0, count), seatsAt: seatsAt.slice(0, count),
  }
}

export interface PoolOffer {
  seller: string
  holdingId: bigint
  costUnits: bigint
  askPrice: bigint
  shares: bigint
}
export async function getPoolOffers(poolId: number): Promise<PoolOffer[]> {
  const raw = await querySmartContract(QPOOLK6, F_GET_POOL_OFFERS, payGetPoolOffers(poolId))
  const count = Number(ru64(raw, 0))
  const baseSeller = 8
  const baseHolding = baseSeller + 32 * MAX_OFFERS_PER_POOL
  const baseCost = baseHolding + 8 * MAX_OFFERS_PER_POOL
  const baseAsk = baseCost + 8 * MAX_OFFERS_PER_POOL
  const baseShares = baseAsk + 8 * MAX_OFFERS_PER_POOL
  const out: PoolOffer[] = []
  for (let i = 0; i < count; i++) {
    out.push({
      seller: await rid(raw, baseSeller + 32 * i),
      holdingId: ru64(raw, baseHolding + 8 * i),
      costUnits: ru64(raw, baseCost + 8 * i),
      askPrice: ru64(raw, baseAsk + 8 * i),
      shares: ru64(raw, baseShares + 8 * i),
    })
  }
  return out
}

// Propose's slot is picked by a free/expired-slot scan, not creation order --
// never assume proposalId follows any sequence. Scan for the matching slot.
export async function findProposalId(
  poolId: number, proposerId: string, kind: number, assetName: bigint | null = null, maxScan = 60,
): Promise<{ id: number; proposal: Proposal } | null> {
  for (let i = 0; i < maxScan; i++) {
    const p = await getProposal(i)
    if (
      p.found === 1 && p.poolId === BigInt(poolId) && p.proposer === proposerId &&
      p.kind === BigInt(kind) && (assetName === null || p.assetName === assetName)
    ) {
      return { id: i, proposal: p }
    }
  }
  return null
}
