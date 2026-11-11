import { useState } from 'react'
import { useWallet } from '../context/WalletContext'
import { submitProcedureCall } from '../qubic/transaction'
import { payDepositHoldingAsset } from '../qubic/encode'
import { nameToU64 } from '../qubic/codec'
import { QPOOLK6, P_DEPOSIT_HOLDING_ASSET } from '../qubic/config'
import { useAction } from '../hooks/useAction'
import { Button, ErrorText, Field, SuccessText, TextInput } from './ui'

export function DepositHoldingAssetForm({ poolId, onChanged }: { poolId: number; onChanged?: () => void }) {
  const { wallet } = useWallet()
  const [issuerId, setIssuerId] = useState('')
  const [assetName, setAssetName] = useState('')
  const [amount, setAmount] = useState('')
  const [seatUnits, setSeatUnits] = useState('')
  const { busy, error, result, run } = useAction<string>()

  if (!wallet) return null

  async function submit() {
    await run(async () => {
      const payload = payDepositHoldingAsset(poolId, issuerId, nameToU64(assetName), BigInt(amount), BigInt(seatUnits || amount))
      const r = await submitProcedureCall(wallet!, QPOOLK6, P_DEPOSIT_HOLDING_ASSET, 0, payload)
      onChanged?.()
      return `Submitted at tick ${r.tick} -- check the holdings list below to confirm it actually landed (needs you to already hold these shares with QPoolK6 management rights granted via QX; a refusal submits fine but creates nothing).`
    })
  }

  return (
    <div className="space-y-2">
      <div className="text-xs text-neutral-500">
        Deposits a token you already own and have granted QPoolK6 management rights over (via QX's
        TransferShareManagementRights) into this pool, creating a new holding. Creator-only.
      </div>
      <Field label="Token issuer identity">
        <TextInput value={issuerId} onChange={(e) => setIssuerId(e.target.value)} placeholder="60-char identity" />
      </Field>
      <Field label="Asset name (1-7 chars)">
        <TextInput value={assetName} onChange={(e) => setAssetName(e.target.value)} maxLength={7} placeholder="MYTOKEN" />
      </Field>
      <Field label="Amount (shares)">
        <TextInput value={amount} onChange={(e) => setAmount(e.target.value)} inputMode="numeric" placeholder="1000" />
      </Field>
      <Field label="Seat units (cost basis; defaults to amount)">
        <TextInput value={seatUnits} onChange={(e) => setSeatUnits(e.target.value)} inputMode="numeric" placeholder="(same as amount)" />
      </Field>
      <Button disabled={busy || !issuerId || !assetName || !amount} onClick={() => void submit()}>
        {busy ? 'submitting...' : 'DepositHoldingAsset'}
      </Button>
      <ErrorText>{error}</ErrorText>
      <SuccessText>{result}</SuccessText>
    </div>
  )
}
