import { useState } from 'react'
import type { Holding } from '../qubic/decode'
import { useWallet } from '../context/WalletContext'
import { submitProcedureCall } from '../qubic/transaction'
import { payOfferPosition } from '../qubic/encode'
import { QPOOLK6, P_OFFER_POSITION } from '../qubic/config'
import { useAction } from '../hooks/useAction'
import { AmountInput, Button, ErrorText, Field, SuccessText } from './ui'

export function OfferPositionForm({
  holdingId, holding, onChanged,
}: { holdingId: number; holding: Holding; onChanged?: () => void }) {
  const { wallet } = useWallet()
  const [sharesToSell, setSharesToSell] = useState('')
  const [askPrice, setAskPrice] = useState('')
  const { busy, error, result, run } = useAction<string>()

  if (!wallet) return null

  // OfferPosition takes cost-units, not shares -- costBasis is the QU
  // originally paid for ALL of this holding's shares combined, a totally
  // different scale from the raw share count. Confirmed live: a 500-share/
  // 500,000-cost-unit holding offered at a bare costUnits=100 (meant as
  // "100 shares") came out to 0.1 shares, rounding down to "0 of 500" for
  // every buyer. Converting from shares the seller actually means to sell
  // avoids that trap instead of asking for the internal unit directly.
  const costUnits = sharesToSell && holding.shares > 0n ? (BigInt(sharesToSell) * holding.costBasis) / holding.shares : null

  async function submit() {
    await run(async () => {
      if (costUnits === null) throw new Error('enter shares to sell')
      const payload = payOfferPosition(holdingId, costUnits, BigInt(askPrice))
      const r = await submitProcedureCall(wallet!, QPOOLK6, P_OFFER_POSITION, 0, payload)
      onChanged?.()
      return `Submitted at tick ${r.tick} -- check "my position" below to confirm it's actually listed (needs you to own that many shares of this holding).`
    })
  }

  return (
    <div className="space-y-1 border-t border-border pt-1 mt-1">
      <div className="text-xs font-semibold">Offer part of my position for sale</div>
      <div className="text-[11px] text-muted-foreground">
        This holding has {holding.shares.toLocaleString()} {holding.assetNameStr || 'shares'} total. Enter how many of
        YOUR shares to sell -- not a raw cost-unit number.
      </div>
      <div className="flex gap-2 flex-wrap items-end">
        <Field label="Shares to sell">
          <AmountInput value={sharesToSell} onChange={setSharesToSell} className="w-28" />
        </Field>
        <Field label="Asking price (total QU)">
          <AmountInput value={askPrice} onChange={setAskPrice} className="w-28" />
        </Field>
        <Button disabled={busy || !sharesToSell || !askPrice} onClick={() => void submit()}>
          {busy ? '...' : 'List for sale'}
        </Button>
      </div>
      {costUnits !== null && (
        <div className="text-[11px] text-muted-foreground">
          = {costUnits.toLocaleString()} cost-units -- make sure that many are still actually yours (check "my
          position" below) and not already listed elsewhere.
        </div>
      )}
      <ErrorText>{error}</ErrorText>
      <SuccessText>{result}</SuccessText>
    </div>
  )
}
