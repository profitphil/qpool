import { useState } from 'react'
import { useWallet } from '../context/WalletContext'
import { submitProcedureCall } from '../qubic/transaction'
import { payContribute } from '../qubic/encode'
import { QPOOLK6, P_CONTRIBUTE, MIN_CONTRIBUTION } from '../qubic/config'
import { useAction } from '../hooks/useAction'
import { Button, ErrorText, Field, SuccessText, TextInput } from './ui'

export function ContributeForm({ poolId, onChanged }: { poolId: number; onChanged?: () => void }) {
  const { wallet, refreshBalance } = useWallet()
  const [amount, setAmount] = useState('')
  const { busy, error, result, run } = useAction<string>()

  if (!wallet) return null

  async function submit() {
    await run(async () => {
      const amt = BigInt(amount)
      const r = await submitProcedureCall(wallet!, QPOOLK6, P_CONTRIBUTE, amt, payContribute(poolId))
      await refreshBalance()
      onChanged?.()
      return `Submitted at tick ${r.tick} -- check the pool's uncommittedTotal above to confirm it actually landed.`
    })
  }

  return (
    <div className="space-y-2">
      <div className="text-xs text-muted-foreground">
        There's no separate "join" step -- contributing QU is how you become a member of this pool. The first time
        costs a small one-time join fee on top of your contribution; after that you're already a member.
      </div>
      <Field label={`Amount to contribute (min ${MIN_CONTRIBUTION.toLocaleString()} QU)`}>
        <TextInput value={amount} onChange={(e) => setAmount(e.target.value)} placeholder="1000000" inputMode="numeric" />
      </Field>
      <Button variant="primary" disabled={busy || !amount} onClick={() => void submit()}>
        {busy ? 'submitting...' : 'Join / contribute'}
      </Button>
      <ErrorText>{error}</ErrorText>
      <SuccessText>{result}</SuccessText>
    </div>
  )
}
