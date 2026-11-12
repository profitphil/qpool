import { useState } from 'react'
import { getBalance } from '../qubic/rpc'
import { AssetList } from './AssetList'
import { Button, Card, ErrorText, Field, TextInput } from './ui'

// Checking a wallet other than your own -- a demo wallet before switching to
// it, a seat-market seller, anyone. Doesn't need a connected wallet.
export function WalletLookup() {
  const [identity, setIdentity] = useState('')
  const [checked, setChecked] = useState<string | null>(null)
  const [balance, setBalance] = useState<bigint | null>(null)
  const [busy, setBusy] = useState(false)
  const [error, setError] = useState<string | null>(null)

  async function check() {
    setBusy(true)
    setError(null)
    try {
      setBalance(await getBalance(identity))
      setChecked(identity)
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e))
    } finally {
      setBusy(false)
    }
  }

  return (
    <Card title="Look up a wallet">
      <div className="text-xs text-muted-foreground">Check any identity's QU balance and token holdings -- not just your own connected wallet.</div>
      <Field label="Identity">
        <TextInput value={identity} onChange={(e) => setIdentity(e.target.value)} placeholder="60-char identity" />
      </Field>
      <Button disabled={busy || !identity} onClick={() => void check()}>
        {busy ? 'checking...' : 'Check'}
      </Button>
      <ErrorText>{error}</ErrorText>
      {checked && (
        <div className="space-y-1 pt-1">
          <div className="text-xs">
            Balance: <span className="font-mono">{balance?.toLocaleString()} QU</span>
          </div>
          <AssetList identity={checked} />
        </div>
      )}
    </Card>
  )
}
