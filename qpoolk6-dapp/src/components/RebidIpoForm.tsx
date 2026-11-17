import { useEffect, useState } from 'react'
import type { Holding } from '../qubic/decode'
import { getIpoTarget } from '../qubic/decode'
import { getTick } from '../qubic/rpc'
import { useWallet } from '../context/WalletContext'
import { submitProcedureCall } from '../qubic/transaction'
import { payRebidIpo, paySettleIpo } from '../qubic/encode'
import { QPOOLK6, P_REBID_IPO, P_SETTLE_IPO } from '../qubic/config'
import { useAction } from '../hooks/useAction'
import { AmountInput, Button, ErrorText, Field, SuccessText } from './ui'

export function RebidIpoForm({
  holdingId, holding, onChanged,
}: { holdingId: number; holding: Holding; onChanged?: () => void }) {
  const { wallet } = useWallet()
  const [price, setPrice] = useState('')
  const [quantity, setQuantity] = useState('')
  const rebid = useAction<string>()
  const settle = useAction<string>()
  // SettleHolding (QPoolK6.h) only accepts once the IPO target's auction has
  // actually closed: row.snapshotEpoch != 0 (END_EPOCH took the snapshot) AND
  // qpi.epoch() > row.snapshotEpoch (we're at least one epoch past it). Before
  // that it refuses with WINDOW_OPEN/IPO_NO_SNAPSHOT every time -- showing the
  // button during live bidding just invites a guaranteed-to-fail click.
  const [settleReady, setSettleReady] = useState(false)

  useEffect(() => {
    let cancelled = false
    async function poll() {
      try {
        const [tgt, t] = await Promise.all([getIpoTarget(Number(holding.ipoContractIndex)), getTick()])
        if (cancelled) return
        setSettleReady(tgt.found === 1 && tgt.snapshotEpoch > 0n && BigInt(t.epoch) > tgt.snapshotEpoch)
      } catch {
        // leave settleReady as-is; a transient RPC hiccup shouldn't flip the button
      }
    }
    void poll()
    const id = setInterval(poll, 5000)
    return () => {
      cancelled = true
      clearInterval(id)
    }
  }, [holding.ipoContractIndex])

  if (!wallet) return null

  // Once settled, the IPO's shares are final -- RebidIpo/SettleIpo both
  // refuse at that point, so showing "Update bid"/"Settle now" afterward
  // just reads as still-live and confuses a seller checking their holding.
  if (holding.ipoSettled > 0n) {
    return (
      <div className="text-[11px] text-muted-foreground border-t border-border pt-1 mt-1">
        IPO settled -- shares are final, nothing left to bid or settle.
      </div>
    )
  }

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
          <AmountInput value={price} onChange={setPrice} className="w-24" />
        </Field>
        <Field label="New quantity">
          <AmountInput value={quantity} onChange={setQuantity} className="w-24" />
        </Field>
        <Button disabled={rebid.busy || !price || !quantity} onClick={() => void submitRebid()}>
          {rebid.busy ? '...' : 'Update bid'}
        </Button>
        {settleReady && (
          <Button disabled={settle.busy} onClick={() => void submitSettle()}>
            {settle.busy ? '...' : 'Settle now'}
          </Button>
        )}
      </div>
      {!settleReady && (
        <div className="text-[11px] text-muted-foreground">
          Settling unlocks once this IPO's bidding epoch ends -- it auto-settles at the next epoch's start anyway.
        </div>
      )}
      <ErrorText>{rebid.error}</ErrorText>
      <SuccessText>{rebid.result}</SuccessText>
      <ErrorText>{settle.error}</ErrorText>
      <SuccessText>{settle.result}</SuccessText>
    </div>
  )
}
