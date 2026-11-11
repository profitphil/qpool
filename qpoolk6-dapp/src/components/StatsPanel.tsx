import { useEffect, useState } from 'react'
import { getStats, type Stats } from '../qubic/decode'
import { Button, Card, ErrorText, KV } from './ui'

export function StatsPanel() {
  const [stats, setStats] = useState<Stats | null>(null)
  const [error, setError] = useState<string | null>(null)
  const [busy, setBusy] = useState(false)

  async function load() {
    setBusy(true)
    setError(null)
    try {
      setStats(await getStats())
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e))
    } finally {
      setBusy(false)
    }
  }

  useEffect(() => {
    void load()
  }, [])

  return (
    <Card title="Contract stats">
      <Button disabled={busy} onClick={() => void load()}>
        {busy ? 'loading...' : 'refresh'}
      </Button>
      <ErrorText>{error}</ErrorText>
      {stats && (
        <div className="grid grid-cols-1 sm:grid-cols-2 gap-x-6">
          {Object.entries(stats).map(([k, v]) => (
            <KV key={k} k={k} v={v.toLocaleString()} />
          ))}
        </div>
      )}
    </Card>
  )
}
