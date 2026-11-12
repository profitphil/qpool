import type { Holding } from '../qubic/decode'
import { useWallet } from '../context/WalletContext'
import { submitProcedureCall } from '../qubic/transaction'
import { payClaimPayout } from '../qubic/encode'
import { QPOOLK6, P_CLAIM_PAYOUT } from '../qubic/config'
import { useAction } from '../hooks/useAction'
import { Button, ErrorText, KV, SuccessText } from './ui'
import { OfferPositionForm } from './OfferPositionForm'
import { MyPositionPanel } from './MyPositionPanel'
import { RebidIpoForm } from './RebidIpoForm'

export function HoldingRow({ id, holding, onChanged }: { id: number; holding: Holding; onChanged?: () => void }) {
  const { wallet, refreshBalance } = useWallet()
  const { busy, error, result, run } = useAction<string>()
  const isIpo = holding.ipoContractIndex > 0n

  async function claim() {
    if (!wallet) return
    await run(async () => {
      const r = await submitProcedureCall(wallet, QPOOLK6, P_CLAIM_PAYOUT, 0, payClaimPayout(id))
      await refreshBalance()
      onChanged?.()
      return `Submitted at tick ${r.tick} -- Qubic has no transaction receipts, so check your balance/this holding's numbers above to confirm it actually paid out (a holding with nothing newly owed submits fine but changes nothing).`
    })
  }

  return (
    <div className="border border-border rounded p-2 space-y-1">
      <div className="text-xs font-semibold">Holding #{id}</div>
      <KV k="token" v={holding.assetNameStr || 'pool shares'} />
      <KV k="issued by" v={holding.issuer} />
      <KV k="shares held" v={holding.shares.toString()} />
      {holding.pendingShares > 0n && <KV k="pending (not settled yet)" v={holding.pendingShares.toString()} />}
      <KV k="original cost" v={holding.costBasis.toString()} />
      {holding.askShares > 0n && <KV k="resting sale on QX (shares / price)" v={`${holding.askShares} / ${holding.askPrice}`} />}
      <KV k="claimable payout" v={holding.totalPayouts.toString()} />
      {holding.active === 0n && <KV k="status" v="retired" />}
      {isIpo && (
        <>
          <KV k="IPO target (contract index)" v={holding.ipoContractIndex.toString()} />
          <KV k="IPO settled" v={holding.ipoSettled > 0n ? 'yes' : 'not yet'} />
          <KV k="current bid / seats available / at risk if outbid" v={`${holding.ipoMaxPrice} / ${holding.ipoAvailable} / ${holding.ipoAtRisk}`} />
        </>
      )}
      {wallet && holding.totalPayouts > 0n && (
        <Button disabled={busy} onClick={() => void claim()}>
          {busy ? 'claiming...' : 'Claim payout'}
        </Button>
      )}
      <ErrorText>{error}</ErrorText>
      <SuccessText>{result}</SuccessText>
      {isIpo && <RebidIpoForm holdingId={id} onChanged={onChanged} />}
      <OfferPositionForm holdingId={id} onChanged={onChanged} />
      <MyPositionPanel holdingId={id} onChanged={onChanged} />
    </div>
  )
}
