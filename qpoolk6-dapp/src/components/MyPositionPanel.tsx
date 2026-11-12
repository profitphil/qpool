import { useState } from 'react'
import { useWallet } from '../context/WalletContext'
import { submitProcedureCall } from '../qubic/transaction'
import { payCancelOffer } from '../qubic/encode'
import { getPosition, type Position } from '../qubic/decode'
import { QPOOLK6, P_CANCEL_OFFER } from '../qubic/config'
import { useAction } from '../hooks/useAction'
import { Button, ErrorText, KV, SuccessText } from './ui'

// Lazy, opt-in -- fetching GetPosition for every holding on every pool view
// would add one query per holding on top of the pool's own holdings scan,
// and a free Cloudflare quick tunnel (this app's default backend exposure)
// already rate-limits bursts. One click, one query, only when wanted.
export function MyPositionPanel({ holdingId, onChanged }: { holdingId: number; onChanged?: () => void }) {
  const { wallet } = useWallet()
  const [position, setPosition] = useState<Position | null>(null)
  const check = useAction<Position>()
  const cancel = useAction<string>()

  if (!wallet) return null

  async function load() {
    const p = await check.run(() => getPosition(holdingId, wallet!.identity))
    setPosition(p)
  }

  async function doCancel() {
    await cancel.run(async () => {
      const r = await submitProcedureCall(wallet!, QPOOLK6, P_CANCEL_OFFER, 0, payCancelOffer(holdingId))
      onChanged?.()
      setPosition(null)
      return `Submitted at tick ${r.tick} -- check "my position" again to confirm it's actually gone.`
    })
  }

  return (
    <div className="space-y-1 border-t border-border pt-1 mt-1">
      <Button disabled={check.busy} onClick={() => void load()}>
        {check.busy ? 'checking...' : 'check my position'}
      </Button>
      <ErrorText>{check.error}</ErrorText>
      {position && position.found === 1 && (
        <div className="space-y-0.5">
          <KV k="my stake" v={position.cost.toString()} />
          <KV k="currently listed for sale" v={position.offeredCostUnits.toString()} />
          <KV k="asking price" v={position.askPrice.toString()} />
          <KV k="vote-locked until tick" v={position.voteLockUntilTick.toString()} />
          {position.offeredCostUnits > 0n && (
            <Button disabled={cancel.busy} onClick={() => void doCancel()}>
              {cancel.busy ? '...' : 'Cancel listing'}
            </Button>
          )}
        </div>
      )}
      {position && position.found !== 1 && <div className="text-xs text-muted-foreground">You don't own any part of this holding.</div>}
      <ErrorText>{cancel.error}</ErrorText>
      <SuccessText>{cancel.result}</SuccessText>
    </div>
  )
}
