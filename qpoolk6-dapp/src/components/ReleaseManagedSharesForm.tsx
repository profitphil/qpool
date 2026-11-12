import { useState } from 'react'
import { useWallet } from '../context/WalletContext'
import { submitProcedureCall } from '../qubic/transaction'
import { payReleaseManagedShares } from '../qubic/encode'
import { nameToU64 } from '../qubic/codec'
import { QPOOLK6, P_RELEASE_MANAGED_SHARES } from '../qubic/config'
import { useAction } from '../hooks/useAction'
import { Button, Card, ErrorText, Field, SuccessText, TextInput } from './ui'

export function ReleaseManagedSharesForm() {
  const { wallet } = useWallet()
  const [issuerId, setIssuerId] = useState('')
  const [assetName, setAssetName] = useState('')
  const [amount, setAmount] = useState('')
  const [destIndex, setDestIndex] = useState('1')
  const { busy, error, result, run } = useAction<string>()

  if (!wallet) return <Card title="Release managed shares">Connect a wallet to use this.</Card>

  async function submit() {
    await run(async () => {
      const payload = payReleaseManagedShares(issuerId, nameToU64(assetName), BigInt(amount), Number(destIndex))
      const r = await submitProcedureCall(wallet!, QPOOLK6, P_RELEASE_MANAGED_SHARES, 0, payload)
      return `Submitted at tick ${r.tick} -- check your asset balance to confirm (Qubic has no transaction receipts; a refusal submits fine but changes nothing).`
    })
  }

  return (
    <Card title="Release managed shares">
      <div className="text-xs text-muted-foreground">
        Reclaims shares you own that QPoolK6 manages but that never ended up in a pool holding (e.g. a deposit that
        got refused after the share transfer already happened). Attach the destination contract's own transfer fee
        if it has one (QX: index 1, charges a fee; most others don't).
      </div>
      <Field label="Token issuer identity">
        <TextInput value={issuerId} onChange={(e) => setIssuerId(e.target.value)} placeholder="60-char identity" />
      </Field>
      <Field label="Asset name (1-7 chars)">
        <TextInput value={assetName} onChange={(e) => setAssetName(e.target.value)} maxLength={7} placeholder="MYTOKEN" />
      </Field>
      <Field label="Amount">
        <TextInput value={amount} onChange={(e) => setAmount(e.target.value)} inputMode="numeric" />
      </Field>
      <Field label="Destination contract index (e.g. 1 = QX)">
        <TextInput value={destIndex} onChange={(e) => setDestIndex(e.target.value)} inputMode="numeric" />
      </Field>
      <Button disabled={busy || !issuerId || !assetName || !amount} onClick={() => void submit()}>
        {busy ? 'submitting...' : 'Release shares'}
      </Button>
      <ErrorText>{error}</ErrorText>
      <SuccessText>{result}</SuccessText>
    </Card>
  )
}
