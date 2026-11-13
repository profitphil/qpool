import { useEffect, useState } from 'react'
import { useWallet } from '../context/WalletContext'
import { submitProcedureCall } from '../qubic/transaction'
import { payContribute } from '../qubic/encode'
import { getMember } from '../qubic/decode'
import { QPOOLK6, P_CONTRIBUTE, MIN_CONTRIBUTION, JOIN_FEE } from '../qubic/config'
import { useAction } from '../hooks/useAction'
import { Button, ErrorText, Field, SuccessText, TextInput } from './ui'

export function ContributeForm({ poolId, onChanged }: { poolId: number; onChanged?: () => void }) {
  const { wallet, refreshBalance } = useWallet()
  const [amount, setAmount] = useState('')
  const [isMember, setIsMember] = useState<boolean | null>(null)
  const { busy, error, result, run } = useAction<string>()

  useEffect(() => {
    if (!wallet) return
    setIsMember(null)
    getMember(poolId, wallet.identity)
      .then((m) => setIsMember(m.found === 1))
      .catch(() => setIsMember(null))
  }, [poolId, wallet])

  if (!wallet) return null

  async function submit() {
    await run(async () => {
      const amt = BigInt(amount)
      const r = await submitProcedureCall(wallet!, QPOOLK6, P_CONTRIBUTE, amt, payContribute(poolId))
      await refreshBalance()
      setIsMember(true)
      onChanged?.()
      return `Submitted at tick ${r.tick} -- check the pool's uncommittedTotal above to confirm it actually landed.`
    })
  }

  if (isMember === false) {
    return (
      <div className="space-y-2">
        <div className="text-xs text-muted-foreground">
          You're not a member of this pool yet. Joining is a one-time contribution with a small join fee
          ({JOIN_FEE.toLocaleString()} QU) on top -- after this you're a member and can contribute freely.
        </div>
        <Field label={`Amount to join with (min ${MIN_CONTRIBUTION.toLocaleString()} QU, plus the join fee)`}>
          <TextInput value={amount} onChange={(e) => setAmount(e.target.value)} placeholder="1000000" inputMode="numeric" />
        </Field>
        <Button variant="primary" loading={busy} disabled={!amount} onClick={() => void submit()}>
          Join pool
        </Button>
        <ErrorText>{error}</ErrorText>
        <SuccessText>{result}</SuccessText>
      </div>
    )
  }

  return (
    <div className="space-y-2">
      <Field label={`Amount to contribute (min ${MIN_CONTRIBUTION.toLocaleString()} QU)`}>
        <TextInput value={amount} onChange={(e) => setAmount(e.target.value)} placeholder="1000000" inputMode="numeric" />
      </Field>
      <Button variant="primary" loading={busy} disabled={!amount} onClick={() => void submit()}>
        Contribute QU to pool
      </Button>
      <ErrorText>{error}</ErrorText>
      <SuccessText>{result}</SuccessText>
    </div>
  )
}
