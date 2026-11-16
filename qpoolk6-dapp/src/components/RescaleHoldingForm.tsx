import { useState } from 'react'
import type { Holding } from '../qubic/decode'
import { useWallet } from '../context/WalletContext'
import { submitProcedureCall } from '../qubic/transaction'
import { payRescaleHolding } from '../qubic/encode'
import { QPOOLK6, P_RESCALE_HOLDING } from '../qubic/config'
import { useAction } from '../hooks/useAction'
import { AmountInput, Button, ErrorText, Field, SuccessText } from './ui'

// Multiplies this holding's costBasis (and every member's cost, in lockstep)
// by a factor, so finer listings become possible -- e.g. a holding at 2
// cost-units per share can only ever express halves of a share; rescaling
// by 100 makes it 200 cost-units per share, so a seller can list down to
// 1/100th. This is value-neutral (10/20 and 1000/2000 are the same real
// stake), so any current holder may do it -- not just the pool creator.
// Refuses if the holding has any open seat offer anywhere (their costUnits
// would go stale at the old scale the instant this runs).
export function RescaleHoldingForm({ holdingId, holding, onChanged }: { holdingId: number; holding: Holding; onChanged?: () => void }) {
  const { wallet } = useWallet()
  const [factor, setFactor] = useState('')
  const { busy, error, result, run } = useAction<string>()

  if (!wallet) return null

  async function submit() {
    await run(async () => {
      const r = await submitProcedureCall(wallet!, QPOOLK6, P_RESCALE_HOLDING, 0, payRescaleHolding(holdingId, BigInt(factor)))
      onChanged?.()
      setFactor('')
      return `Submitted at tick ${r.tick} -- check "original cost" above to confirm it actually multiplied (refused if any seat offer is still open on this holding -- cancel it first).`
    })
  }

  return (
    <div className="space-y-1 border-t border-border pt-1 mt-1">
      <div className="text-xs font-semibold">Split this holding into finer units</div>
      <div className="text-[11px] text-muted-foreground">
        Currently {holding.costBasis.toLocaleString()} cost-units for {holding.shares.toLocaleString()} shares
        ({(Number(holding.costBasis) / Number(holding.shares)).toFixed(4)} per share) -- the smallest listable slice
        of a share is 1/that number. Multiplying by a factor gives everyone finer units without changing anyone's
        real stake. Cancel any open offers on this holding first.
      </div>
      <div className="flex gap-2 flex-wrap items-end">
        <Field label="Multiply by" hint="e.g. 100 to go from halves to hundredths of a share">
          <AmountInput value={factor} onChange={setFactor} className="w-28" />
        </Field>
        <Button disabled={busy || !factor || factor === '0' || factor === '1'} onClick={() => void submit()}>
          {busy ? '...' : 'Rescale'}
        </Button>
      </div>
      <ErrorText>{error}</ErrorText>
      <SuccessText>{result}</SuccessText>
    </div>
  )
}
