// Builds + signs a transaction calling a QPoolK6 (or QX, etc.) procedure.
// Wraps @qubic-lib/qubic-ts-library's QubicTransaction, which internally
// builds the exact 80-byte header + payload + 64-byte signature layout
// confirmed byte-for-byte against this devkit's own tools-bin/qtx.cpp:
// sourcePublicKey(32) destinationPublicKey(32) amount(i64) tick(u32)
// inputType(u16) inputSize(u16), then the payload, then the signature.
import { QubicTransaction, DynamicPayload, PublicKey, Long } from './lib'
import { contractIndexToBytes } from './codec'
import { TICK_OFFSET } from './config'
import { getTick, broadcastTransaction, waitUntilTick } from './rpc'
import type { Wallet } from './identity'

export interface CallParams {
  seed: string
  sourceIdentity: string
  contractIndex: number
  amount: bigint | number
  currentTick: number
  inputType: number
  payload?: Uint8Array
}

export interface SignedCall {
  bytes: Uint8Array
  base64: string
  tick: number
}

export async function buildAndSignContractCall(params: CallParams): Promise<SignedCall> {
  const tick = params.currentTick + TICK_OFFSET
  const tx = new QubicTransaction()
    .setSourcePublicKey(params.sourceIdentity)
    .setDestinationPublicKey(new PublicKey(contractIndexToBytes(params.contractIndex)))
    .setAmount(new Long(typeof params.amount === 'bigint' ? params.amount : BigInt(params.amount)))
    .setTick(tick)
    .setInputType(params.inputType)

  const payloadBytes = params.payload ?? new Uint8Array(0)
  if (payloadBytes.length > 0) {
    const payload = new DynamicPayload(payloadBytes.length)
    payload.setPayload(payloadBytes)
    tx.setPayload(payload)
  }

  const built = await tx.build(params.seed)
  return { bytes: built, base64: tx.encodeTransactionToBase64(built), tick }
}

export interface SubmitResult {
  tick: number
  transactionId?: string
}

// The one path every write-form in this app uses: get the current tick,
// build+sign, broadcast, and wait for the targeted tick to pass so the
// caller can immediately re-query and show the result. Throws if the node
// never accepted the broadcast (peersBroadcasted missing/falsy).
export async function submitProcedureCall(
  wallet: Wallet,
  contractIndex: number,
  inputType: number,
  amount: bigint | number,
  payload?: Uint8Array,
): Promise<SubmitResult> {
  const { tick: currentTick } = await getTick()
  const signed = await buildAndSignContractCall({
    seed: wallet.seed,
    sourceIdentity: wallet.identity,
    contractIndex,
    amount,
    currentTick,
    inputType,
    payload,
  })
  const result = await broadcastTransaction(signed.bytes)
  if (!result.peersBroadcasted) {
    throw new Error(`Broadcast was not accepted: ${JSON.stringify(result)}`)
  }
  await waitUntilTick(signed.tick + 2)
  return { tick: signed.tick, transactionId: result.transactionId as string | undefined }
}

// A plain QU transfer to a wallet or contract -- same envelope, no payload,
// inputType 0. Used for sending a "dividend" from a declared payer wallet,
// or a plain top-up transfer.
export async function buildAndSignTransfer(params: {
  seed: string
  sourceIdentity: string
  destinationIdentity: string
  amount: bigint | number
  currentTick: number
}): Promise<SignedCall> {
  const tick = params.currentTick + TICK_OFFSET
  const tx = new QubicTransaction()
    .setSourcePublicKey(params.sourceIdentity)
    .setDestinationPublicKey(params.destinationIdentity)
    .setAmount(new Long(typeof params.amount === 'bigint' ? params.amount : BigInt(params.amount)))
    .setTick(tick)
    .setInputType(0)
    .setInputSize(0)
  const built = await tx.build(params.seed)
  return { bytes: built, base64: tx.encodeTransactionToBase64(built), tick }
}
