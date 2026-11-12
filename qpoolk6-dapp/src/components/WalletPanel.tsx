import { useState } from 'react'
import { useWallet } from '../context/WalletContext'
import { Button, ErrorText, TextInput } from './ui'

export function WalletPanel() {
  const { wallet, balance, connecting, error, connect, disconnect, refreshBalance } = useWallet()
  const [seed, setSeed] = useState('')

  if (wallet) {
    return (
      <div className="bg-card border-b border-border p-3 flex items-center gap-3 flex-wrap text-sm">
        <span className="text-muted-foreground">Connected:</span>
        <span className="font-mono text-xs break-all text-primary">{wallet.identity}</span>
        <span className="text-muted-foreground">Balance:</span>
        <span className="font-mono">{balance?.toLocaleString()} QU</span>
        <Button onClick={() => void refreshBalance()}>refresh</Button>
        <Button onClick={disconnect}>disconnect</Button>
      </div>
    )
  }

  return (
    <div className="border-b border-border p-3 space-y-2">
      <div className="text-sm text-amber-400">
        Testnet only. Your seed is kept in this browser tab's memory and used only to sign transactions locally --
        it is never sent anywhere, and is forgotten when you close or reload this tab. Never paste a seed that
        protects anything real.
      </div>
      <div className="flex gap-2 items-start flex-wrap">
        <TextInput
          type="password"
          value={seed}
          onChange={(e) => setSeed(e.target.value)}
          placeholder="55-char lowercase seed"
          className="max-w-md"
        />
        <Button variant="primary" disabled={connecting} onClick={() => void connect(seed)}>
          {connecting ? 'connecting...' : 'connect wallet'}
        </Button>
      </div>
      <ErrorText>{error}</ErrorText>
    </div>
  )
}
