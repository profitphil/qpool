import { useEffect, useMemo, useState } from 'react'
import { useWallet } from '../context/WalletContext'
import { submitProcedureCall } from '../qubic/transaction'
import { payQxAddToBidOrder } from '../qubic/encode'
import { getTick } from '../qubic/rpc'
import { scanPools, scanAllHoldings, scanAllPoolOffers, scanAssetAsks, type AssetListing } from '../qubic/scan'
import { scanKnownAssetCatalog, type KnownAsset } from '../qubic/knownAssets'
import { QX, QX_P_ADD_TO_BID_ORDER } from '../qubic/config'
import { activeIpoTargets } from './EpochBanner'
import { BuyRow } from './SeatMarket'
import { useAction } from '../hooks/useAction'
import { Avatar, Badge, Button, Card, EmptyState, ErrorText, Field, Identity, Skeleton, Stat, SuccessText, TextInput } from './ui'
import { IconBook, IconCoins, IconSearch, IconSeat, IconTicket } from './icons'

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
    <div className="border border-border rounded-xl p-3 space-y-2.5 bg-input/20">
      <div className="flex items-center gap-2">
        <Avatar seed={listing.assetNameStr} />
        <div className="min-w-0">
          <div className="font-semibold text-sm truncate">{listing.assetNameStr}</div>
          <Identity id={listing.issuer} label="issuer" />
        </div>
        <Badge tone="info" dot>on QX</Badge>
      </div>
      <div className="flex items-baseline justify-between">
        <Stat label="lowest ask" value={`${best.price.toString()} QU`} accent="var(--color-primary)" />
        <div className="text-right text-xs text-muted-foreground">
          <div>{best.numberOfShares.toString()} at this price</div>
          <div>{totalShares.toString()} resting total</div>
        </div>
      </div>
      {wallet && (
        <div className="flex gap-2 items-end pt-1 border-t border-border/50">
          <Field label="shares to bid for">
            <TextInput value={shares} onChange={(e) => setShares(e.target.value)} inputMode="numeric" />
          </Field>
          <Button variant="primary" size="sm" loading={busy} onClick={() => void bid()}>
            Bid @ {best.price.toString()}
          </Button>
        </div>
      )}
      <ErrorText>{error}</ErrorText>
      <SuccessText>{result}</SuccessText>
    </div>
  )
}

function KnownAssetRow({ asset, hasLiveAsk }: { asset: KnownAsset; hasLiveAsk: boolean }) {
  return (
    <div className="flex items-center gap-3 py-2 border-b border-border/40 last:border-0">
      <Avatar seed={asset.assetNameStr} size={28} />
      <div className="min-w-0 flex-1">
        <div className="flex items-center gap-2">
          <span className="font-medium text-sm">{asset.assetNameStr}</span>
          {hasLiveAsk && <Badge tone="success" dot>listed on QX</Badge>}
        </div>
        <Identity id={asset.issuer} label="issuer" />
      </div>
      <div className="text-right text-xs">
        <div className="font-mono tabular-nums">{asset.totalUnits.toLocaleString()}</div>
        <div className="text-muted-foreground">{asset.holders.length} known holder{asset.holders.length === 1 ? '' : 's'}</div>
      </div>
    </div>
  )
}

