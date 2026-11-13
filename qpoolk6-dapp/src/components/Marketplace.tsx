import { useEffect, useState } from 'react'
import { scanPools, scanAllPoolOffers } from '../qubic/scan'
import { BuyRow } from './SeatMarket'
import { Badge, Button, Card, EmptyState, ErrorText, Skeleton } from './ui'
import { IconSeat } from './icons'

// Pool-mediated seat resale only -- a member selling part or all of their
// seat to someone else. Raw QX trading and IPO bidding each have their own
// tab now; this one is just the seat market.
export function Marketplace() {
  const [offers, setOffers] = useState<Awaited<ReturnType<typeof scanAllPoolOffers>>>([])
  const [busy, setBusy] = useState(false)
  const [loadedOnce, setLoadedOnce] = useState(false)
  const [error, setError] = useState<string | null>(null)

  async function load() {
    setBusy(true)
    setError(null)
    try {
      const pools = await scanPools(30)
      setOffers(await scanAllPoolOffers(pools))
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e))
    } finally {
      setBusy(false)
      setLoadedOnce(true)
    }
  }

  useEffect(() => {
    void load()
  }, [])

  return (
    <div className="space-y-4">
      <Card
        title={`Pool seats for sale (${offers.length})`}
        icon={<IconSeat />}
        iconTone="success"
        right={<Button size="sm" loading={busy} onClick={() => void load()}>rescan</Button>}
      >
        <div className="text-xs text-muted-foreground">
          A pool member selling part or all of their seat -- buying one hands you their share of whatever that pool
          already holds. Scanning the first 30 pools.
        </div>
        <ErrorText>{error}</ErrorText>
        {!loadedOnce ? (
          <Skeleton className="h-20" />
        ) : offers.length === 0 ? (
          <EmptyState icon="🪑" title="No open seat-market offers" hint="Checked the first 30 pools." />
        ) : (
          <div className="grid sm:grid-cols-2 gap-2">
            {offers.map((o, i) => (
              <div key={i} className="space-y-1.5">
                <div className="flex items-center gap-1.5 text-xs">
                  <Badge tone="neutral">{o.poolName} (#{o.poolId})</Badge>
                </div>
                <BuyRow offer={o} onChanged={load} />
              </div>
            ))}
          </div>
        )}
      </Card>
    </div>
  )
}
