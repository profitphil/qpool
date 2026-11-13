import { useEffect, useState } from 'react'
import { useWallet } from '../context/WalletContext'
import { submitProcedureCall } from '../qubic/transaction'
import { payQxAddToBidOrder } from '../qubic/encode'
import { getTick } from '../qubic/rpc'
import { scanPools, scanAllHoldings, scanAllPoolOffers, scanAssetAsks, type AssetListing } from '../qubic/scan'
import { QX, QX_P_ADD_TO_BID_ORDER } from '../qubic/config'
import { activeIpoTargets } from './EpochBanner'
import { useAction } from '../hooks/useAction'
import { BuyRow } from './SeatMarket'
import { Button, Card, ErrorText, Field, KV, SuccessText, TextInput } from './ui'

function QuickBidRow({ listing, onChanged }: { listing: AssetListing; onChanged?: () => void }) {
  const { wallet } = useWallet()
  const best = listing.asks.reduce((a, b) => (a.price < b.price ? a : b))
  const totalShares = listing.asks.reduce((sum, o) => sum + o.numberOfShares, 0n)
  const [shares, setShares] = useState(best.numberOfShares.toString())
  const { busy, error, result, run } = useAction<string>()

  async function bid() {
    if (!wallet) return
    await run(async () => {
      const sharesNum = BigInt(shares || '0')
      const payload = payQxAddToBidOrder(listing.issuer, listing.assetName, best.price, sharesNum)
      const r = await submitProcedureCall(wallet, QX, QX_P_ADD_TO_BID_ORDER, best.price * sharesNum, payload)
      onChanged?.()
      return `Submitted at tick ${r.tick} -- fills automatically against this ask over the next tick or two, it isn't synchronous. Check "My orders" below to confirm.`
    })
  }

  return (
    <div className="border border-border rounded p-2 space-y-1">
      <KV k="asset" v={listing.assetNameStr} />
      <KV k="issuer" v={listing.issuer} />
      <KV k="lowest ask" v={`${best.price.toString()} QU/share`} />
      <KV k="resting shares (this price)" v={best.numberOfShares.toString()} />
      <KV k="resting shares (all asks)" v={totalShares.toString()} />
      {wallet && (
        <div className="flex gap-2 items-end">
          <Field label="shares to bid for">
            <TextInput value={shares} onChange={(e) => setShares(e.target.value)} inputMode="numeric" />
          </Field>
          <Button disabled={busy} onClick={() => void bid()}>
            {busy ? 'bidding...' : `Bid @ ${best.price.toString()}`}
          </Button>
        </div>
      )}
      <ErrorText>{error}</ErrorText>
      <SuccessText>{result}</SuccessText>
    </div>
  )
}

export function Marketplace() {
  const [epoch, setEpoch] = useState<number | null>(null)
  const [listings, setListings] = useState<AssetListing[]>([])
  const [offers, setOffers] = useState<Awaited<ReturnType<typeof scanAllPoolOffers>>>([])
  const [scanLimit, setScanLimit] = useState(60)
  const [busy, setBusy] = useState(false)
  const [error, setError] = useState<string | null>(null)

  async function load() {
    setBusy(true)
    setError(null)
    try {
      const [{ epoch: e }, pools] = await Promise.all([getTick(), scanPools(30)])
      setEpoch(e)
      const holdings = await scanAllHoldings(scanLimit)
      const candidates = holdings.map(({ holding: h }) => ({
        issuer: h.issuer, assetName: h.assetName, assetNameStr: h.assetNameStr,
      }))
      const [assetListings, poolOffers] = await Promise.all([
        scanAssetAsks(candidates),
        scanAllPoolOffers(pools),
      ])
      setListings(assetListings)
      setOffers(poolOffers)
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e))
    } finally {
      setBusy(false)
    }
  }

  useEffect(() => {
    void load()
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [])

  const active = epoch === null ? [] : activeIpoTargets(epoch)

  return (
    <div className="space-y-4">
      <Card title="What's for sale right now">
        <div className="text-xs text-muted-foreground">
          Scans the first {scanLimit} holding slots and the first 30 pools for anything currently listed --
          resting asks on QX's raw order book, open seats on a pool's internal seat market, and any IPO
          target currently taking bids. New deposits outside this range won't show up until you scan more.
        </div>
        <div className="flex gap-2 items-center">
          <Button disabled={busy} onClick={() => void load()}>
            {busy ? 'scanning...' : 'rescan'}
          </Button>
          <Button onClick={() => setScanLimit((n) => n + 60)}>scan 60 more holding slots</Button>
        </div>
        <ErrorText>{error}</ErrorText>
      </Card>

      <Card title={`IPO open for bidding (${active.length})`}>
        {active.length === 0 ? (
          <div className="text-xs text-muted-foreground">None open this epoch.</div>
        ) : (
          <div className="space-y-1">
            {active.map((t) => (
              <KV key={t.index} k={`${t.name} (contract index ${t.index})`} v={'use Propose -> "Bid in an IPO" in any pool'} />
            ))}
          </div>
        )}
      </Card>

      <Card title={`QX order book -- resting asks (${listings.length} asset${listings.length === 1 ? '' : 's'})`}>
        {listings.length === 0 ? (
          <div className="text-xs text-muted-foreground">No resting asks found on any token seen in a holding so far.</div>
        ) : (
          <div className="grid sm:grid-cols-2 gap-2">
            {listings.map((l, i) => (
              <QuickBidRow key={i} listing={l} onChanged={load} />
            ))}
          </div>
        )}
      </Card>

      <Card title={`Pool seats for sale (${offers.length})`}>
        {offers.length === 0 ? (
          <div className="text-xs text-muted-foreground">No open seat-market offers in the first 30 pools.</div>
        ) : (
          <div className="grid sm:grid-cols-2 gap-2">
            {offers.map((o, i) => (
              <div key={i} className="space-y-1">
                <div className="text-xs font-semibold">
                  Pool: {o.poolName} (#{o.poolId})
                </div>
                <BuyRow offer={o} onChanged={load} />
              </div>
            ))}
          </div>
        )}
      </Card>
    </div>
  )
}
