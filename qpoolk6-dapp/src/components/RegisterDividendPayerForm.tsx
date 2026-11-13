import { useState } from 'react'
import { useWallet } from '../context/WalletContext'
import { submitProcedureCall } from '../qubic/transaction'
import { payRegisterDividendPayer } from '../qubic/encode'
import { nameToU64 } from '../qubic/codec'
import { QPOOLK6, P_REGISTER_DIVIDEND_PAYER, NULL_ID } from '../qubic/config'
import { useAction } from '../hooks/useAction'
import { Button, Card, ErrorText, Field, Identity, SuccessText, TextInput } from './ui'
import { IconGift } from './icons'

export function RegisterDividendPayerForm() {
  const { wallet } = useWallet()
  const [assetName, setAssetName] = useState('')
  const [payerId, setPayerId] = useState('')
  const { busy, error, result, run } = useAction<string>()

  if (!wallet) return <Card title="Register dividend payer" icon={<IconGift />}>Connect a wallet (must be the token's issuer) to use this.</Card>

  async function submit(clear: boolean) {
    await run(async () => {
      const payload = payRegisterDividendPayer(wallet!.identity, nameToU64(assetName), clear ? NULL_ID : payerId)
      const r = await submitProcedureCall(wallet!, QPOOLK6, P_REGISTER_DIVIDEND_PAYER, 0, payload)
      return `Submitted at tick ${r.tick} -- this only succeeds if your connected wallet is ${assetName}'s registered issuer; there's no receipt to confirm it here, only the effect on future deposits.`
    })
  }

  return (
    <Card title="Register dividend payer" icon={<IconGift />}>
      <div className="text-xs text-muted-foreground">
        Issuer-only: declares who the REAL dividend payer is for a token you issued, when it differs from your own
        identity (e.g. a staking contract that pays dividends on your token but isn't its registered issuer). Must
        be sent from the connected wallet, and that wallet must be this token's issuer -- your connected identity is{' '}
        <Identity id={wallet.identity} />.
      </div>
      <Field label="Asset name (1-7 chars, issued by your connected wallet)">
        <TextInput value={assetName} onChange={(e) => setAssetName(e.target.value)} maxLength={7} placeholder="MYTOKEN" />
      </Field>
      <Field label="Real payer identity">
        <TextInput value={payerId} onChange={(e) => setPayerId(e.target.value)} placeholder="60-char identity" />
      </Field>
      <div className="flex gap-2">
        <Button variant="primary" loading={busy} disabled={!assetName || !payerId} onClick={() => void submit(false)}>
          Register
        </Button>
        <Button loading={busy} disabled={!assetName} onClick={() => void submit(true)}>
          Clear (back to issuer-pays)
        </Button>
      </div>
      <ErrorText>{error}</ErrorText>
      <SuccessText>{result}</SuccessText>
    </Card>
  )
}
