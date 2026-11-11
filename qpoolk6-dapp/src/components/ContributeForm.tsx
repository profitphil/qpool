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
      <Field label={`Contribute QU to pool #${poolId} (min ${MIN_CONTRIBUTION.toLocaleString()})`}>
        <TextInput value={amount} onChange={(e) => setAmount(e.target.value)} placeholder="1000000" inputMode="numeric" />
      </Field>
      <Button disabled={busy || !amount} onClick={() => void submit()}>
        {busy ? 'submitting...' : 'Contribute'}
      </Button>
      <ErrorText>{error}</ErrorText>
      <SuccessText>{result}</SuccessText>
    </div>
  )
}
