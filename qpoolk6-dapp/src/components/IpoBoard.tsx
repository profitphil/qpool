import { useEffect, useState } from 'react'
import { getTick } from '../qubic/rpc'
import { activeIpoTargets, nextIpoTarget } from './EpochBanner'
import { Avatar, Badge, Button, Card, EmptyState, ErrorText, Skeleton } from './ui'
import { IconTicket } from './icons'

// A target's IPO is a uniform-price auction capped at 676 shares (one per
// computor), run through whichever pool proposes a BUY_IPO bid during its
// one-epoch window -- not a standalone trade, so this tab is read-only status
// plus a pointer at the Propose form, not its own order-entry form.
export function IpoBoard() {
  const [epoch, setEpoch] = useState<number | null>(null)
  const [busy, setBusy] = useState(false)
  const [error, setError] = useState<string | null>(null)

  async function load() {
    setBusy(true)
    setError(null)
    try {
      const { epoch: e } = await getTick()
      setEpoch(e)
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e))
    } finally {
      setBusy(false)
    }
  }

  useEffect(() => {
    void load()
  }, [])

  const active = epoch === null ? [] : activeIpoTargets(epoch)
  const next = epoch === null ? null : nextIpoTarget(epoch)

  return (
    <div className="space-y-4">
      <Card
        title={`IPO bidding (${active.length} open)`}
        icon={<IconTicket />}
        iconTone="warning"
        right={<Button size="sm" loading={busy} onClick={() => void load()}>refresh</Button>}
      >
        <div className="text-xs text-muted-foreground">
          Bid into a brand-new target's initial share offering -- a uniform-price auction capped at 676 shares
          total, one epoch-long window per target. Submitted through any pool you belong to, via Propose &rarr;
          "Bid in an IPO" with the contract index shown below.
        </div>
        <ErrorText>{error}</ErrorText>
      </Card>

      {epoch === null ? (
        <Card title="Open for bidding" icon={<IconTicket />} iconTone="warning">
          <Skeleton className="h-10" />
        </Card>
      ) : active.length === 0 ? (
        <Card title="Open for bidding" icon={<IconTicket />} iconTone="warning">
          <EmptyState
            icon="🎟"
            title="No IPO open this epoch"
            hint={
              next
                ? `Next: ${next.name} (index ${next.index}) at epoch ${next.constructionEpoch - 1}`
                : 'No more scheduled IPO windows -- ask for a genesis restart to open more.'
            }
          />
        </Card>
      ) : (
        <Card title={`Open for bidding (${active.length})`} icon={<IconTicket />} iconTone="warning">
          <div className="grid sm:grid-cols-2 gap-2">
            {active.map((t) => (
              <div key={t.index} className="flex items-center justify-between border border-border rounded-xl p-3 bg-input/20">
                <div className="flex items-center gap-2">
                  <Avatar seed={t.name} size={28} />
                  <div>
                    <div className="font-semibold text-sm">{t.name}</div>
                    <div className="text-xs text-muted-foreground">contract index {t.index}</div>
                  </div>
                </div>
                <Badge tone="warning" dot>bidding open</Badge>
              </div>
            ))}
          </div>
          <div className="text-[11px] text-muted-foreground">Use Propose &rarr; "Bid in an IPO" in any pool, with that contract index.</div>
        </Card>
      )}
    </div>
  )
}
