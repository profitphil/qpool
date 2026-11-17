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
import { AmountInput, Button, ErrorText, Field, Select, SuccessText, TextInput } from './ui'

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
  const showShares = isBuyQswap || isBuyQx || isIpo || kind === KIND_SELL_QSWAP || kind === KIND_SELL_QX || kind === KIND_SELL_QX_LIMIT
  const showPrice = isBuyQx || isIpo || kind === KIND_SELL_QX || kind === KIND_SELL_QX_LIMIT
  // IPO's budget is DERIVED (shares * price), not entered directly -- see
  // impliedIpoCost below. SELL_QX_LIMIT forces quAmount to 0 (QPoolK6.h
  // requires it there).
  const showBudget = kind !== KIND_SELL_QX_LIMIT && !isIpo
  // Only BUY_* kinds spend the pool's own funds (quAmount is a budget, not a
  // floor like it is for SELL_*) -- confirmed directly in QPoolK6.h's
  // Propose(): all three refuse with INVALID_INPUT if quAmount exceeds
  // uncommittedTotal, silently, with no proposal record left behind.
  const budgetSpendsPoolFunds = isBuyQswap || isBuyQx || isIpo
  // BUY_IPO's initial bid now takes shares and price directly (K9: QPoolK6.h
  // requires quAmount == price*shares exactly for this kind), so a pool can
  // deliberately bid for fewer shares than its full budget would buy --
  // staggering across multiple bids -- instead of always committing
  // everything to the first one.
  const impliedIpoCost = isIpo && shares && price ? BigInt(shares) * BigInt(price) : null
  const budgetExceedsFunds = budgetSpendsPoolFunds
    && ((isIpo ? impliedIpoCost : quAmount ? BigInt(quAmount) : null) ?? 0n) > availableFunds
  const budgetLabel = isBuyQswap || isBuyQx
    ? 'Budget (QU to spend)'
    : kind === KIND_ALLOCATE
      ? 'Amount to allocate (QU)'
      : 'Minimum proceeds (QU) -- refuses rather than sell for less'
  const sharesLabel = isIpo
    ? 'Shares to bid for *'
    : isBuyQswap || isBuyQx
      ? 'Minimum shares you will accept'
      : 'Shares to sell'
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
        kind === KIND_SELL_QX_LIMIT ? 0n : isIpo ? (impliedIpoCost ?? 0n) : BigInt(quAmount || 0),
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
          Pick how many shares to bid for now and at what price -- you don't have to use the pool's whole budget on
          the first bid. Leave some uncommitted to stagger a second bid later, or raise this one with RebidIpo once
          more funds come in.
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
          <AmountInput value={quAmount} onChange={setQuAmount} />
        </Field>
      )}
      {budgetExceedsFunds && (
        <ErrorText>
          That's more than this pool has available ({availableFunds.toLocaleString()} QU) -- it would be refused
          silently with no proposal created. Contribute more first, or lower the budget.
        </ErrorText>
      )}
      {showShares && (
        <Field label={sharesLabel} hint={isIpo ? 'Max 676 -- the whole IPO only has that many seats.' : undefined}>
          <AmountInput value={shares} onChange={setShares} />
        </Field>
      )}
      {showPrice && (
        <Field label={priceLabel}>
          <AmountInput value={price} onChange={setPrice} />
        </Field>
      )}
      {isIpo && (
        <div className="text-[11px] text-muted-foreground">
          {impliedIpoCost !== null
            ? `= ${impliedIpoCost.toLocaleString()} QU committed to this bid (shares × price) -- the rest of the pool's uncommitted funds stay free for a later bid.`
            : 'Fill in shares and price above to see the QU this bid will commit.'}
        </div>
      )}

      <Button disabled={busy || !canSubmit} onClick={() => void submit()}>
        {busy ? 'submitting...' : 'Propose'}
      </Button>
      <ErrorText>{error}</ErrorText>
      <SuccessText>{result}</SuccessText>
    </div>
  )
}
