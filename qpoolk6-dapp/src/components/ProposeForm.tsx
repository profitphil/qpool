import { useState } from 'react'
import { useWallet } from '../context/WalletContext'
import { submitProcedureCall } from '../qubic/transaction'
import { payPropose } from '../qubic/encode'
import { nameToU64 } from '../qubic/codec'
import {
  QPOOLK6, P_PROPOSE, NULL_ID,
  KIND_BUY_QSWAP, KIND_BUY_QX, KIND_BUY_IPO, KIND_SELL_QSWAP, KIND_SELL_QX, KIND_ALLOCATE, KIND_SELL_QX_LIMIT,
} from '../qubic/config'
import { useAction } from '../hooks/useAction'
import { Button, ErrorText, Field, Select, SuccessText, TextInput } from './ui'

const KINDS = [
  { v: KIND_BUY_QSWAP, label: 'BUY_QSWAP -- swap pool QU for a token on QSWAP' },
  { v: KIND_BUY_QX, label: 'BUY_QX -- bid for a token on QX\'s order book' },
  { v: KIND_BUY_IPO, label: 'BUY_IPO -- bid in a contract\'s IPO' },
  { v: KIND_SELL_QSWAP, label: 'SELL_QSWAP -- swap a holding for QU on QSWAP' },
  { v: KIND_SELL_QX, label: 'SELL_QX -- sell a holding on QX at market' },
  { v: KIND_ALLOCATE, label: 'ALLOCATE -- move uncommitted QU to committed' },
  { v: KIND_SELL_QX_LIMIT, label: 'SELL_QX_LIMIT -- rest a limit sell on QX' },
]

export function ProposeForm({ poolId, onChanged }: { poolId: number; onChanged?: () => void }) {
  const { wallet } = useWallet()
  const [kind, setKind] = useState(KIND_BUY_QX)
  const [issuerId, setIssuerId] = useState('')
  const [assetName, setAssetName] = useState('')
  const [quAmount, setQuAmount] = useState('')
  const [shares, setShares] = useState('')
  const [price, setPrice] = useState('')
  const [holdingOrTargetId, setHoldingOrTargetId] = useState('')
  const { busy, error, result, run } = useAction<string>()

  if (!wallet) return null
  const isIpo = kind === KIND_BUY_IPO

  async function submit() {
    await run(async () => {
      const payload = payPropose(
        poolId,
        kind,
        isIpo ? NULL_ID : issuerId,
        isIpo ? 0n : nameToU64(assetName),
        BigInt(quAmount || 0),
        BigInt(shares || 0),
        BigInt(price || 0),
        Number(holdingOrTargetId || 0),
      )
      const r = await submitProcedureCall(wallet!, QPOOLK6, P_PROPOSE, 0, payload)
      onChanged?.()
      return `Submitted at tick ${r.tick} -- look it up in the proposals list below (poolId=${poolId}, kind=${kind}) to confirm it was actually recorded (refused silently fails to appear there).`
    })
  }

  return (
    <div className="space-y-2">
      <Field label="Kind">
        <Select value={kind} onChange={(e) => setKind(Number(e.target.value))}>
          {KINDS.map((k) => (
            <option key={k.v} value={k.v}>
              {k.label}
            </option>
          ))}
        </Select>
      </Field>
      {!isIpo && (
        <>
          <Field label="Token issuer identity (contract shares: leave blank)">
            <TextInput value={issuerId} onChange={(e) => setIssuerId(e.target.value)} placeholder="60-char identity or blank" />
          </Field>
          <Field label="Asset name (1-7 chars; contract shares: blank)">
            <TextInput value={assetName} onChange={(e) => setAssetName(e.target.value)} maxLength={7} placeholder="MYTOKEN" />
          </Field>
        </>
      )}
      {isIpo && <div className="text-xs text-muted-foreground">BUY_IPO never names an issuer/asset upfront (read from chain events at settlement).</div>}
      <Field label="QU amount (budget) *">
        <TextInput value={quAmount} onChange={(e) => setQuAmount(e.target.value)} inputMode="numeric" required />
      </Field>
      {isIpo && (
        <div className="text-xs text-muted-foreground">
          There's no way to know how much the pool will have collected by the time this executes -- pick the most
          QU you want to commit to this bid now. You can raise it later with RebidIpo if more comes in, but the
          proposal itself needs a concrete number, not a guess at the pool's future balance.
        </div>
      )}
      <Field label="Shares (BUY_*: min-out / quantity)">
        <TextInput value={shares} onChange={(e) => setShares(e.target.value)} inputMode="numeric" />
      </Field>
      <Field label="Price">
        <TextInput value={price} onChange={(e) => setPrice(e.target.value)} inputMode="numeric" />
      </Field>
      <Field label={isIpo ? 'Target contract index (e.g. the IPO contract)' : 'holdingId (contract-shares BUY_*: paying contract index; else 0)'}>
        <TextInput value={holdingOrTargetId} onChange={(e) => setHoldingOrTargetId(e.target.value)} inputMode="numeric" />
      </Field>
      <Button disabled={busy || !quAmount} onClick={() => void submit()}>
        {busy ? 'submitting...' : 'Propose'}
      </Button>
      <ErrorText>{error}</ErrorText>
      <SuccessText>{result}</SuccessText>
    </div>
  )
}
