import { createContext, useCallback, useContext, useState, type ReactNode } from 'react'
import { deriveWallet, type Wallet } from '../qubic/identity'
import { getBalance } from '../qubic/rpc'

interface WalletState {
  wallet: Wallet | null
  balance: bigint | null
  connecting: boolean
  error: string | null
  connect: (seed: string) => Promise<void>
  disconnect: () => void
  refreshBalance: () => Promise<void>
}

const WalletCtx = createContext<WalletState | null>(null)

// Session-only by default: the seed lives in memory, never localStorage,
// never sent anywhere but this browser tab's own signing calls. Closing the
// tab forgets it -- deliberate, for a shared public demo.
export function WalletProvider({ children }: { children: ReactNode }) {
  const [wallet, setWallet] = useState<Wallet | null>(null)
  const [balance, setBalance] = useState<bigint | null>(null)
  const [connecting, setConnecting] = useState(false)
  const [error, setError] = useState<string | null>(null)

  const connect = useCallback(async (seed: string) => {
    setError(null)
    setConnecting(true)
    try {
      const w = await deriveWallet(seed.trim())
      setWallet(w)
      setBalance(await getBalance(w.identity))
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e))
      throw e
    } finally {
      setConnecting(false)
    }
  }, [])

  const disconnect = useCallback(() => {
    setWallet(null)
    setBalance(null)
    setError(null)
  }, [])

  const refreshBalance = useCallback(async () => {
    if (!wallet) return
    setBalance(await getBalance(wallet.identity))
  }, [wallet])

  return (
    <WalletCtx.Provider value={{ wallet, balance, connecting, error, connect, disconnect, refreshBalance }}>
      {children}
    </WalletCtx.Provider>
  )
}

export function useWallet(): WalletState {
  const ctx = useContext(WalletCtx)
  if (!ctx) throw new Error('useWallet must be used within WalletProvider')
  return ctx
}
