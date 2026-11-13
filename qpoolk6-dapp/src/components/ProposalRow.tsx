import { useState } from 'react'
import type { Proposal } from '../qubic/decode'
import { useWallet } from '../context/WalletContext'
import { submitProcedureCall } from '../qubic/transaction'
import { payVote, payTally, payExecute } from '../qubic/encode'
import {
  QPOOLK6, P_VOTE, P_TALLY, P_EXECUTE, PROP_STATUS, KIND_NAMES, KIND_ALLOCATE, KIND_BUY_IPO, KIND_BUY_QSWAP,
  KIND_SELL_QSWAP, IPO_TARGETS, VOTE_WINDOW, IPO_VOTE_WINDOW, EXECUTE_WINDOW, TICKS_PER_SECOND_ESTIMATE,
} from '../qubic/config'
import { useAction } from '../hooks/useAction'
import { Badge, Button, ErrorText, KV, SuccessText } from './ui'

function formatCountdown(ticksLeft: number): string {
  if (ticksLeft <= 0) return 'any moment now'
  const seconds = ticksLeft / TICKS_PER_SECOND_ESTIMATE
  const m = Math.floor(seconds / 60)
  const s = Math.floor(seconds % 60)
  return m > 0 ? `~${m}m ${s}s` : `~${s}s`
}

export function ProposalRow({
  id, proposal, currentTick, onChanged,
}: { id: number; proposal: Proposal; currentTick?: number | null; onChanged?: () => void }) {
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
  const statusNum = Number(proposal.status)
  const showAsset = kindNum === KIND_BUY_QSWAP || kindNum === KIND_SELL_QSWAP // others: no token/contract-shares, or IPO's name isn't known yet
  const showShares = kindNum !== KIND_ALLOCATE && kindNum !== KIND_BUY_IPO
  const showPrice = kindNum !== KIND_BUY_QSWAP && kindNum !== KIND_SELL_QSWAP && kindNum !== KIND_ALLOCATE
  // BUY_IPO repurposes holdingId to carry the target's own contract index (it
  // isn't a real holding -- there's nothing to hold until the IPO settles).
  const ipoTargetIndex = kindNum === KIND_BUY_IPO ? Number(proposal.holdingId) : null
  const ipoTarget = ipoTargetIndex === null ? null : IPO_TARGETS.find((t) => t.index === ipoTargetIndex)
  const isLive = statusNum === 0 || statusNum === 1

  // Vote/execute windows are measured from openTick, not from when Tally is
  // called -- Tally only ever reads a boundary that already passed on its
  // own (see QPoolK6.h's Tally: it refuses with WINDOW_OPEN before this
  // point, same arithmetic). passedTick (used for the execute window) isn't
  // exposed by GetProposal, but Tally always sets it to exactly openTick +
  // window, so it's reproducible here without a contract change.
  const voteWindow = kindNum === KIND_BUY_IPO ? IPO_VOTE_WINDOW : VOTE_WINDOW
  const voteEndTick = Number(proposal.openTick) + voteWindow
  const executeEndTick = voteEndTick + EXECUTE_WINDOW
  const ticksLeft =
    currentTick == null ? null : statusNum === 0 ? voteEndTick - currentTick : statusNum === 1 ? executeEndTick - currentTick : null

  return (
    <div className="border border-border rounded p-2 space-y-1">
      <div className="text-xs font-semibold">
        Proposal #{id} -- {kindName} -- {statusName}
      </div>
      {ticksLeft !== null && (
        <div className="text-[11px]">
          {statusNum === 0 ? (
            ticksLeft > 0 ? (
              <Badge tone="warning" dot>voting closes in {formatCountdown(ticksLeft)}</Badge>
            ) : (
              <Badge tone="info" dot>voting closed -- call Tally</Badge>
            )
          ) : (
            ticksLeft > 0 ? (
              <Badge tone="warning" dot>execute within {formatCountdown(ticksLeft)} (operator only)</Badge>
            ) : (
              <Badge tone="danger" dot>execute window expired -- will mark EXPIRED</Badge>
            )
          )}
        </div>
      )}
      <KV k="proposer" v={proposal.proposer} />
      {ipoTargetIndex !== null && (
        <KV k="IPO target (contract index)" v={ipoTarget ? `${ipoTarget.name} (index ${ipoTarget.index})` : `index ${ipoTargetIndex}`} />
      )}
      {showAsset && <KV k="token" v={`${proposal.assetNameStr || '(none)'} / ${proposal.issuer}`} />}
      <KV k="budget / minimum proceeds" v={proposal.quAmount.toString()} />
      {showShares && <KV k="shares" v={proposal.shares.toString()} />}
      {showPrice && <KV k="price" v={proposal.price.toString()} />}
      <KV k="yes / no / eligible votes" v={`${proposal.yesWeight} / ${proposal.noWeight} / ${proposal.eligibleWeight}`} />
      <KV k="openTick" v={proposal.openTick.toString()} />
      {wallet && isLive && (
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
                return `Submitted at tick ${r.tick} -- check the status above (PASSED/REJECTED) to confirm. Refused with no visible error if the voting window is still open.`
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
                return `Submitted at tick ${r.tick} -- check the status above (EXECUTED means it worked). Only the pool's creator or an admin may execute -- any other wallet is refused with no visible error.`
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
