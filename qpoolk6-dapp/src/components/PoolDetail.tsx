import { useEffect, useState } from 'react'
import { useWallet } from '../context/WalletContext'
import { getPool, type Pool } from '../qubic/decode'
import { getTick } from '../qubic/rpc'
import { scanHoldingsForPool, scanProposalsForPool } from '../qubic/scan'
import type { Holding, Proposal } from '../qubic/decode'
import { Badge, Button, Card, ErrorText, Identity, KV } from './ui'
import { ContributeForm } from './ContributeForm'
import { DepositHoldingAssetForm } from './DepositHoldingAssetForm'
import { ProposeForm } from './ProposeForm'
import { HoldingRow } from './HoldingRow'
import { ProposalRow } from './ProposalRow'
import { SeatMarket } from './SeatMarket'
import { IconLayers, IconList, IconPool, IconUpload, IconVote, IconWallet } from './icons'

export function PoolDetail({ poolId }: { poolId: number }) {
  const { wallet } = useWallet()
  const [pool, setPool] = useState<Pool | null>(null)
  const [holdings, setHoldings] = useState<{ id: number; holding: Holding }[]>([])
  const [proposals, setProposals] = useState<{ id: number; proposal: Proposal }[]>([])
  const [error, setError] = useState<string | null>(null)
  const [loadingExtras, setLoadingExtras] = useState(false)
  const [tick, setTick] = useState<number | null>(null)

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
    // A pool's first load fires ~260 requests at once (holdings + proposals
    // scan); on the shared free Cloudflare tunnel that occasionally drops or
    // times out a handful even with rpc.ts's own 429 backoff, undercounting
    // a just-submitted proposal with no visible error. One silent re-scan
    // after the initial burst has settled catches that without the user
    // needing to notice and click rescan themselves.
    const retry = setTimeout(() => void loadExtras(), 2500)
    return () => clearTimeout(retry)
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [poolId])

  // Drives each ProposalRow's vote/execute countdown -- one shared poll here
  // rather than every row fetching its own tick independently.
  useEffect(() => {
    let cancelled = false
    async function poll() {
      try {
        const t = await getTick()
        if (!cancelled) setTick(t.tick)
      } catch {
        /* a missed tick poll just pauses the countdown, nothing else depends on it */
      }
    }
    void poll()
    const id = setInterval(poll, 5000)
    return () => {
      cancelled = true
      clearInterval(id)
    }
  }, [])

  if (!pool) return <div className="text-sm text-muted-foreground">Loading pool #{poolId}...</div>
  if (pool.found !== 1) return <ErrorText>Pool #{poolId} not found.</ErrorText>

  const isCreator = wallet?.identity === pool.creator

  return (
    <div className="space-y-4">
      <Card title={`Pool #${poolId}: ${pool.poolNameStr}`} icon={<IconPool />} iconTone="info">
        <div className="flex items-center justify-between">
          <Button onClick={() => void loadPool()}>refresh</Button>
          <Badge tone={pool.permissioned ? 'danger' : 'success'}>{pool.permissioned ? 'Private' : 'Open to anyone'}</Badge>
        </div>
        <div className="grid sm:grid-cols-2 gap-x-6">
          <KV k="creator" v={<Identity id={pool.creator} />} />
          <KV k="members" v={pool.contributorCount.toString()} />
          <KV k="available funds (not yet allocated)" v={`${pool.uncommittedTotal.toLocaleString()} QU`} />
          <KV k="allocated funds" v={`${pool.committedTotal.toLocaleString()} QU`} />
          <KV k="holdings" v={pool.holdingCount.toString()} />
          <KV k="token deposits" v={pool.depositCount.toString()} />
        </div>
        <ErrorText>{error}</ErrorText>
      </Card>

      <div className="grid md:grid-cols-2 gap-4 items-start">
        <Card title="Join / contribute" icon={<IconWallet />} iconTone="success">
          <ContributeForm poolId={poolId} onChanged={loadPool} />
        </Card>

        <Card title="Propose a pool action" icon={<IconVote />} iconTone="warning">
          <ProposeForm poolId={poolId} holdings={holdings} availableFunds={pool.uncommittedTotal} onChanged={loadExtras} />
        </Card>

        {isCreator && (
          <Card
            title="Deposit a token"
            icon={<IconUpload />}
            iconTone="info"
            right={<Badge tone="info">creator only</Badge>}
            interactive
          >
            <DepositHoldingAssetForm poolId={poolId} onChanged={loadExtras} />
          </Card>
        )}
      </div>

      <SeatMarket poolId={poolId} />

      <Card title={`Holdings (${holdings.length} found, scanned)`} icon={<IconLayers />} iconTone="neutral">
        <Button loading={loadingExtras} onClick={() => void loadExtras()}>rescan</Button>
        <div className="grid sm:grid-cols-2 gap-2">
          {holdings.map(({ id, holding }) => (
            <HoldingRow key={id} id={id} holding={holding} onChanged={loadExtras} />
          ))}
        </div>
      </Card>

      <Card title={`Proposals (${proposals.length} found, scanned)`} icon={<IconList />} iconTone="neutral">
        <div className="text-xs text-muted-foreground">
          A busy shared tunnel can occasionally undercount on the very first load -- if you just submitted a
          proposal and don't see it below, rescan before assuming it failed.
        </div>
        <Button loading={loadingExtras} onClick={() => void loadExtras()}>rescan</Button>
        <div className="grid sm:grid-cols-2 gap-2">
          {proposals.map(({ id, proposal }) => (
            <ProposalRow key={id} id={id} proposal={proposal} currentTick={tick} onChanged={loadExtras} />
          ))}
        </div>
      </Card>
    </div>
  )
}
