import { useState } from 'react'
import { useWallet } from '../context/WalletContext'
import { submitProcedureCall } from '../qubic/transaction'
import { payCreatePool } from '../qubic/encode'
import { QPOOLK6, P_CREATE_POOL, CREATE_POOL_FEE } from '../qubic/config'
import { useAction } from '../hooks/useAction'
import { Button, Card, ErrorText, Field, SuccessText, TextInput } from './ui'
import { IconPlus } from './icons'

export function CreatePoolForm({ existingNames, onCreated }: { existingNames: string[]; onCreated?: () => void }) {
  const { wallet, refreshBalance } = useWallet()
  const [name, setName] = useState('')
  const [permissioned, setPermissioned] = useState(false)
  const { busy, error, result, run } = useAction<string>()

  const nameTaken = existingNames.includes(name.toUpperCase())

  async function submit() {
    if (!wallet) return
    await run(async () => {
      const payload = payCreatePool(name, permissioned ? 1 : 0)
      const r = await submitProcedureCall(wallet, QPOOLK6, P_CREATE_POOL, CREATE_POOL_FEE, payload)
      await refreshBalance()
      onCreated?.()
      return `Submitted at tick ${r.tick} -- rescan the pool list below to confirm it was actually created (fee: ${CREATE_POOL_FEE.toLocaleString()} QU).`
    })
  }

  if (!wallet) return <Card title="Create pool" icon={<IconPlus />}>Connect a wallet to create a pool.</Card>

  return (
    <Card title="Create pool" icon={<IconPlus />}>
      <Field label="Pool name (1-8 characters)" hint="Must be unique -- no two pools can share a name.">
        <TextInput value={name} onChange={(e) => setName(e.target.value.toUpperCase())} maxLength={8} placeholder="MYPOOL" />
      </Field>
      {nameTaken && <ErrorText>A pool named "{name.toUpperCase()}" already exists -- pick a different name.</ErrorText>}
      <label className="flex items-center gap-2 text-sm">
        <input type="checkbox" checked={permissioned} onChange={(e) => setPermissioned(e.target.checked)} className="accent-primary h-4 w-4" />
        Permissioned (only admitted members can join/contribute)
      </label>
      <div className="text-xs text-muted-foreground">Fee: {CREATE_POOL_FEE.toLocaleString()} QU, paid by you.</div>
      <Button variant="primary" loading={busy} disabled={name.length < 1 || name.length > 8 || nameTaken} onClick={() => void submit()}>
        Create pool
      </Button>
      <ErrorText>{error}</ErrorText>
      <SuccessText>{result}</SuccessText>
    </Card>
  )
}
