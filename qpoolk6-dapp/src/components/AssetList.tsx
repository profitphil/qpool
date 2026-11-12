import { useState } from 'react'
import { getOwnedAssets, type OwnedAsset } from '../qubic/rpc'
import { CONTRACT_NAMES } from '../qubic/config'
import { Button, ErrorText, KV } from './ui'

function managedByLabel(index: number): string {
  if (index === 0) return 'self (not managed by any contract)'
  return CONTRACT_NAMES[index] ?? `contract #${index}`
}

// Lazy, opt-in -- same reasoning as MyPositionPanel: don't add a query no one
// asked for on every render, this backend rate-limits bursts.
export function AssetList({ identity }: { identity: string }) {
  const [assets, setAssets] = useState<OwnedAsset[] | null>(null)
  const [busy, setBusy] = useState(false)
  const [error, setError] = useState<string | null>(null)

  async function load() {
    setBusy(true)
    setError(null)
    try {
      setAssets(await getOwnedAssets(identity))
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e))
    } finally {
      setBusy(false)
    }
  }

  return (
    <div className="space-y-1">
      <Button disabled={busy} onClick={() => void load()}>
        {busy ? 'checking...' : 'Show assets held'}
      </Button>
      <ErrorText>{error}</ErrorText>
      {assets && assets.length === 0 && <div className="text-xs text-muted-foreground">No tokens -- only QU.</div>}
      {assets && assets.length > 0 && (
        <div className="space-y-0.5">
          {assets.map((a, i) => (
            <KV key={i} k={`${a.name} (managed by ${managedByLabel(a.managingContractIndex)})`} v={a.numberOfUnits.toString()} />
          ))}
        </div>
      )}
    </div>
  )
}
