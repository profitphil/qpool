import { useState } from 'react'
import { useWallet } from '../context/WalletContext'
import { submitProcedureCall } from '../qubic/transaction'
import { payRebidIpo, paySettleIpo } from '../qubic/encode'
import { QPOOLK6, P_REBID_IPO, P_SETTLE_IPO } from '../qubic/config'
import { useAction } from '../hooks/useAction'
import { Button, ErrorText, Field, SuccessText, TextInput } from './ui'

export function RebidIpoForm({ holdingId, onChanged }: { holdingId: number; onChanged?: () => void }) {
  const { wallet } = useWallet()
  const [price, setPrice] = useState('')
  const [quantity, setQuantity] = useState('')
  const rebid = useAction<string>()
  const settle = useAction<string>()

  if (!wallet) return null

  async function submitRebid() {
    await rebid.run(async () => {
      const payload = payRebidIpo(holdingId, BigInt(price), BigInt(quantity))
      const r = await submitProcedureCall(wallet!, QPOOLK6, P_REBID_IPO, 0, payload)
      onChanged?.()
      return `Submitted at tick ${r.tick} -- check ipoMaxPrice/ipoAvailable above to confirm it actually changed.`
    })
  }

  async function submitSettle() {
    await settle.run(async () => {
      const r = await submitProcedureCall(wallet!, QPOOLK6, P_SETTLE_IPO, 0, paySettleIpo(holdingId))
      onChanged?.()
      return `Submitted at tick ${r.tick} -- check ipoSettled/shares above (free to call even if BEGIN_EPOCH already auto-settled it).`
    })
  }

  return (
    <div className="space-y-1 border-t border-border pt-1 mt-1">
      <div className="text-xs font-semibold">IPO holding</div>
      <div className="flex gap-2 flex-wrap items-end">
        <Field label="New price">
          <TextInput value={price} onChange={(e) => setPrice(e.target.value)} inputMode="numeric" className="w-24" />
        </Field>
        <Field label="New quantity">
          <TextInput value={quantity} onChange={(e) => setQuantity(e.target.value)} inputMode="numeric" className="w-24" />
        </Field>
        <Button disabled={rebid.busy || !price || !quantity} onClick={() => void submitRebid()}>
          {rebid.busy ? '...' : 'RebidIpo'}
        </Button>
        <Button disabled={settle.busy} onClick={() => void submitSettle()}>
          {settle.busy ? '...' : 'SettleIpo'}
        </Button>
      </div>
      <ErrorText>{rebid.error}</ErrorText>
      <SuccessText>{rebid.result}</SuccessText>
      <ErrorText>{settle.error}</ErrorText>
      <SuccessText>{settle.result}</SuccessText>
    </div>
  )
}
