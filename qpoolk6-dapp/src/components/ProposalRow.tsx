import { useState } from 'react'
import type { Proposal } from '../qubic/decode'
import { useWallet } from '../context/WalletContext'
import { submitProcedureCall } from '../qubic/transaction'
import { payVote, payTally, payExecute } from '../qubic/encode'
import { QPOOLK6, P_VOTE, P_TALLY, P_EXECUTE, PROP_STATUS, KIND_NAMES, KIND_ALLOCATE, KIND_BUY_IPO, KIND_BUY_QSWAP, KIND_SELL_QSWAP } from '../qubic/config'
import { useAction } from '../hooks/useAction'
import { Button, ErrorText, KV, SuccessText } from './ui'

export function ProposalRow({ id, proposal, onChanged }: { id: number; proposal: Proposal; onChanged?: () => void }) {
  const { wallet } = useWallet()
  const { busy, error, result, run } = useAction<string>()
  const [approve, setApprove] = useState(true)

  function act(fn: () => Promise<string>) {
    void run(async () => {
      const r = await fn()
      onChanged?.()
      return r
    })
  }

  const statusName = PROP_STATUS[Number(proposal.status)] ?? `UNKNOWN(${proposal.status})`
  const kindName = KIND_NAMES[Number(proposal.kind)] ?? `kind ${proposal.kind}`
  const kindNum = Number(proposal.kind)
  const showAsset = kindNum === KIND_BUY_QSWAP || kindNum === KIND_SELL_QSWAP // others: no token/contract-shares, or IPO's name isn't known yet
  const showShares = kindNum !== KIND_ALLOCATE && kindNum !== KIND_BUY_IPO
  const showPrice = kindNum !== KIND_BUY_QSWAP && kindNum !== KIND_SELL_QSWAP && kindNum !== KIND_ALLOCATE

  return (
    <div className="border border-border rounded p-2 space-y-1">
      <div className="text-xs font-semibold">
        Proposal #{id} -- {kindName} -- {statusName}
      </div>
      <KV k="proposer" v={proposal.proposer} />
      {showAsset && <KV k="token" v={`${proposal.assetNameStr || '(none)'} / ${proposal.issuer}`} />}
      <KV k="budget / minimum proceeds" v={proposal.quAmount.toString()} />
      {showShares && <KV k="shares" v={proposal.shares.toString()} />}
      {showPrice && <KV k="price" v={proposal.price.toString()} />}
      <KV k="yes / no / eligible votes" v={`${proposal.yesWeight} / ${proposal.noWeight} / ${proposal.eligibleWeight}`} />
      <KV k="openTick" v={proposal.openTick.toString()} />
      {wallet && (
        <div className="flex gap-2 items-center flex-wrap pt-1">
          <select value={approve ? '1' : '0'} onChange={(e) => setApprove(e.target.value === '1')} className="text-xs border rounded">
            <option value="1">YES</option>
            <option value="0">NO</option>
          </select>
          <Button
            disabled={busy}
            onClick={() =>
              act(async () => {
                const r = await submitProcedureCall(wallet, QPOOLK6, P_VOTE, 0, payVote(id, approve))
                return `Submitted at tick ${r.tick} -- check yesWeight/noWeight above to confirm it actually counted (e.g. refused if the window already closed).`
              })
            }
          >
            Vote
          </Button>
          <Button
            disabled={busy}
            onClick={() =>
              act(async () => {
                const r = await submitProcedureCall(wallet, QPOOLK6, P_TALLY, 0, payTally(id))
                return `Submitted at tick ${r.tick} -- check the status above (PASSED/REJECTED) to confirm.`
              })
            }
          >
            Tally
          </Button>
          <Button
            disabled={busy}
            onClick={() =>
              act(async () => {
                const r = await submitProcedureCall(wallet, QPOOLK6, P_EXECUTE, 0, payExecute(id))
                return `Submitted at tick ${r.tick} -- check the status above (EXECUTED means it worked) to confirm.`
              })
            }
          >
            Execute
          </Button>
        </div>
      )}
      <ErrorText>{error}</ErrorText>
      <SuccessText>{result}</SuccessText>
    </div>
  )
}
