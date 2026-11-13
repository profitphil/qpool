import { useEffect, useState } from 'react'
import { getStats, type Stats } from '../qubic/decode'
import { Button, Card, ErrorText, Stat } from './ui'
import { IconChart } from './icons'

function labelize(key: string): string {
  return key.replace(/([a-z])([A-Z])/g, '$1 $2').replace(/^./, (c) => c.toUpperCase())
}

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
    <Card
      title="Contract stats"
      icon={<IconChart />}
      right={<Button size="sm" loading={busy} onClick={() => void load()}>refresh</Button>}
    >
      <ErrorText>{error}</ErrorText>
      {stats && (
        <div className="grid grid-cols-2 sm:grid-cols-3 gap-3">
          {Object.entries(stats).map(([k, v]) => (
            <div key={k} className="border border-border rounded-xl p-3 bg-muted/30">
              <Stat label={labelize(k)} value={v.toLocaleString()} />
            </div>
          ))}
        </div>
      )}
    </Card>
  )
}
