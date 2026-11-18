import { useEffect, useState } from 'react'
import { useWallet } from '../context/WalletContext'
import { scanPools, filterJoinedPools } from '../qubic/scan'
import type { Pool } from '../qubic/decode'
import { Badge, Button, Card, EmptyState, ErrorText, Identity, SubTabs } from './ui'
import { CreatePoolForm } from './CreatePoolForm'
import { PoolDetail } from './PoolDetail'
import { IconPool } from './icons'

type SubTab = 'explore' | 'joined'

function PoolGrid({ pools, onSelect }: { pools: { id: number; pool: Pool }[]; onSelect: (id: number) => void }) {
  return (
    <div className="grid sm:grid-cols-2 md:grid-cols-3 gap-3">
      {pools.map(({ id, pool }) => (
        <div
          key={id}
          onClick={() => onSelect(id)}
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
  )
}

export function PoolsTab() {
  const { wallet } = useWallet()
  const [subTab, setSubTab] = useState<SubTab>('explore')
  const [pools, setPools] = useState<{ id: number; pool: Pool }[]>([])
  const [joined, setJoined] = useState<{ id: number; pool: Pool }[] | null>(null)
  const [selected, setSelected] = useState<number | null>(null)
  const [busy, setBusy] = useState(false)
  const [joinedBusy, setJoinedBusy] = useState(false)
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

  async function loadJoined() {
    if (!wallet) return
    setJoinedBusy(true)
    setError(null)
    try {
      setJoined(await filterJoinedPools(pools, wallet.identity))
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e))
    } finally {
      setJoinedBusy(false)
    }
  }

  useEffect(() => {
    void load()
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [])

  // Re-check membership whenever the underlying pool list changes (a rescan
  // or "scan 30 more") or a different wallet connects -- a stale Joined list
  // would otherwise keep showing someone else's pools after a wallet switch.
  useEffect(() => {
    setJoined(null)
    if (subTab === 'joined') void loadJoined()
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [pools, wallet?.identity])

  function selectSubTab(t: SubTab) {
    setSubTab(t)
    if (t === 'joined' && joined === null) void loadJoined()
  }

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
      <CreatePoolForm existingNames={pools.map(({ pool }) => pool.poolNameStr)} onCreated={load} />

      <SubTabs
        tabs={[
          { key: 'explore', label: `Explore (${pools.length})` },
          { key: 'joined', label: `Joined${joined ? ` (${joined.length})` : ''}` },
        ]}
        active={subTab}
        onChange={selectSubTab}
      />

      {subTab === 'explore' && (
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
          <PoolGrid pools={pools} onSelect={setSelected} />
        </Card>
      )}

      {subTab === 'joined' && (
        <Card
          title={`Your pools (${joined?.length ?? 0} of ${pools.length} scanned)`}
          icon={<IconPool />}
          right={<Button size="sm" loading={joinedBusy} onClick={() => void loadJoined()}>rescan</Button>}
        >
          <ErrorText>{error}</ErrorText>
          {!wallet ? (
            <EmptyState title="Connect a wallet to see your pools" hint="Membership is checked per-wallet, not stored locally." />
          ) : joinedBusy && joined === null ? (
            <EmptyState title="Checking membership across every scanned pool..." />
          ) : joined && joined.length === 0 ? (
            <EmptyState
              title="Not a member of any scanned pool yet"
              hint="Join one from Explore, or scan more slots above if your pool might be further down."
            />
          ) : (
            <PoolGrid pools={joined ?? []} onSelect={setSelected} />
          )}
        </Card>
      )}
    </div>
  )
}
