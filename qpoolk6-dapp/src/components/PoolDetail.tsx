import { useEffect, useState } from 'react'
import { getPool, type Pool } from '../qubic/decode'
import { scanHoldingsForPool, scanProposalsForPool } from '../qubic/scan'
import type { Holding, Proposal } from '../qubic/decode'
import { Badge, Button, Card, ErrorText, KV } from './ui'
import { ContributeForm } from './ContributeForm'
import { DepositHoldingAssetForm } from './DepositHoldingAssetForm'
import { ProposeForm } from './ProposeForm'
import { HoldingRow } from './HoldingRow'
import { ProposalRow } from './ProposalRow'
import { SeatMarket } from './SeatMarket'

export function PoolDetail({ poolId }: { poolId: number }) {
  const [pool, setPool] = useState<Pool | null>(null)
  const [holdings, setHoldings] = useState<{ id: number; holding: Holding }[]>([])
  const [proposals, setProposals] = useState<{ id: number; proposal: Proposal }[]>([])
  const [error, setError] = useState<string | null>(null)
  const [loadingExtras, setLoadingExtras] = useState(false)

  async function loadPool() {
    try {
      setPool(await getPool(poolId))
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e))
    }
  }

  async function loadExtras() {
    setLoadingExtras(true)
    try {
      // Sequential, not Promise.all -- a free Cloudflare quick tunnel rate-limits
      // request bursts, and this pool's holdings scan alone already saturates
      // the per-request concurrency cap (see scan.ts).
      const h = await scanHoldingsForPool(poolId)
      setHoldings(h)
      const p = await scanProposalsForPool(poolId)
      setProposals(p)
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e))
    } finally {
      setLoadingExtras(false)
    }
  }

  useEffect(() => {
    void loadPool()
    void loadExtras()
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [poolId])

  if (!pool) return <div className="text-sm text-muted-foreground">Loading pool #{poolId}...</div>
  if (pool.found !== 1) return <ErrorText>Pool #{poolId} not found.</ErrorText>

  return (
    <div className="space-y-4">
      <Card title={`Pool #${poolId}: ${pool.poolNameStr}`}>
        <div className="flex items-center justify-between">
          <Button onClick={() => void loadPool()}>refresh</Button>
          <Badge color={pool.permissioned ? '#f97066' : '#47cd89'}>{pool.permissioned ? 'Private' : 'Open to anyone'}</Badge>
        </div>
        <KV k="creator" v={pool.creator} />
        <KV k="members" v={pool.contributorCount.toString()} />
        <KV k="available funds (not yet allocated)" v={`${pool.uncommittedTotal.toLocaleString()} QU`} />
        <KV k="allocated funds" v={`${pool.committedTotal.toLocaleString()} QU`} />
        <KV k="holdings" v={pool.holdingCount.toString()} />
        <KV k="token deposits" v={pool.depositCount.toString()} />
        <ErrorText>{error}</ErrorText>
      </Card>

      <Card title="Join / contribute">
        <ContributeForm poolId={poolId} onChanged={loadPool} />
      </Card>

      <Card title="Deposit a token">
        <DepositHoldingAssetForm poolId={poolId} onChanged={loadExtras} />
      </Card>

      <Card title="Propose a pool action">
        <ProposeForm poolId={poolId} holdings={holdings} onChanged={loadExtras} />
      </Card>

      <SeatMarket poolId={poolId} />

      <Card title={`Holdings (${holdings.length} found, scanned)`}>
        <Button disabled={loadingExtras} onClick={() => void loadExtras()}>
          {loadingExtras ? 'scanning...' : 'rescan'}
        </Button>
        <div className="grid sm:grid-cols-2 gap-2">
          {holdings.map(({ id, holding }) => (
            <HoldingRow key={id} id={id} holding={holding} onChanged={loadExtras} />
          ))}
        </div>
      </Card>

      <Card title={`Proposals (${proposals.length} found, scanned)`}>
        <div className="grid sm:grid-cols-2 gap-2">
          {proposals.map(({ id, proposal }) => (
            <ProposalRow key={id} id={id} proposal={proposal} onChanged={loadExtras} />
          ))}
        </div>
      </Card>
    </div>
  )
}
