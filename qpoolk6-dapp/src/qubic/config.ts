// Mirrors tools-bin/qpoolk6_lib.py's constant block exactly. Keep these two
// files in sync by hand -- there is no shared source of truth between the
// Python test suite and this frontend.

export const QPOOLK6 = 49
export const QPAYHUB = 29
export const QTREAT = 30
export const K6TGT = 50
export const K6TGT2 = 51
export const QX = 1
export const QSWAP = 13

export const CONTRACT_NAMES: Record<number, string> = {
  [QPOOLK6]: 'QPOOLK6', [QPAYHUB]: 'QPAYHUB', [QTREAT]: 'QTREAT',
  [K6TGT]: 'K6TGT', [K6TGT2]: 'K6TGT2', [QX]: 'QX', [QSWAP]: 'QSWAP',
}

// ---- procedures ----
export const P_CREATE_POOL = 1
export const P_CONTRIBUTE = 2
export const P_WITHDRAW_UNCOMMITTED = 3
export const P_PROPOSE = 4
export const P_VOTE = 5
export const P_TALLY = 6
export const P_EXECUTE = 7
export const P_RECONCILE_HOLDING = 8
export const P_CLAIM_PAYOUT = 9
export const P_OFFER_POSITION = 10
export const P_CANCEL_OFFER = 11
export const P_BUY_POSITION = 12
export const P_REBID_IPO = 13
export const P_SETTLE_IPO = 14
export const P_DEPOSIT_HOLDING_ASSET = 16
export const P_SET_ADMISSION = 17
export const P_SET_PERMISSIONED = 18
export const P_RELEASE_MANAGED_SHARES = 19
export const P_CLOSE_EMPTY_POOL = 20
export const P_EVICT_SQUAT = 21
export const P_BUY_AND_BURN_QPAY = 22
export const P_CANCEL_QX_BID = 23
export const P_SET_POOL_ADMINS = 24
export const P_CANCEL_QX_ASK = 25
export const P_REGISTER_DIVIDEND_PAYER = 26

// ---- functions ----
export const F_GET_POOL = 1
export const F_GET_MEMBER = 2
export const F_GET_HOLDING = 3
export const F_GET_POSITION = 4
export const F_GET_PROPOSAL = 5
export const F_GET_STATS = 6
export const F_GET_SOURCE = 7
export const F_GET_IPO_TARGET = 8
export const F_GET_IPO_BIDS = 9
export const F_GET_NAME_REGISTRY = 10
export const F_GET_POOL_OFFERS = 11

// ---- QX ----
export const QX_P_ISSUE_ASSET = 1
export const QX_P_TRANSFER_OWNERSHIP = 2
export const QX_P_ADD_TO_ASK_ORDER = 5
export const QX_P_ADD_TO_BID_ORDER = 6
export const QX_P_REMOVE_FROM_ASK_ORDER = 7
export const QX_P_REMOVE_FROM_BID_ORDER = 8
export const QX_P_TRANSFER_SHARE_MGMT_RIGHTS = 9

// ---- proposal kinds / status ----
export const KIND_BUY_QSWAP = 1
export const KIND_BUY_QX = 2
export const KIND_BUY_IPO = 3
export const KIND_SELL_QSWAP = 4
export const KIND_SELL_QX = 5
export const KIND_ALLOCATE = 6
export const KIND_SELL_QX_LIMIT = 7
export const PROP_STATUS = ['OPEN', 'PASSED', 'REJECTED', 'EXECUTED', 'EXPIRED', 'FAILED'] as const
// Index 0 unused (proposal kinds are 1-7); plain-language names for display.
export const KIND_NAMES = [
  '',
  'Buy a token (QSWAP)',
  'Buy a token or shares (QX)',
  'Bid in an IPO',
  'Sell a holding (QSWAP)',
  'Sell a holding now (QX)',
  'Allocate funds to a holding',
  'List a holding for sale (QX)',
] as const

// ---- return codes ----
export const ERR_NAMES: Record<number, string> = {
  0: 'OK', 1: 'INVALID_INPUT', 2: 'CAPACITY', 3: 'NOT_FOUND', 4: 'NOT_MEMBER',
  5: 'INSUFFICIENT_FEE', 6: 'INSUFFICIENT_BALANCE', 7: 'WRONG_STATUS',
  8: 'WINDOW_OPEN', 9: 'WINDOW_CLOSED', 10: 'ALREADY_VOTED', 11: 'EXTERNAL_CALL',
  12: 'TRANSFER_FAILED', 13: 'NOTHING_OWED', 14: 'PRICE_TOO_HIGH', 15: 'NO_OFFER',
  16: 'IPO_REJECTED', 17: 'IPO_PRICE_TAKEN', 18: 'IPO_NOT_ACTIVE', 19: 'ABOVE_CEILING',
  20: 'PROPOSAL_PENDING', 21: 'NOT_ADMITTED', 22: 'NOT_AUTHORIZED', 23: 'SOURCE_CONFLICT',
  24: 'SETTLEMENT_PENDING', 25: 'QX_BID_PENDING', 26: 'DISABLED', 27: 'QX_ONLY',
  28: 'CONTRACT_MISMATCH', 29: 'CONTRACT_NOT_LIVE', 30: 'NO_FILL', 31: 'BOOKS_INCONSISTENT',
  32: 'SEAT_LOCKED', 33: 'DEPOSIT_AFTER_OPEN', 34: 'NAME_UNPROVEN', 35: 'IPO_NO_SNAPSHOT',
  36: 'PRICE_TOO_LOW', 37: 'SEATS_FULL', 38: 'OFFER_CHANGED', 39: 'NOTHING_TO_BUY',
  40: 'NO_PENDING_BID', 41: 'NAME_PENDING', 42: 'QX_SIDE_BUSY', 43: 'NO_PENDING_ASK',
  44: 'SELF_CROSS', 45: 'POOL_SLOTS',
}
export function errName(code: number): string {
  return ERR_NAMES[code] ?? `UNKNOWN(${code})`
}

// ---- fee constants ----
export const CREATE_POOL_FEE = 25_000_000
export const CREATE_POOL_FEE_QPAY = 10_000
export const JOIN_FEE = 25_000
export const MIN_CONTRIBUTION = 10_000
export const MANAGEMENT_PERMILLE = 10
export const MAX_OFFERS_PER_POOL = 128

// Empirically measured on this devkit's accelerated test chain (2 full
// epochs spanned 2,802 ticks) -- an estimate for display only, the real
// pace drifts a bit tick to tick.
export const EPOCH_TICKS_ESTIMATE = 1400
export const TICKS_PER_SECOND_ESTIMATE = 2

export const TICKS_PER_HOUR = 2
export const VOTE_WINDOW = 72 * TICKS_PER_HOUR
export const EXECUTE_WINDOW = 48 * TICKS_PER_HOUR
export const PROPOSAL_LIFETIME = VOTE_WINDOW + EXECUTE_WINDOW
export const IPO_VOTE_WINDOW = 24 * TICKS_PER_HOUR
export const IPO_PROPOSAL_LIFETIME = IPO_VOTE_WINDOW + EXECUTE_WINDOW

export const NULL_ID = 'AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAFXIB'

// Tick submitted with a transaction must be in the future -- how far ahead to
// target, matching the margin test scripts use (send()'s default offset=25).
export const TICK_OFFSET = 25
