import { useEffect, useState } from 'react'
import { getTick } from '../qubic/rpc'
import { EPOCH_TICKS_ESTIMATE, TICKS_PER_SECOND_ESTIMATE } from '../qubic/config'

function formatCountdown(seconds: number): string {
  if (seconds <= 0) return 'any moment now'
  const m = Math.floor(seconds / 60)
  const s = Math.floor(seconds % 60)
  return `~${m}m ${s}s`
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

  return (
    <div className="bg-background border-b border-border px-4 py-1.5 text-xs text-muted-foreground flex items-center justify-center gap-2 flex-wrap">
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
  )
}
