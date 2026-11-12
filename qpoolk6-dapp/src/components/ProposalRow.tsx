import { useState } from 'react'
import type { Proposal } from '../qubic/decode'
import { useWallet } from '../context/WalletContext'
import { submitProcedureCall } from '../qubic/transaction'
import { payVote, payTally, payExecute } from '../qubic/encode'
import { QPOOLK6, P_VOTE, P_TALLY, P_EXECUTE, PROP_STATUS } from '../qubic/config'
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

  return (
    <div className="border border-border rounded p-2 space-y-1">
      <div className="text-xs font-semibold">Proposal #{id} -- {statusName}</div>
      <KV k="kind" v={proposal.kind.toString()} />
      <KV k="proposer" v={proposal.proposer} />
      <KV k="asset/issuer" v={`${proposal.assetNameStr || '(none)'} / ${proposal.issuer}`} />
      <KV k="quAmount" v={proposal.quAmount.toString()} />
      <KV k="shares" v={proposal.shares.toString()} />
      <KV k="price" v={proposal.price.toString()} />
      <KV k="yes/no/eligible" v={`${proposal.yesWeight} / ${proposal.noWeight} / ${proposal.eligibleWeight}`} />
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
