import { useCallback, useState } from 'react'

export function useAction<T>() {
  const [busy, setBusy] = useState(false)
  const [error, setError] = useState<string | null>(null)
  const [result, setResult] = useState<T | null>(null)

  const run = useCallback(async (fn: () => Promise<T>) => {
    setBusy(true)
    setError(null)
    try {
      const r = await fn()
      setResult(r)
      return r
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e))
      return null
    } finally {
      setBusy(false)
    }
  }, [])

  const reset = useCallback(() => {
    setError(null)
    setResult(null)
  }, [])

  return { busy, error, result, run, reset }
}
