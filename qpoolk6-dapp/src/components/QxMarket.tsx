import { useEffect, useMemo, useState } from 'react'
import { useWallet } from '../context/WalletContext'
import { submitProcedureCall } from '../qubic/transaction'
import { nameToU64 } from '../qubic/codec'
import {
  payQxAddToAskOrder, payQxAddToBidOrder, payQxRemoveFromAskOrder, payQxRemoveFromBidOrder,
} from '../qubic/encode'
import {
  getAssetAskOrders, getAssetBidOrders, getEntityAskOrders, getEntityBidOrders,
  type AssetOrder, type EntityOrder,
} from '../qubic/decode'
import { scanAllHoldings, scanAssetAsks, type AssetListing } from '../qubic/scan'
import { scanKnownAssetCatalog, type KnownAsset } from '../qubic/knownAssets'
import { QX, QX_P_ADD_TO_ASK_ORDER, QX_P_ADD_TO_BID_ORDER, QX_P_REMOVE_FROM_ASK_ORDER, QX_P_REMOVE_FROM_BID_ORDER } from '../qubic/config'
import { useAction } from '../hooks/useAction'
import { AmountInput, Avatar, Badge, Button, Card, EmptyState, ErrorText, Field, Identity, KV, Select, Skeleton, Stat, SuccessText, TextInput } from './ui'
import { AssetPicker } from './AssetPicker'
import { IconBook, IconCoins, IconList, IconPlus, IconSearch } from './icons'

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
            <AmountInput value={shares} onChange={setShares} />
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

function PlaceOrderForm({ knownAssets }: { knownAssets: KnownAsset[] }) {
  const { wallet } = useWallet()
  const [side, setSide] = useState<'ask' | 'bid'>('ask')
  const [issuerId, setIssuerId] = useState('')
  const [assetName, setAssetName] = useState('')
  const [price, setPrice] = useState('')
  const [shares, setShares] = useState('')
  const { busy, error, result, run } = useAction<string>()

  if (!wallet) return <div className="text-xs text-muted-foreground">Connect a wallet to place an order.</div>

  async function submit() {
    await run(async () => {
      const payload =
        side === 'ask'
          ? payQxAddToAskOrder(issuerId, nameToU64(assetName), BigInt(price), BigInt(shares))
          : payQxAddToBidOrder(issuerId, nameToU64(assetName), BigInt(price), BigInt(shares))
      // Asks are free (need the shares themselves, already held under QX's own
      // management -- not QPoolK6's); bids must attach the full price*shares,
      // confirmed directly in Qx.h (AddToBidOrder refuses otherwise, refunding).
      const amount = side === 'bid' ? BigInt(price) * BigInt(shares) : 0n
      const r = await submitProcedureCall(wallet!, QX, side === 'ask' ? QX_P_ADD_TO_ASK_ORDER : QX_P_ADD_TO_BID_ORDER, amount, payload)
      return `Submitted at tick ${r.tick} -- check "my orders" below to confirm it actually rested (asks need the shares already under QX's own management, not QPoolK6's or any other contract's -- use "Release shares" in Tools first if you deposited them into a pool).`
    })
  }

  return (
    <div className="space-y-2">
      <Field label="Side">
        <Select value={side} onChange={(e) => setSide(e.target.value as 'ask' | 'bid')}>
          <option value="ask">Ask (sell -- you must already hold the shares)</option>
          <option value="bid">Bid (buy -- attaches price &times; shares as payment)</option>
        </Select>
      </Field>
      <Field label="Find a token" hint="Or type the issuer and symbol by hand below.">
        <AssetPicker assets={knownAssets} onSelect={(issuer, symbol) => { setIssuerId(issuer); setAssetName(symbol) }} />
      </Field>
      <Field label="Token's issuer (identity)">
        <TextInput value={issuerId} onChange={(e) => setIssuerId(e.target.value)} placeholder="60-char identity" />
      </Field>
      <Field label="Token symbol (1-7 chars)">
        <TextInput value={assetName} onChange={(e) => setAssetName(e.target.value)} maxLength={7} placeholder="MYTOKEN" />
      </Field>
      <Field label="Price per share">
        <AmountInput value={price} onChange={setPrice} />
      </Field>
      <Field label="Shares">
        <AmountInput value={shares} onChange={setShares} />
      </Field>
      <Button variant="primary" loading={busy} disabled={!issuerId || !assetName || !price || !shares} onClick={() => void submit()}>
        Place {side}
      </Button>
      <ErrorText>{error}</ErrorText>
      <SuccessText>{result}</SuccessText>
    </div>
  )
}

