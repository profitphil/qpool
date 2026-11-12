import { useEffect, useState } from 'react'
import { getTick } from '../qubic/rpc'
import { EPOCH_TICKS_ESTIMATE, TICKS_PER_SECOND_ESTIMATE, IPO_TARGETS } from '../qubic/config'

function formatCountdown(seconds: number): string {
  if (seconds <= 0) return 'any moment now'
  const m = Math.floor(seconds / 60)
  const s = Math.floor(seconds % 60)
  return `~${m}m ${s}s`
}

// A target's IPO window is always constructionEpoch - 1.
function activeIpoTargets(epoch: number) {
  return IPO_TARGETS.filter((t) => t.constructionEpoch - 1 === epoch)
}
function nextIpoTarget(epoch: number) {
  const upcoming = IPO_TARGETS.filter((t) => t.constructionEpoch - 1 > epoch)
  if (upcoming.length === 0) return null
  return upcoming.reduce((a, b) => (a.constructionEpoch < b.constructionEpoch ? a : b))
}

export function EpochBanner() {
  const [epoch, setEpoch] = useState<number | null>(null)
  const [tick, setTick] = useState<number | null>(null)
  const [secondsLeft, setSecondsLeft] = useState<number | null>(null)
  const [error, setError] = useState<string | null>(null)

  useEffect(() => {
    let cancelled = false
    async function poll() {
      try {
        const t = await getTick()
        if (cancelled) return
        setEpoch(t.epoch)
        setTick(t.tick)
        const ticksElapsed = t.tick - t.epochStartTick
        const ticksLeft = Math.max(0, EPOCH_TICKS_ESTIMATE - ticksElapsed)
        setSecondsLeft(ticksLeft / TICKS_PER_SECOND_ESTIMATE)
        setError(null)
      } catch (e) {
        if (!cancelled) setError(e instanceof Error ? e.message : String(e))
      }
    }
    void poll()
    const id = setInterval(poll, 5000)
    return () => {
      cancelled = true
      clearInterval(id)
    }
  }, [])

  const active = epoch === null ? [] : activeIpoTargets(epoch)
  const next = epoch === null ? null : nextIpoTarget(epoch)

  return (
    <div className="bg-background border-b border-border px-4 py-1.5 text-xs text-muted-foreground flex flex-col items-center gap-1">
      <div className="flex items-center justify-center gap-2 flex-wrap">
        {error ? (
          <span className="text-destructive">epoch/tick unavailable: {error}</span>
        ) : epoch === null ? (
          <span>loading epoch...</span>
        ) : (
          <>
            <span>
              Epoch <span className="text-foreground font-medium">{epoch}</span>
            </span>
            <span>&middot;</span>
            <span className="font-mono">tick {tick?.toLocaleString()}</span>
            <span>&middot;</span>
            <span>
              next epoch in <span className="text-primary font-medium">{formatCountdown(secondsLeft ?? 0)}</span> (estimate)
            </span>
          </>
        )}
      </div>
      {epoch !== null && (
        <div className="flex items-center justify-center gap-2 flex-wrap">
          {active.length > 0 ? (
            <span>
              Bidding open now on{' '}
              <span className="text-success font-medium">
                {active.map((t) => `${t.name} (index ${t.index})`).join(', ')}
              </span>{' '}
              -- use that contract index in Propose &rarr; "Bid in an IPO"
            </span>
          ) : next ? (
            <span className="text-muted-foreground">
              No IPO open this epoch -- next: <span className="text-foreground">{next.name} (index {next.index})</span> at epoch{' '}
              {next.constructionEpoch - 1}
            </span>
          ) : (
            <span className="text-muted-foreground">No more scheduled IPO windows -- ask for a genesis restart to open more.</span>
          )}
        </div>
      )}
    </div>
  )
}
