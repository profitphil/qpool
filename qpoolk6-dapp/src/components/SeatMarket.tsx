import { useEffect, useState } from 'react'
import { useWallet } from '../context/WalletContext'
import { submitProcedureCall } from '../qubic/transaction'
import { payBuyPosition } from '../qubic/encode'
import { getPoolOffers, getHolding, type PoolOffer, type Holding } from '../qubic/decode'
import { QPOOLK6, P_BUY_POSITION, JOIN_FEE } from '../qubic/config'
import { useAction } from '../hooks/useAction'
import { Avatar, Button, Card, ErrorText, Identity, KV, Skeleton, Stat, SuccessText } from './ui'
import { IconSeat } from './icons'

export function BuyRow({ offer, onChanged }: { offer: PoolOffer; onChanged?: () => void }) {
  const { wallet, refreshBalance } = useWallet()
  const [holding, setHolding] = useState<Holding | null>(null)
  const { busy, error, result, run } = useAction<string>()

  useEffect(() => {
    getHolding(Number(offer.holdingId)).then(setHolding).catch(() => setHolding(null))
  }, [offer.holdingId])

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

  // offer.shares is the HOLDING's live total share count (a race guard
  // BuyPosition needs), not what this seat represents -- confirmed directly
  // in QPoolK6.h's GetPoolOffers. What a buyer actually gets is this seat's
  // cost-units as a fraction of the holding's total cost basis.
  const pct = holding && holding.costBasis > 0n ? (Number(offer.costUnits) / Number(holding.costBasis)) * 100 : null
  const impliedShares = holding && pct !== null ? Math.round((Number(holding.shares) * pct) / 100) : null

  return (
    <div className="border border-border rounded-xl p-3 space-y-2 bg-card">
      {!holding ? (
        <Skeleton className="h-10" />
      ) : (
        <div className="flex items-center gap-2">
          <Avatar seed={holding.assetNameStr} size={28} />
          <div className="min-w-0">
            <div className="font-semibold text-sm">{holding.assetNameStr}</div>
            <div className="text-[11px] text-muted-foreground">
              this seat: ~{impliedShares?.toLocaleString()} of {holding.shares.toString()} {holding.assetNameStr}
              {pct !== null && ` (${pct.toFixed(1)}% of the holding's position)`}
            </div>
          </div>
        </div>
      )}
      <div className="flex items-baseline justify-between">
        <Stat label="asking price" value={`${offer.askPrice.toLocaleString()} QU`} accent="var(--color-primary)" />
        {holding && (
          <div className="text-right text-xs text-muted-foreground">
            <div>dividends paid to this holding so far</div>
            <div className="font-mono text-foreground">{holding.totalPayouts.toLocaleString()} QU</div>
          </div>
        )}
      </div>
      <KV k="seller" v={<Identity id={offer.seller} />} />
      {wallet && (
        <Button variant="primary" size="sm" loading={busy} onClick={() => void buy()} className="w-full">
          Buy for {offer.askPrice.toLocaleString()} QU
        </Button>
      )}
      {wallet && (
        <div className="text-[11px] text-muted-foreground">
          +{JOIN_FEE.toLocaleString()} QU join fee if you're not already in this pool (refunded if you are).
        </div>
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
    <Card title={`Seat market (${offers.length} open offers)`} icon={<IconSeat />} iconTone="success">
      <div className="text-xs text-muted-foreground">
        Open offers against this pool's holdings, from OfferPosition. Buying one calls BuyPosition with exactly
        this row's numbers -- if someone else bought it or the seller changed it first, the purchase is refused
        (OFFER_CHANGED), not silently adjusted.
      </div>
      <Button loading={busy} onClick={() => void load()}>refresh</Button>
      <ErrorText>{error}</ErrorText>
      <div className="grid sm:grid-cols-2 gap-2">
        {offers.map((o, i) => (
          <BuyRow key={i} offer={o} onChanged={load} />
        ))}
      </div>
    </Card>
  )
}
