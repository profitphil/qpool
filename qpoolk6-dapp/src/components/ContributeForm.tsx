import { useEffect, useState } from 'react'
import { useWallet } from '../context/WalletContext'
import { submitProcedureCall } from '../qubic/transaction'
import { payContribute, payJoinPool } from '../qubic/encode'
import { getMember } from '../qubic/decode'
import { QPOOLK6, P_CONTRIBUTE, P_JOIN_POOL, MIN_CONTRIBUTION, JOIN_FEE } from '../qubic/config'
import { useAction } from '../hooks/useAction'
import { AmountInput, Button, ErrorText, Field, SuccessText } from './ui'

export function ContributeForm({ poolId, onChanged }: { poolId: number; onChanged?: () => void }) {
  const { wallet, refreshBalance } = useWallet()
  const [amount, setAmount] = useState('')
  const [isMember, setIsMember] = useState<boolean | null>(null)
  const join = useAction<string>()
  const contribute = useAction<string>()

  useEffect(() => {
    if (!wallet) return
    setIsMember(null)
    getMember(poolId, wallet.identity)
      .then((m) => setIsMember(m.found === 1))
      .catch(() => setIsMember(null))
  }, [poolId, wallet])

  if (!wallet) return null

  async function doJoin() {
    await join.run(async () => {
      const r = await submitProcedureCall(wallet!, QPOOLK6, P_JOIN_POOL, JOIN_FEE, payJoinPool(poolId))
      await refreshBalance()
      setIsMember(true)
      onChanged?.()
      return `Submitted at tick ${r.tick} -- check above to confirm you're now listed as a member (contributing is a separate step, whenever you're ready).`
    })
  }

  async function doContribute() {
    await contribute.run(async () => {
      const r = await submitProcedureCall(wallet!, QPOOLK6, P_CONTRIBUTE, BigInt(amount), payContribute(poolId))
      await refreshBalance()
      onChanged?.()
      return `Submitted at tick ${r.tick} -- check the pool's uncommittedTotal above to confirm it actually landed.`
    })
  }

  if (isMember === false) {
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

  return (
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
  )
}