export function Marketplace() {
  const [epoch, setEpoch] = useState<number | null>(null)
  const [listings, setListings] = useState<AssetListing[]>([])
  const [offers, setOffers] = useState<Awaited<ReturnType<typeof scanAllPoolOffers>>>([])
  const [known, setKnown] = useState<KnownAsset[] | null>(null)
  const [query, setQuery] = useState('')
  const [scanLimit, setScanLimit] = useState(60)
  const [busy, setBusy] = useState(false)
  const [loadedOnce, setLoadedOnce] = useState(false)
  const [error, setError] = useState<string | null>(null)

  async function load() {
    setBusy(true)
    setError(null)
    try {
      const [{ epoch: e }, pools, knownCatalog] = await Promise.all([getTick(), scanPools(30), scanKnownAssetCatalog()])
      setEpoch(e)
      setKnown(knownCatalog)
      const holdings = await scanAllHoldings(scanLimit)
      const candidates = [
        ...holdings.map(({ holding: h }) => ({ issuer: h.issuer, assetName: h.assetName, assetNameStr: h.assetNameStr })),
        ...knownCatalog.map((a) => ({ issuer: a.issuer, assetName: a.assetName, assetNameStr: a.assetNameStr })),
      ]
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
      setLoadedOnce(true)
    }
  }

  useEffect(() => {
    void load()
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [])

  const active = epoch === null ? [] : activeIpoTargets(epoch)
  const askedAssetKeys = useMemo(() => new Set(listings.map((l) => `${l.issuer}:${l.assetName}`)), [listings])
  const filteredKnown = useMemo(() => {
    if (!known) return []
    const q = query.trim().toUpperCase()
    return q ? known.filter((a) => a.assetNameStr.toUpperCase().includes(q)) : known
  }, [known, query])

  return (
    <div className="space-y-4">
      <Card
        title="What's for sale right now"
        icon={<IconCoins />}
        right={
          <div className="flex gap-2">
            <Button size="sm" loading={busy} onClick={() => void load()}>rescan</Button>
            <Button size="sm" variant="ghost" onClick={() => setScanLimit((n) => n + 60)}>+60 holding slots</Button>
          </div>
        }
      >
        <div className="text-xs text-muted-foreground">
          Resting QX asks, open pool seat-market offers, any IPO taking bids, and every token known to exist on this
          testnet -- whether or not it's ever touched a pool. Scanning the first {scanLimit} holding slots and first
          30 pools for live listings.
        </div>
        <div className="relative max-w-xs">
          <span className="absolute left-2.5 top-1/2 -translate-y-1/2 text-muted-foreground"><IconSearch /></span>
          <TextInput
            value={query}
            onChange={(e) => setQuery(e.target.value)}
            placeholder="Search known tokens (e.g. QDOGE)"
            className="pl-8"
          />
        </div>
        <ErrorText>{error}</ErrorText>
      </Card>

      <Card title={`IPO open for bidding (${active.length})`} icon={<IconTicket />} iconTone="warning">
        {!loadedOnce ? (
          <Skeleton className="h-10" />
        ) : active.length === 0 ? (
          <EmptyState icon="🎟" title="No IPO open this epoch" />
        ) : (
          <div className="grid sm:grid-cols-2 gap-2">
            {active.map((t) => (
              <div key={t.index} className="flex items-center justify-between border border-border rounded-xl p-3 bg-input/20">
                <div className="flex items-center gap-2">
                  <Avatar seed={t.name} size={28} />
                  <div>
                    <div className="font-semibold text-sm">{t.name}</div>
                    <div className="text-xs text-muted-foreground">contract index {t.index}</div>
                  </div>
                </div>
                <Badge tone="warning" dot>bidding open</Badge>
              </div>
            ))}
          </div>
        )}
        {active.length > 0 && (
          <div className="text-[11px] text-muted-foreground">Use Propose &rarr; "Bid in an IPO" in any pool, with that contract index.</div>
        )}
      </Card>

      <Card title={`QX order book -- resting asks (${listings.length})`} icon={<IconBook />} iconTone="info">
        {!loadedOnce ? (
          <div className="grid sm:grid-cols-2 gap-2">
            <Skeleton className="h-28" />
            <Skeleton className="h-28" />
          </div>
        ) : listings.length === 0 ? (
          <EmptyState icon="📭" title="No resting asks right now" hint="Checked every token seen in a holding or held by a known testnet wallet." />
        ) : (
          <div className="grid sm:grid-cols-2 gap-2">
            {listings.map((l, i) => (
              <QuickBidRow key={i} listing={l} onChanged={load} />
            ))}
          </div>
        )}
      </Card>

      <Card title={`Pool seats for sale (${offers.length})`} icon={<IconSeat />} iconTone="success">
        {!loadedOnce ? (
          <Skeleton className="h-20" />
        ) : offers.length === 0 ? (
          <EmptyState icon="🪑" title="No open seat-market offers" hint="Checked the first 30 pools." />
        ) : (
          <div className="grid sm:grid-cols-2 gap-2">
            {offers.map((o, i) => (
              <div key={i} className="space-y-1.5">
                <div className="flex items-center gap-1.5 text-xs">
                  <Badge tone="neutral">{o.poolName} (#{o.poolId})</Badge>
                </div>
                <BuyRow offer={o} onChanged={load} />
              </div>
            ))}
          </div>
        )}
      </Card>

      <Card title={`Known tokens on this testnet (${filteredKnown.length})`} icon={<IconCoins />} iconTone="neutral">
        <div className="text-xs text-muted-foreground">
          Every token any of this devkit's known demo/test wallets currently hold, regardless of whether it's in a
          pool or listed anywhere -- this is the full catalog, not just active listings.
        </div>
        {!loadedOnce ? (
          <Skeleton className="h-24" />
        ) : filteredKnown.length === 0 ? (
          <EmptyState
            icon="🔍"
            title={query ? `No token matching "${query}"` : 'No tokens found'}
            hint={query ? 'Nothing issued under that name among this testnet\'s known wallets.' : undefined}
          />
        ) : (
          <div>
            {filteredKnown.map((a, i) => (
              <KnownAssetRow key={i} asset={a} hasLiveAsk={askedAssetKeys.has(`${a.issuer}:${a.assetName}`)} />
            ))}
          </div>
        )}
      </Card>
    </div>
  )
}
