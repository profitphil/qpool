import { useEffect, useState } from 'react'
import { scanPools } from '../qubic/scan'
import type { Pool } from '../qubic/decode'
import { Badge, Button, Card, ErrorText } from './ui'
import { CreatePoolForm } from './CreatePoolForm'
import { PoolDetail } from './PoolDetail'

export function PoolsTab() {
  const [pools, setPools] = useState<{ id: number; pool: Pool }[]>([])
  const [selected, setSelected] = useState<number | null>(null)
  const [busy, setBusy] = useState(false)
  const [error, setError] = useState<string | null>(null)
  const [scanLimit, setScanLimit] = useState(30)

  async function load() {
    setBusy(true)
    setError(null)
    try {
      setPools(await scanPools(scanLimit))
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e))
    } finally {
      setBusy(false)
    }
  }

  useEffect(() => {
    void load()
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [])

  if (selected !== null) {
    return (
      <div className="space-y-3">
        <Button onClick={() => setSelected(null)}>&larr; back to pool list</Button>
        <PoolDetail poolId={selected} />
      </div>
    )
  }

  return (
    <div className="space-y-4">
      <CreatePoolForm onCreated={load} />
      <Card title={`Pools (${pools.length} found in first ${scanLimit} slots)`}>
        <div className="flex gap-2 items-center">
          <Button disabled={busy} onClick={() => void load()}>
            {busy ? 'scanning...' : 'rescan'}
          </Button>
          <Button onClick={() => setScanLimit((n) => n + 30)}>scan 30 more</Button>
        </div>
        <ErrorText>{error}</ErrorText>
        <div className="grid sm:grid-cols-2 md:grid-cols-3 gap-3">
          {pools.map(({ id, pool }) => (
            <button
              key={id}
              onClick={() => setSelected(id)}
              className="group text-left bg-card border border-border rounded-xl p-3 space-y-2 text-xs shadow-sm transition-all duration-200 hover:border-primary/50 hover:shadow-lg hover:-translate-y-0.5"
            >
              <div className="flex items-center justify-between">
                <div className="flex items-center gap-2">
                  <div className="flex h-6 w-6 items-center justify-center rounded-lg bg-gradient-to-r from-primary to-success text-xs font-bold text-primary-foreground">
                    #{id}
                  </div>
                  <div className="font-semibold text-sm">{pool.poolNameStr}</div>
                </div>
                <Badge tone={pool.permissioned ? 'danger' : 'success'}>
                  {pool.permissioned ? 'Private' : 'Open'}
                </Badge>
              </div>
              <div className="font-mono text-muted-foreground truncate">{pool.creator}</div>
              <div className="flex justify-between text-muted-foreground pt-1 border-t border-border/60">
                <span>holdings: {pool.holdingCount.toString()}</span>
                <span className="text-foreground font-medium transition-transform group-hover:translate-x-1">
                  committed: {pool.committedTotal.toString()} &rarr;
                </span>
              </div>
            </button>
          ))}
        </div>
      </Card>
    </div>
  )
}
