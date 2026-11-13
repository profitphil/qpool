import { useState } from 'react'
import { useWallet } from '../context/WalletContext'
import { DEMO_WALLETS } from '../qubic/demoWallets'
import { AssetList } from './AssetList'
import { Avatar, Button, ErrorText, Field, Identity, Select, TextInput } from './ui'

function Divider({ label }: { label: string }) {
  return (
    <div className="flex items-center gap-3 py-1">
      <div className="h-px flex-1 bg-border" />
      <span className="text-[10.5px] font-bold uppercase tracking-wider text-muted-foreground">{label}</span>
      <div className="h-px flex-1 bg-border" />
    </div>
  )
}

export function WalletPanel() {
  const { wallet, balance, connecting, error, connect, disconnect, refreshBalance } = useWallet()
  const [seed, setSeed] = useState('')
  const [demoIndex, setDemoIndex] = useState(0)

  if (wallet) {
    return (
      <div className="bg-card border border-border rounded-[18px] shadow-[0_2px_8px_rgba(15,23,42,0.04),0_12px_32px_-18px_rgba(15,23,42,0.14)] p-4 space-y-2.5 text-sm">
        <div className="flex items-center gap-3 flex-wrap">
          <Avatar seed={wallet.identity} size={34} />
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
    <div className="flex justify-center py-6">
      <div className="w-full max-w-[448px] bg-card border border-border rounded-[22px] shadow-[0_24px_70px_-20px_rgba(15,23,42,0.18)] p-8 space-y-4">
        <div className="space-y-1">
          <div className="text-[10.5px] font-bold uppercase tracking-wider text-accent-foreground">Connect wallet</div>
          <h2 className="text-xl font-extrabold">Sign in to QPoolK6</h2>
          <p className="text-[13px] text-muted-foreground">Pick a pre-funded demo wallet to get started in one click.</p>
        </div>

        <div className="space-y-2">
          <Field label="Demo wallet" hint="Shared pool -- if someone else is on the same number, pick a different one.">
            <Select value={demoIndex} onChange={(e) => setDemoIndex(Number(e.target.value))}>
              {DEMO_WALLETS.map((w, i) => (
                <option key={w.identity} value={i}>
                  {w.label}
                </option>
              ))}
            </Select>
          </Field>
          <Button
            variant="primary"
            loading={connecting}
            onClick={() => void connect(DEMO_WALLETS[demoIndex].seed)}
            className="w-full"
          >
            Connect demo wallet
          </Button>
        </div>

        <Divider label="Use with care" />

        <div className="space-y-2">
          <Field label="Your own seed phrase">
            <TextInput
              type="password"
              value={seed}
              onChange={(e) => setSeed(e.target.value)}
              placeholder="55-char lowercase seed"
            />
          </Field>
          <Button loading={connecting} disabled={!seed} onClick={() => void connect(seed)} className="w-full">
            Connect with seed
          </Button>
        </div>

        <ErrorText>{error}</ErrorText>

        <div className="flex items-start gap-1.5 text-[11px] text-amber-700 bg-amber-50 border border-amber-200 rounded-lg px-2.5 py-2">
          <span className="leading-none mt-px">&#9888;</span>
          <span>
            Testnet only. Your seed is kept in this browser tab's memory and used only to sign transactions locally --
            it is never sent anywhere, and is forgotten when you close or reload this tab. Never use a seed that
            protects anything real.
          </span>
        </div>
      </div>
    </div>
  )
}