function MyOrders() {
  const { wallet } = useWallet()
  const [asks, setAsks] = useState<EntityOrder[] | null>(null)
  const [bids, setBids] = useState<EntityOrder[] | null>(null)
  const check = useAction<void>()
  const cancel = useAction<string>()

  if (!wallet) return null

  async function load() {
    await check.run(async () => {
      setAsks(await getEntityAskOrders(wallet!.identity))
      setBids(await getEntityBidOrders(wallet!.identity))
    })
  }

  async function doCancel(o: EntityOrder, side: 'ask' | 'bid') {
    await cancel.run(async () => {
      const payload =
        side === 'ask'
          ? payQxRemoveFromAskOrder(o.issuer, o.assetName, o.price, o.numberOfShares)
          : payQxRemoveFromBidOrder(o.issuer, o.assetName, o.price, o.numberOfShares)
      const r = await submitProcedureCall(wallet!, QX, side === 'ask' ? QX_P_REMOVE_FROM_ASK_ORDER : QX_P_REMOVE_FROM_BID_ORDER, 0, payload)
      await load()
      return `Submitted at tick ${r.tick} -- refresh above to confirm it's actually gone (a bid's locked QU comes back automatically).`
    })
  }

  return (
    <div className="space-y-2">
      <Button loading={check.busy} onClick={() => void load()}>Check my orders</Button>
      <ErrorText>{check.error}</ErrorText>
      {asks && (
        <div className="space-y-1">
          <div className="text-xs font-semibold">My asks ({asks.length})</div>
          {asks.map((o, i) => (
            <div key={i} className="flex items-center justify-between gap-2 text-xs border-b border-border/60 py-0.5">
              <span className="space-x-2">
                <span className="font-semibold">{o.assetNameStr}</span>
                <span>{o.numberOfShares.toString()} @ {o.price.toString()}</span>
                <Identity id={o.issuer} label="issuer" />
              </span>
              <Button size="sm" disabled={cancel.busy} onClick={() => void doCancel(o, 'ask')}>
                cancel
              </Button>
            </div>
          ))}
        </div>
      )}
      {bids && (
        <div className="space-y-1">
          <div className="text-xs font-semibold">My bids ({bids.length})</div>
          {bids.map((o, i) => (
            <div key={i} className="flex items-center justify-between gap-2 text-xs border-b border-border/60 py-0.5">
              <span className="space-x-2">
                <span className="font-semibold">{o.assetNameStr}</span>
                <span>{o.numberOfShares.toString()} @ {o.price.toString()}</span>
                <Identity id={o.issuer} label="issuer" />
              </span>
              <Button size="sm" disabled={cancel.busy} onClick={() => void doCancel(o, 'bid')}>
                cancel
              </Button>
            </div>
          ))}
        </div>
      )}
      <ErrorText>{cancel.error}</ErrorText>
      <SuccessText>{cancel.result}</SuccessText>
    </div>
  )
}

function OrderBookViewer({ knownAssets }: { knownAssets: KnownAsset[] }) {
  const [issuerId, setIssuerId] = useState('')
  const [assetName, setAssetName] = useState('')
  const [asks, setAsks] = useState<AssetOrder[] | null>(null)
  const [bids, setBids] = useState<AssetOrder[] | null>(null)
  const { busy, error, run } = useAction<void>()

  async function load() {
    await run(async () => {
      const name = nameToU64(assetName)
      setAsks(await getAssetAskOrders(issuerId, name))
      setBids(await getAssetBidOrders(issuerId, name))
    })
  }

  return (
    <div className="space-y-2">
      <Field label="Find a token" hint="Or type the issuer and symbol by hand below.">
        <AssetPicker assets={knownAssets} onSelect={(issuer, symbol) => { setIssuerId(issuer); setAssetName(symbol) }} />
      </Field>
      <Field label="Token's issuer (identity)">
        <TextInput value={issuerId} onChange={(e) => setIssuerId(e.target.value)} placeholder="60-char identity" />
      </Field>
      <Field label="Token symbol (1-7 chars)">
        <TextInput value={assetName} onChange={(e) => setAssetName(e.target.value)} maxLength={7} placeholder="MYTOKEN" />
      </Field>
      <Button loading={busy} disabled={!issuerId || !assetName} onClick={() => void load()}>
        Show order book
      </Button>
      <ErrorText>{error}</ErrorText>
      {asks && bids && (
        <div className="grid sm:grid-cols-2 gap-2">
          <div>
            <div className="text-xs font-semibold">Asks ({asks.length})</div>
            {asks.map((o, i) => <KV key={i} k={<Identity id={o.entity} />} v={`${o.numberOfShares} @ ${o.price}`} />)}
          </div>
          <div>
            <div className="text-xs font-semibold">Bids ({bids.length})</div>
            {bids.map((o, i) => <KV key={i} k={<Identity id={o.entity} />} v={`${o.numberOfShares} @ ${o.price}`} />)}
          </div>
        </div>
      )}
    </div>
  )
}

