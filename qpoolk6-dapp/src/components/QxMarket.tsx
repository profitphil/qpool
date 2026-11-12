import { useState } from 'react'
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
import { QX, QX_P_ADD_TO_ASK_ORDER, QX_P_ADD_TO_BID_ORDER, QX_P_REMOVE_FROM_ASK_ORDER, QX_P_REMOVE_FROM_BID_ORDER } from '../qubic/config'
import { useAction } from '../hooks/useAction'
import { Button, Card, ErrorText, Field, KV, Select, SuccessText, TextInput } from './ui'

function PlaceOrderForm() {
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
      <Field label="Token's issuer (identity)">
        <TextInput value={issuerId} onChange={(e) => setIssuerId(e.target.value)} placeholder="60-char identity" />
      </Field>
      <Field label="Token symbol (1-7 chars)">
        <TextInput value={assetName} onChange={(e) => setAssetName(e.target.value)} maxLength={7} placeholder="MYTOKEN" />
      </Field>
      <Field label="Price per share">
        <TextInput value={price} onChange={(e) => setPrice(e.target.value)} inputMode="numeric" />
      </Field>
      <Field label="Shares">
        <TextInput value={shares} onChange={(e) => setShares(e.target.value)} inputMode="numeric" />
      </Field>
      <Button variant="primary" disabled={busy || !issuerId || !assetName || !price || !shares} onClick={() => void submit()}>
        {busy ? 'submitting...' : `Place ${side}`}
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
      <Button disabled={check.busy} onClick={() => void load()}>
        {check.busy ? 'checking...' : 'Check my orders'}
      </Button>
      <ErrorText>{check.error}</ErrorText>
      {asks && (
        <div className="space-y-1">
          <div className="text-xs font-semibold">My asks ({asks.length})</div>
          {asks.map((o, i) => (
            <div key={i} className="flex items-center justify-between gap-2 text-xs border-b border-border/60 py-0.5">
              <span>
                {o.assetNameStr} -- {o.numberOfShares.toString()} @ {o.price.toString()}
              </span>
              <Button disabled={cancel.busy} onClick={() => void doCancel(o, 'ask')}>
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
              <span>
                {o.assetNameStr} -- {o.numberOfShares.toString()} @ {o.price.toString()}
              </span>
              <Button disabled={cancel.busy} onClick={() => void doCancel(o, 'bid')}>
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

function OrderBookViewer() {
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
      <Field label="Token's issuer (identity)">
        <TextInput value={issuerId} onChange={(e) => setIssuerId(e.target.value)} placeholder="60-char identity" />
      </Field>
      <Field label="Token symbol (1-7 chars)">
        <TextInput value={assetName} onChange={(e) => setAssetName(e.target.value)} maxLength={7} placeholder="MYTOKEN" />
      </Field>
      <Button disabled={busy || !issuerId || !assetName} onClick={() => void load()}>
        {busy ? 'loading...' : 'Show order book'}
      </Button>
      <ErrorText>{error}</ErrorText>
      {asks && bids && (
        <div className="grid sm:grid-cols-2 gap-2">
          <div>
            <div className="text-xs font-semibold">Asks ({asks.length})</div>
            {asks.map((o, i) => <KV key={i} k={o.entity.slice(0, 10) + '...'} v={`${o.numberOfShares} @ ${o.price}`} />)}
          </div>
          <div>
            <div className="text-xs font-semibold">Bids ({bids.length})</div>
            {bids.map((o, i) => <KV key={i} k={o.entity.slice(0, 10) + '...'} v={`${o.numberOfShares} @ ${o.price}`} />)}
          </div>
        </div>
      )}
    </div>
  )
}

export function QxMarket() {
  return (
    <div className="space-y-4">
      <Card title="Place an order on QX">
        <div className="text-xs text-muted-foreground">
          QX is a real contract on this chain, separate from QPoolK6 -- any wallet can trade any token directly here,
          independent of pools. An ask needs the shares already self-held under QX's own management (not handed to
          QPoolK6 or any other contract); a bid locks the full price &times; shares as payment until filled or cancelled.
        </div>
        <PlaceOrderForm />
      </Card>
      <Card title="My orders">
        <MyOrders />
      </Card>
      <Card title="Order book for a token">
        <OrderBookViewer />
      </Card>
    </div>
  )
}
