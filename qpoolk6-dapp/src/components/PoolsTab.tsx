import { useEffect, useState } from 'react'
import { scanPools } from '../qubic/scan'
import type { Pool } from '../qubic/decode'
import { Button, Card, ErrorText } from './ui'
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
        <div className="grid sm:grid-cols-2 md:grid-cols-3 gap-2">
          {pools.map(({ id, pool }) => (
            <button
              key={id}
              onClick={() => setSelected(id)}
              className="text-left border border-neutral-200 dark:border-neutral-800 rounded p-2 hover:bg-neutral-100 dark:hover:bg-neutral-800 text-xs space-y-0.5"
            >
              <div className="font-semibold">
                #{id}: {pool.poolNameStr}
              </div>
              <div className="font-mono text-neutral-500 truncate">{pool.creator}</div>
              <div>holdings: {pool.holdingCount.toString()}, committed: {pool.committedTotal.toString()}</div>
            </button>
          ))}
        </div>
      </Card>
    </div>
  )
}
