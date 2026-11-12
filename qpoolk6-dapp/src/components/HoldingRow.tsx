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
      <KV k="asset" v={holding.assetNameStr || '(contract shares)'} />
      <KV k="issuer" v={holding.issuer} />
      <KV k="shares" v={holding.shares.toString()} />
      <KV k="pendingShares" v={holding.pendingShares.toString()} />
      <KV k="costBasis" v={holding.costBasis.toString()} />
      <KV k="askShares / askPrice" v={`${holding.askShares} / ${holding.askPrice}`} />
      <KV k="totalPayouts (claimable)" v={holding.totalPayouts.toString()} />
      <KV k="active" v={holding.active.toString()} />
      {isIpo && (
        <>
          <KV k="ipoContractIndex" v={holding.ipoContractIndex.toString()} />
          <KV k="ipoSettled" v={holding.ipoSettled.toString()} />
          <KV k="ipoMaxPrice / ipoAvailable / ipoAtRisk" v={`${holding.ipoMaxPrice} / ${holding.ipoAvailable} / ${holding.ipoAtRisk}`} />
        </>
      )}
      {wallet && holding.totalPayouts > 0n && (
        <Button disabled={busy} onClick={() => void claim()}>
          {busy ? 'claiming...' : 'ClaimPayout'}
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
