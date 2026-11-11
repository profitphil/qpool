import { useEffect, useState } from 'react'
import { useWallet } from '../context/WalletContext'
import { submitProcedureCall } from '../qubic/transaction'
import { payBuyPosition } from '../qubic/encode'
import { getPoolOffers, type PoolOffer } from '../qubic/decode'
import { QPOOLK6, P_BUY_POSITION, JOIN_FEE } from '../qubic/config'
import { useAction } from '../hooks/useAction'
import { Button, Card, ErrorText, KV, SuccessText } from './ui'

function BuyRow({ offer, onChanged }: { offer: PoolOffer; onChanged?: () => void }) {
  const { wallet, refreshBalance } = useWallet()
  const { busy, error, result, run } = useAction<string>()

  async function buy() {
    if (!wallet) return
    await run(async () => {
      const payload = payBuyPosition(Number(offer.holdingId), offer.seller, offer.costUnits, offer.shares)
      // Pay askPrice + JOIN_FEE unconditionally -- a buyer who isn't already a
      // pool member is charged the join fee on top, and the contract refunds
      // the difference automatically if they turn out to already be a member.
      // Attaching only askPrice silently fails (INSUFFICIENT_FEE) for any
      // brand-new buyer, which is the common case for this public dapp.
      const r = await submitProcedureCall(wallet, QPOOLK6, P_BUY_POSITION, offer.askPrice + BigInt(JOIN_FEE), payload)
      await refreshBalance()
      onChanged?.()
      return `Submitted at tick ${r.tick} -- check your balance and this holding's shares to confirm it actually bought (refused if the offer changed since this list last refreshed: OFFER_CHANGED).`
    })
  }

  return (
    <div className="border border-neutral-200 dark:border-neutral-800 rounded p-2 space-y-1">
      <KV k="holdingId" v={offer.holdingId.toString()} />
      <KV k="seller" v={offer.seller} />
      <KV k="costUnits" v={offer.costUnits.toString()} />
      <KV k="shares" v={offer.shares.toString()} />
      <KV k="askPrice (total QU)" v={offer.askPrice.toString()} />
      {wallet && (
        <Button disabled={busy} onClick={() => void buy()}>
          {busy
            ? 'buying...'
            : `Buy for ${offer.askPrice.toLocaleString()} QU (+${JOIN_FEE.toLocaleString()} join fee if you're not already in this pool, refunded if you are)`}
        </Button>
      )}
      <ErrorText>{error}</ErrorText>
      <SuccessText>{result}</SuccessText>
    </div>
  )
}

export function SeatMarket({ poolId }: { poolId: number }) {
  const [offers, setOffers] = useState<PoolOffer[]>([])
  const [error, setError] = useState<string | null>(null)
  const [busy, setBusy] = useState(false)

  async function load() {
    setBusy(true)
    setError(null)
    try {
      setOffers(await getPoolOffers(poolId))
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e))
    } finally {
      setBusy(false)
    }
  }

  useEffect(() => {
    void load()
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [poolId])

  return (
    <Card title={`Seat market (${offers.length} open offers)`}>
      <div className="text-xs text-neutral-500">
        Open offers against this pool's holdings, from OfferPosition. Buying one calls BuyPosition with exactly
        this row's numbers -- if someone else bought it or the seller changed it first, the purchase is refused
        (OFFER_CHANGED), not silently adjusted.
      </div>
      <Button disabled={busy} onClick={() => void load()}>
        {busy ? 'loading...' : 'refresh'}
      </Button>
      <ErrorText>{error}</ErrorText>
      <div className="grid sm:grid-cols-2 gap-2">
        {offers.map((o, i) => (
          <BuyRow key={i} offer={o} onChanged={load} />
        ))}
      </div>
    </Card>
  )
}
