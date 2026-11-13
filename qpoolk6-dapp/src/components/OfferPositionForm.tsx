import { useState } from 'react'
import { useWallet } from '../context/WalletContext'
import { submitProcedureCall } from '../qubic/transaction'
import { payOfferPosition } from '../qubic/encode'
import { QPOOLK6, P_OFFER_POSITION } from '../qubic/config'
import { useAction } from '../hooks/useAction'
import { AmountInput, Button, ErrorText, Field, SuccessText } from './ui'

export function OfferPositionForm({ holdingId, onChanged }: { holdingId: number; onChanged?: () => void }) {
  const { wallet } = useWallet()
  const [costUnits, setCostUnits] = useState('')
  const [askPrice, setAskPrice] = useState('')
  const { busy, error, result, run } = useAction<string>()

  if (!wallet) return null

  async function submit() {
    await run(async () => {
      const payload = payOfferPosition(holdingId, BigInt(costUnits), BigInt(askPrice))
      const r = await submitProcedureCall(wallet!, QPOOLK6, P_OFFER_POSITION, 0, payload)
      onChanged?.()
      return `Submitted at tick ${r.tick} -- check "my position" below to confirm it's actually listed (needs you to own that many cost units of this holding).`
    })
  }

  return (
    <div className="space-y-1 border-t border-border pt-1 mt-1">
      <div className="text-xs font-semibold">Offer part of my position for sale</div>
      <div className="flex gap-2 flex-wrap items-end">
        <Field label="How much of your stake to sell">
          <AmountInput value={costUnits} onChange={setCostUnits} className="w-28" />
        </Field>
        <Field label="Asking price (total QU)">
          <AmountInput value={askPrice} onChange={setAskPrice} className="w-28" />
        </Field>
        <Button disabled={busy || !costUnits || !askPrice} onClick={() => void submit()}>
          {busy ? '...' : 'List for sale'}
        </Button>
      </div>
      <ErrorText>{error}</ErrorText>
      <SuccessText>{result}</SuccessText>
    </div>
  )
}
