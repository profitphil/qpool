import { useState } from 'react'
import { useWallet } from '../context/WalletContext'
import { DEMO_WALLETS } from '../qubic/demoWallets'
import { AssetList } from './AssetList'
import { Avatar, Button, ErrorText, Field, Identity, Select, TextInput } from './ui'

export function WalletPanel() {
  const { wallet, balance, connecting, error, connect, disconnect, refreshBalance } = useWallet()
  const [seed, setSeed] = useState('')
  const [demoIndex, setDemoIndex] = useState(0)

  if (wallet) {
    return (
      <div className="bg-gradient-to-b from-card to-card/60 border-b border-border p-3 space-y-2.5 text-sm">
        <div className="flex items-center gap-3 flex-wrap">
          <Avatar seed={wallet.identity} size={30} />
          <div className="min-w-0">
            <Identity id={wallet.identity} />
            <div className="font-mono text-base font-semibold tabular-nums">
              {balance?.toLocaleString()} <span className="text-xs text-muted-foreground font-normal">QU</span>
            </div>
          </div>
          <div className="ml-auto flex gap-2">
            <Button size="sm" onClick={() => void refreshBalance()}>refresh</Button>
            <Button size="sm" variant="ghost" onClick={disconnect}>disconnect</Button>
          </div>
        </div>
        <AssetList identity={wallet.identity} />
      </div>
    )
  }

  return (
    <div className="border-b border-border p-3 space-y-2.5">
      <div className="flex items-start gap-1.5 text-xs text-amber-400/90 bg-amber-400/5 border border-amber-400/20 rounded-lg px-2.5 py-2">
        <span className="leading-none mt-px">&#9888;</span>
        <span>
          Testnet only. Your seed is kept in this browser tab's memory and used only to sign transactions locally --
          it is never sent anywhere, and is forgotten when you close or reload this tab. Never paste a seed that
          protects anything real.
        </span>
      </div>
      <div className="flex gap-2 items-end flex-wrap">
        <Field label="No seed of your own? Try a pre-funded demo wallet" hint="Shared pool -- if someone else is on the same number, pick a different one.">
          <Select value={demoIndex} onChange={(e) => setDemoIndex(Number(e.target.value))} className="max-w-md">
            {DEMO_WALLETS.map((w, i) => (
              <option key={w.identity} value={i}>
                {w.label}
              </option>
            ))}
          </Select>
        </Field>
        <Button variant="primary" loading={connecting} onClick={() => void connect(DEMO_WALLETS[demoIndex].seed)}>
          use this demo wallet
        </Button>
      </div>
      <div className="flex gap-2 items-start flex-wrap">
        <TextInput
          type="password"
          value={seed}
          onChange={(e) => setSeed(e.target.value)}
          placeholder="or paste your own 55-char lowercase seed"
          className="max-w-md"
        />
        <Button loading={connecting} onClick={() => void connect(seed)}>
          connect wallet
        </Button>
      </div>
      <ErrorText>{error}</ErrorText>
    </div>
  )
}