export function QxMarket() {
  const [listings, setListings] = useState<AssetListing[]>([])
  const [knownAssets, setKnownAssets] = useState<KnownAsset[]>([])
  const [query, setQuery] = useState('')
  const [scanLimit, setScanLimit] = useState(60)
  const [busy, setBusy] = useState(false)
  const [loadedOnce, setLoadedOnce] = useState(false)
  const [error, setError] = useState<string | null>(null)

  async function load() {
    setBusy(true)
    setError(null)
    try {
      const knownCatalog = await scanKnownAssetCatalog()
      setKnownAssets(knownCatalog)
      const holdings = await scanAllHoldings(scanLimit)
      const candidates = [
        ...holdings.map(({ holding: h }) => ({ issuer: h.issuer, assetName: h.assetName, assetNameStr: h.assetNameStr })),
        ...knownCatalog.map((a) => ({ issuer: a.issuer, assetName: a.assetName, assetNameStr: a.assetNameStr })),
      ]
      setListings(await scanAssetAsks(candidates))
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

  const askedAssetKeys = useMemo(() => new Set(listings.map((l) => `${l.issuer}:${l.assetName}`)), [listings])
  const filteredKnown = useMemo(() => {
    const q = query.trim().toUpperCase()
    return q ? knownAssets.filter((a) => a.assetNameStr.toUpperCase().includes(q)) : knownAssets
  }, [knownAssets, query])

  return (
    <div className="space-y-4">
      <Card
        title="QX -- the raw exchange"
        icon={<IconCoins />}
        right={
          <div className="flex gap-2">
            <Button size="sm" loading={busy} onClick={() => void load()}>rescan</Button>
            <Button size="sm" variant="ghost" onClick={() => setScanLimit((n) => n + 60)}>+60 holding slots</Button>
          </div>
        }
      >
        <div className="text-xs text-muted-foreground">
          QX is a real contract on this chain, separate from QPoolK6 -- any wallet can trade any token directly here,
          independent of pools. Scanning the first {scanLimit} holding slots for live resting asks, plus every token
          known to exist on this testnet whether or not it's ever touched a pool.
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

      <Card title={`Resting asks (${listings.length})`} icon={<IconBook />} iconTone="info">
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
            hint={query ? "Nothing issued under that name among this testnet's known wallets." : undefined}
          />
        ) : (
          <div>
            {filteredKnown.map((a, i) => (
              <KnownAssetRow key={i} asset={a} hasLiveAsk={askedAssetKeys.has(`${a.issuer}:${a.assetName}`)} />
            ))}
          </div>
        )}
      </Card>

      <Card title="Place an order on QX" icon={<IconPlus />} iconTone="info">
        <div className="text-xs text-muted-foreground">
          An ask needs the shares already self-held under QX's own management (not handed to QPoolK6 or any other
          contract); a bid locks the full price &times; shares as payment until filled or cancelled.
        </div>
        <PlaceOrderForm knownAssets={knownAssets} />
      </Card>
      <Card title="My orders" icon={<IconList />} iconTone="neutral">
        <MyOrders />
      </Card>
      <Card title="Order book for a token" icon={<IconBook />} iconTone="info">
        <OrderBookViewer knownAssets={knownAssets} />
      </Card>
    </div>
  )
}
