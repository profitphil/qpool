import { useState } from 'react'
import { useWallet } from '../context/WalletContext'
import { submitProcedureCall } from '../qubic/transaction'
import { payPropose } from '../qubic/encode'
import { nameToU64 } from '../qubic/codec'
import {
  QPOOLK6, P_PROPOSE, NULL_ID, KIND_NAMES,
  KIND_BUY_QSWAP, KIND_BUY_QX, KIND_BUY_IPO, KIND_SELL_QSWAP, KIND_SELL_QX, KIND_ALLOCATE, KIND_SELL_QX_LIMIT,
} from '../qubic/config'
import { useAction } from '../hooks/useAction'
import type { Holding } from '../qubic/decode'
import { Button, ErrorText, Field, Select, SuccessText, TextInput } from './ui'

const KINDS = [
  KIND_BUY_QSWAP, KIND_BUY_QX, KIND_BUY_IPO, KIND_SELL_QSWAP, KIND_SELL_QX, KIND_ALLOCATE, KIND_SELL_QX_LIMIT,
].map((v) => ({ v, label: KIND_NAMES[v] }))

export function ProposeForm({
  poolId,
  holdings,
  availableFunds,
  onChanged,
}: {
  poolId: number
  holdings: { id: number; holding: Holding }[]
  availableFunds: bigint
  onChanged?: () => void
}) {
  const { wallet } = useWallet()
  const [kind, setKind] = useState(KIND_BUY_QX)
  const [issuerId, setIssuerId] = useState('')
  const [assetName, setAssetName] = useState('')
  const [quAmount, setQuAmount] = useState('')
  const [shares, setShares] = useState('')
  const [price, setPrice] = useState('')
  const [holdingOrTargetId, setHoldingOrTargetId] = useState('')
  const [contractShares, setContractShares] = useState(false)
  const { busy, error, result, run } = useAction<string>()

  if (!wallet) return null

  // What this kind actually needs -- mirrors QPoolK6.h's Propose() validation
  // exactly, not a guess: a field that kind ignores is hidden, not just optional.
  const isIpo = kind === KIND_BUY_IPO
  const isBuyQswap = kind === KIND_BUY_QSWAP
  const isBuyQx = kind === KIND_BUY_QX
  const isSellOrAllocate = kind === KIND_SELL_QSWAP || kind === KIND_SELL_QX || kind === KIND_ALLOCATE || kind === KIND_SELL_QX_LIMIT
  const buyingRealToken = (isBuyQswap || isBuyQx) && !(isBuyQx && contractShares)
  const showTokenFields = buyingRealToken
  const showContractSharesToggle = isBuyQx
  const showHoldingPicker = isSellOrAllocate
  const showIpoTarget = isIpo
  const showPayerIndex = isBuyQx && contractShares
  const showShares = isBuyQswap || isBuyQx || kind === KIND_SELL_QSWAP || kind === KIND_SELL_QX || kind === KIND_SELL_QX_LIMIT
  const showPrice = isBuyQx || isIpo || kind === KIND_SELL_QX || kind === KIND_SELL_QX_LIMIT
  const showBudget = kind !== KIND_SELL_QX_LIMIT // forced to 0 for SELL_QX_LIMIT (QPoolK6.h requires quAmount==0 there)
  // Only BUY_* kinds spend the pool's own funds (quAmount is a budget, not a
  // floor like it is for SELL_*) -- confirmed directly in QPoolK6.h's
  // Propose(): all three refuse with INVALID_INPUT if quAmount exceeds
  // uncommittedTotal, silently, with no proposal record left behind.
  const budgetSpendsPoolFunds = isBuyQswap || isBuyQx || isIpo
  const budgetExceedsFunds = budgetSpendsPoolFunds && !!quAmount && BigInt(quAmount) > availableFunds
  const budgetLabel = isIpo
    ? 'Budget (max QU for this bid) *'
    : isBuyQswap || isBuyQx
      ? 'Budget (QU to spend)'
      : kind === KIND_ALLOCATE
        ? 'Amount to allocate (QU)'
        : 'Minimum proceeds (QU) -- refuses rather than sell for less'
  const sharesLabel = isBuyQswap || isBuyQx ? 'Minimum shares you will accept' : 'Shares to sell'
  const priceLabel = isIpo
    ? 'Opening bid, per share *'
    : isBuyQx
      ? 'Maximum price per share'
      : kind === KIND_SELL_QX
        ? 'Minimum price per share'
        : 'Asking price per share'
  // Only the fields actually shown for this kind gate submission -- a field
  // hidden for this kind (e.g. budget on SELL_QX_LIMIT) must never block it.
  const canSubmit =
    (!showBudget || !!quAmount) &&
    (!showShares || !!shares) &&
    (!showPrice || !!price) &&
    (!showIpoTarget || !!holdingOrTargetId) &&
    (!showPayerIndex || !!holdingOrTargetId) &&
    (!showHoldingPicker || !!holdingOrTargetId) &&
    (!showTokenFields || (!!issuerId && !!assetName)) &&
    !budgetExceedsFunds

  async function submit() {
    await run(async () => {
      const payload = payPropose(
        poolId,
        kind,
        showTokenFields ? issuerId : NULL_ID,
        showTokenFields ? nameToU64(assetName) : 0n,
        kind === KIND_SELL_QX_LIMIT ? 0n : BigInt(quAmount || 0),
        showShares ? BigInt(shares || 0) : 0n,
        showPrice ? BigInt(price || 0) : 0n,
        Number(holdingOrTargetId || 0),
      )
      const r = await submitProcedureCall(wallet!, QPOOLK6, P_PROPOSE, 0, payload)
      onChanged?.()
      return `Submitted at tick ${r.tick} -- look it up in the proposals list below (poolId=${poolId}, kind=${kind}) to confirm it was actually recorded (refused silently fails to appear there).`
    })
  }

  return (
    <div className="space-y-2">
      <Field label="What do you want to propose?">
        <Select value={kind} onChange={(e) => setKind(Number(e.target.value))}>
          {KINDS.map((k) => (
            <option key={k.v} value={k.v}>
              {k.label}
            </option>
          ))}
        </Select>
      </Field>

      {showTokenFields && (
        <>
          <Field label="Token's issuer (identity)">
            <TextInput value={issuerId} onChange={(e) => setIssuerId(e.target.value)} placeholder="60-char identity" />
          </Field>
          <Field label="Token symbol (1-7 chars)">
            <TextInput value={assetName} onChange={(e) => setAssetName(e.target.value)} maxLength={7} placeholder="MYTOKEN" />
          </Field>
        </>
      )}

      {showContractSharesToggle && (
        <label className="flex items-center gap-2 text-sm">
          <input type="checkbox" checked={contractShares} onChange={(e) => setContractShares(e.target.checked)} />
          Buying a pool's own shares instead of a token
        </label>
      )}

      {showPayerIndex && (
        <Field label="Which pool (contract index of the one selling its shares)">
          <TextInput value={holdingOrTargetId} onChange={(e) => setHoldingOrTargetId(e.target.value)} inputMode="numeric" />
        </Field>
      )}

      {showIpoTarget && (
        <Field label="Which company (contract index) *">
          <TextInput value={holdingOrTargetId} onChange={(e) => setHoldingOrTargetId(e.target.value)} inputMode="numeric" required />
        </Field>
      )}
      {isIpo && (
        <div className="text-xs text-muted-foreground">
          There's no way to know how much the pool will have collected by the time this executes -- pick the most
          QU you want to commit to this bid now. You can raise it later with RebidIpo if more comes in, but the
          proposal itself needs a concrete number, not a guess at the pool's future balance.
        </div>
      )}

      {showHoldingPicker && (
        <Field label="Which holding">
          <Select value={holdingOrTargetId} onChange={(e) => setHoldingOrTargetId(e.target.value)}>
            <option value="">-- choose a holding --</option>
            {holdings.map(({ id, holding }) => (
              <option key={id} value={id}>
                #{id} -- {holding.assetNameStr || 'pool shares'} ({holding.shares.toString()} shares)
              </option>
            ))}
          </Select>
        </Field>
      )}

      {showBudget && (
        <Field
          label={budgetLabel}
          hint={budgetSpendsPoolFunds ? `Pool has ${availableFunds.toLocaleString()} QU available (not yet allocated).` : undefined}
        >
          <TextInput value={quAmount} onChange={(e) => setQuAmount(e.target.value)} inputMode="numeric" required={isIpo} />
        </Field>
      )}
      {budgetExceedsFunds && (
        <ErrorText>
          That's more than this pool has available ({availableFunds.toLocaleString()} QU) -- it would be refused
          silently with no proposal created. Contribute more first, or lower the budget.
        </ErrorText>
      )}
      {showShares && (
        <Field label={sharesLabel}>
          <TextInput value={shares} onChange={(e) => setShares(e.target.value)} inputMode="numeric" />
        </Field>
      )}
      {showPrice && (
        <Field label={priceLabel}>
          <TextInput value={price} onChange={(e) => setPrice(e.target.value)} inputMode="numeric" required={isIpo} />
        </Field>
      )}

      <Button disabled={busy || !canSubmit} onClick={() => void submit()}>
        {busy ? 'submitting...' : 'Propose'}
      </Button>
      <ErrorText>{error}</ErrorText>
      <SuccessText>{result}</SuccessText>
    </div>
  )
}
