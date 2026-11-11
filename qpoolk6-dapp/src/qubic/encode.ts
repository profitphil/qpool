// Payload encoders -- mirrors tools-bin/qpoolk6_lib.py's pay_* functions
// byte-for-byte. Keep in sync by hand; there is no shared source of truth.
import { u64, i64, u32, concatBytes, identityToBytes, nameToU64 } from './codec'
import { NULL_ID } from './config'

export function payCreatePool(name: string, permissioned = 0): Uint8Array {
  return concatBytes(u64(nameToU64(name)), u64(permissioned))
}
export function payContribute(poolId: number): Uint8Array {
  return u64(poolId)
}
export function payWithdrawUncommitted(poolId: number, amount: bigint | number): Uint8Array {
  return concatBytes(u64(poolId), u64(amount))
}
export function paySetAdmission(poolId: number, memberId: string, allowed: boolean): Uint8Array {
  return concatBytes(u64(poolId), identityToBytes(memberId), u64(allowed ? 1 : 0))
}
export function paySetPermissioned(poolId: number, permissioned: boolean): Uint8Array {
  return concatBytes(u64(poolId), u64(permissioned ? 1 : 0))
}
export function payCloseEmptyPool(poolId: number): Uint8Array {
  return u64(poolId)
}

// K8+ breaking change: for KIND_BUY_IPO, assetName MUST be 0 and issuerId
// MUST be NULL_ID -- the IPO's share name is read from chain events at
// settlement, never declared upfront.
export function payPropose(
  poolId: number,
  kind: number,
  issuerId: string,
  assetName: bigint,
  quAmount: bigint | number,
  shares: bigint | number,
  price: bigint | number,
  holdingId = 0,
): Uint8Array {
  return concatBytes(
    u64(poolId), u64(kind), identityToBytes(issuerId), u64(assetName),
    u64(quAmount), u64(shares), u64(price), u64(holdingId),
  )
}
export function payVote(proposalId: number, approve: boolean): Uint8Array {
  return concatBytes(u64(proposalId), u64(approve ? 1 : 0))
}
export function payTally(proposalId: number): Uint8Array {
  return u64(proposalId)
}
export function payExecute(proposalId: number): Uint8Array {
  return u64(proposalId)
}
export function payReconcileHolding(holdingId: number): Uint8Array {
  return u64(holdingId)
}
export function payClaimPayout(holdingId: number): Uint8Array {
  return u64(holdingId)
}
export function payOfferPosition(holdingId: number, costUnits: bigint | number, askPrice: bigint | number): Uint8Array {
  return concatBytes(u64(holdingId), u64(costUnits), u64(askPrice))
}
export function payCancelOffer(holdingId: number): Uint8Array {
  return u64(holdingId)
}
export function payBuyPosition(
  holdingId: number, sellerId: string, expectedCostUnits: bigint | number, expectedShares: bigint | number,
): Uint8Array {
  return concatBytes(u64(holdingId), identityToBytes(sellerId), u64(expectedCostUnits), u64(expectedShares))
}
export function payRebidIpo(holdingId: number, price: bigint | number, quantity: bigint | number): Uint8Array {
  return concatBytes(u64(holdingId), u64(price), u64(quantity))
}
export function paySettleIpo(holdingId: number): Uint8Array {
  return u64(holdingId)
}
export function payDepositHoldingAsset(
  poolId: number, issuerId: string, assetName: bigint, amount: bigint | number, seatUnits: bigint | number, contractIndex = 0,
): Uint8Array {
  return concatBytes(u64(poolId), identityToBytes(issuerId), u64(assetName), u64(amount), u64(seatUnits), u64(contractIndex))
}
// Issuer-only: declares payerId as the real dividend payer for (issuerId,
// assetName). payerId=NULL_ID clears a prior registration. Must be sent by
// issuerId's own seed.
export function payRegisterDividendPayer(issuerId: string, assetName: bigint, payerId: string = NULL_ID): Uint8Array {
  return concatBytes(identityToBytes(issuerId), u64(assetName), identityToBytes(payerId))
}
export function payReleaseManagedShares(
  issuerId: string, assetName: bigint, amount: bigint | number, destIndex: number,
): Uint8Array {
  return concatBytes(identityToBytes(issuerId), u64(assetName), u64(amount), u64(destIndex))
}
export function payEvictSquat(holdingId: number, rivalName: bigint): Uint8Array {
  return concatBytes(u64(holdingId), u64(rivalName))
}
export function payBuyAndBurn(maxLevels = 0): Uint8Array {
  return u64(maxLevels)
}
export function payCancelQxBid(poolId: number, holdingId: number): Uint8Array {
  return concatBytes(u64(poolId), u64(holdingId))
}
export function payCancelQxAsk(poolId: number, holdingId: number): Uint8Array {
  return concatBytes(u64(poolId), u64(holdingId))
}
export function paySetPoolAdmins(poolId: number, admin0 = NULL_ID, admin1 = NULL_ID, admin2 = NULL_ID): Uint8Array {
  return concatBytes(u64(poolId), identityToBytes(admin0), identityToBytes(admin1), identityToBytes(admin2))
}

// ---- queries ----
export function payGetPool(poolId: number): Uint8Array {
  return u64(poolId)
}
export function payGetPoolOffers(poolId: number): Uint8Array {
  return u64(poolId)
}
export function payGetMember(poolId: number, memberId: string): Uint8Array {
  return concatBytes(u64(poolId), identityToBytes(memberId))
}
export function payGetHolding(holdingId: number): Uint8Array {
  return u64(holdingId)
}
export function payGetPosition(holdingId: number, memberId: string): Uint8Array {
  return concatBytes(u64(holdingId), identityToBytes(memberId))
}
export function payGetProposal(proposalId: number): Uint8Array {
  return u64(proposalId)
}
export function payGetSource(sourceId: string, kind: number): Uint8Array {
  return concatBytes(identityToBytes(sourceId), u64(kind))
}
export function payGetIpoTarget(ipoContractIndex: number, price: bigint | number = 0): Uint8Array {
  return concatBytes(u64(ipoContractIndex), u64(price))
}
export function payGetIpoBids(holdingId: number): Uint8Array {
  return u64(holdingId)
}

// ---- QX ----
export function payQxIssueAsset(assetName: bigint, shares: bigint | number, unit = 0, decimals = 0): Uint8Array {
  const dec = new Uint8Array(1)
  dec[0] = decimals & 0xff
  return concatBytes(u64(assetName), i64(shares), u32(unit), dec)
}
export function payQxTransferOwnership(issuerId: string, newOwnerId: string, assetName: bigint, shares: bigint | number): Uint8Array {
  return concatBytes(identityToBytes(issuerId), identityToBytes(newOwnerId), u64(assetName), i64(shares))
}
export function payQxAddToAskOrder(issuerId: string, assetName: bigint, price: bigint | number, shares: bigint | number): Uint8Array {
  return concatBytes(identityToBytes(issuerId), u64(assetName), i64(price), i64(shares))
}
export function payQxAddToBidOrder(issuerId: string, assetName: bigint, price: bigint | number, shares: bigint | number): Uint8Array {
  return concatBytes(identityToBytes(issuerId), u64(assetName), i64(price), i64(shares))
}
export function payQxTransferMgmtRights(issuerId: string, assetName: bigint, shares: bigint | number, newMgrIndex: number): Uint8Array {
  return concatBytes(identityToBytes(issuerId), u64(assetName), i64(shares), u32(newMgrIndex))
}
