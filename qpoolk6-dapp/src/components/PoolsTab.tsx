import { useEffect, useState } from 'react'
import { scanPools } from '../qubic/scan'
import type { Pool } from '../qubic/decode'
import { Badge, Button, Card, ErrorText, Identity } from './ui'
import { CreatePoolForm } from './CreatePoolForm'
import { PoolDetail } from './PoolDetail'
import { IconPool } from './icons'

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
      <Card
        title={`Pools (${pools.length} found in first ${scanLimit} slots)`}
        icon={<IconPool />}
        right={
          <div className="flex gap-2">
            <Button size="sm" loading={busy} onClick={() => void load()}>rescan</Button>
            <Button size="sm" variant="ghost" onClick={() => setScanLimit((n) => n + 30)}>scan 30 more</Button>
          </div>
        }
      >
        <ErrorText>{error}</ErrorText>
        <div className="grid sm:grid-cols-2 md:grid-cols-3 gap-3">
          {pools.map(({ id, pool }) => (
            <div
              key={id}
              onClick={() => setSelected(id)}
              role="button"
              tabIndex={0}
              className="group text-left bg-card border border-border rounded-xl p-3 space-y-2 text-xs shadow-[0_1px_0_rgba(255,255,255,0.03)_inset,0_4px_10px_-4px_rgba(0,0,0,0.5)] transition-all duration-200 hover:border-primary/50 hover:shadow-[0_1px_0_rgba(255,255,255,0.03)_inset,0_12px_24px_-8px_rgba(176,249,254,0.2)] hover:-translate-y-0.5 cursor-pointer"
            >
              <div className="flex items-center justify-between">
                <div className="flex items-center gap-2">
                  <div className="flex h-7 w-7 items-center justify-center rounded-lg bg-gradient-to-br from-primary to-primary/70 text-xs font-bold text-primary-foreground">
                    #{id}
                  </div>
                  <div className="font-semibold text-sm">{pool.poolNameStr}</div>
                </div>
                <Badge tone={pool.permissioned ? 'danger' : 'success'}>
                  {pool.permissioned ? 'Private' : 'Open'}
                </Badge>
              </div>
              <div onClick={(e) => e.stopPropagation()}>
                <Identity id={pool.creator} label="by" />
              </div>
              <div className="flex justify-between text-muted-foreground pt-1.5 border-t border-border/60">
                <span>holdings: {pool.holdingCount.toString()}</span>
                <span className="text-foreground font-medium transition-transform group-hover:translate-x-1">
                  committed: {pool.committedTotal.toString()} &rarr;
                </span>
              </div>
            </div>
          ))}
        </div>
      </Card>
    </div>
  )
}
