import { useEffect, useState } from 'react'
import { useWallet } from '../context/WalletContext'
import { submitProcedureCall } from '../qubic/transaction'
import { payContribute, payJoinPool, payWithdrawUncommitted } from '../qubic/encode'
import { getMember, type Member } from '../qubic/decode'
import { QPOOLK6, P_CONTRIBUTE, P_JOIN_POOL, P_WITHDRAW_UNCOMMITTED, MIN_CONTRIBUTION, JOIN_FEE } from '../qubic/config'
import { useAction } from '../hooks/useAction'
import { AmountInput, Button, ErrorText, Field, KV, SuccessText } from './ui'

export function ContributeForm({ poolId, onChanged }: { poolId: number; onChanged?: () => void }) {
  const { wallet, refreshBalance } = useWallet()
  const [amount, setAmount] = useState('')
  const [withdrawAmount, setWithdrawAmount] = useState('')
  const [member, setMember] = useState<Member | null>(null)
  const join = useAction<string>()
  const contribute = useAction<string>()
  const withdraw = useAction<string>()

  async function loadMember() {
    if (!wallet) return
    try {
      const m = await getMember(poolId, wallet.identity)
      setMember(m.found === 1 ? m : null)
    } catch {
      setMember(null)
    }
  }

  useEffect(() => {
    setMember(null)
    void loadMember()
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [poolId, wallet])

  if (!wallet) return null

  async function doJoin() {
    await join.run(async () => {
      const r = await submitProcedureCall(wallet!, QPOOLK6, P_JOIN_POOL, JOIN_FEE, payJoinPool(poolId))
      await refreshBalance()
      await loadMember()
      onChanged?.()
      return `Submitted at tick ${r.tick} -- check above to confirm you're now listed as a member (contributing is a separate step, whenever you're ready).`
    })
  }

  async function doContribute() {
    await contribute.run(async () => {
      const r = await submitProcedureCall(wallet!, QPOOLK6, P_CONTRIBUTE, BigInt(amount), payContribute(poolId))
      await refreshBalance()
      await loadMember()
      onChanged?.()
      return `Submitted at tick ${r.tick} -- check the pool's uncommittedTotal above to confirm it actually landed.`
    })
  }

  async function doWithdraw() {
    await withdraw.run(async () => {
      const r = await submitProcedureCall(wallet!, QPOOLK6, P_WITHDRAW_UNCOMMITTED, 0, payWithdrawUncommitted(poolId, BigInt(withdrawAmount)))
      await refreshBalance()
      await loadMember()
      setWithdrawAmount('')
      onChanged?.()
      return `Submitted at tick ${r.tick} -- check your wallet balance above to confirm it actually landed.`
    })
  }

  if (member === null) {
    return (
      <div className="space-y-2">
        <div className="text-xs text-muted-foreground">
          You're not a member of this pool yet. Joining costs a one-time {JOIN_FEE.toLocaleString()} QU fee and
          doesn't commit any contribution -- once you're a member, contributing is a separate step whenever you're
          ready.
        </div>
        <Button variant="primary" loading={join.busy} onClick={() => void doJoin()}>
          Join pool ({JOIN_FEE.toLocaleString()} QU fee)
        </Button>
        <ErrorText>{join.error}</ErrorText>
        <SuccessText>{join.result}</SuccessText>
      </div>
    )
  }

  const available = member.locked > member.uncommitted ? 0n : member.uncommitted - member.locked

  return (
    <div className="space-y-3">
      <div className="grid grid-cols-2 gap-x-4 text-xs">
        <KV k="your uncommitted" v={`${member.uncommitted.toLocaleString()} QU`} />
        <KV k="your committed" v={`${member.committed.toLocaleString()} QU`} />
        {member.locked > 0n && <KV k="locked for an open vote" v={`${member.locked.toLocaleString()} QU`} />}
      </div>

      <div className="space-y-2">
        <Field label={`Amount to contribute (min ${MIN_CONTRIBUTION.toLocaleString()} QU)`}>
          <AmountInput value={amount} onChange={setAmount} placeholder="1000000" />
        </Field>
        <Button variant="primary" loading={contribute.busy} disabled={!amount} onClick={() => void doContribute()}>
          Contribute QU to pool
        </Button>
        <ErrorText>{contribute.error}</ErrorText>
        <SuccessText>{contribute.result}</SuccessText>
      </div>

      <div className="space-y-2 border-t border-border pt-2">
        <div className="text-xs text-muted-foreground">
          {available > 0n
            ? `Up to ${available.toLocaleString()} QU available to withdraw (not locked for an open vote).`
            : member.uncommitted > 0n
              ? "Your uncommitted QU is locked for an open vote right now -- try again once it's resolved."
              : "Nothing uncommitted to withdraw -- once QU is committed to a holding, the only way out is selling your seat on the Seat Market below."}
        </div>
        {available > 0n && (
          <>
            <Field label="Amount to withdraw">
              <AmountInput value={withdrawAmount} onChange={setWithdrawAmount} placeholder={available.toString()} />
            </Field>
            <Button loading={withdraw.busy} disabled={!withdrawAmount} onClick={() => void doWithdraw()}>
              Withdraw uncommitted QU
            </Button>
            <ErrorText>{withdraw.error}</ErrorText>
            <SuccessText>{withdraw.result}</SuccessText>
          </>
        )}
      </div>
    </div>
  )
}
