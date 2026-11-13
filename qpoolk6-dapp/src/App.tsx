import { useState } from 'react'
import { WalletProvider } from './context/WalletContext'
import { EpochBanner } from './components/EpochBanner'
import { WalletPanel } from './components/WalletPanel'
import { PoolsTab } from './components/PoolsTab'
import { StatsPanel } from './components/StatsPanel'
import { RegisterDividendPayerForm } from './components/RegisterDividendPayerForm'
import { ReleaseManagedSharesForm } from './components/ReleaseManagedSharesForm'
import { WalletLookup } from './components/WalletLookup'
import { Marketplace } from './components/Marketplace'
import { QxMarket } from './components/QxMarket'

const TABS = ['Pools', 'Market', 'Tools', 'Stats'] as const
type Tab = (typeof TABS)[number]

function Logo() {
  return (
    <div className="flex h-8 w-8 items-center justify-center rounded-lg bg-gradient-to-br from-primary to-primary/60 shadow-[0_4px_12px_-2px_rgba(97,240,254,0.5)]">
      <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="#0c131b" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round">
        <circle cx="12" cy="12" r="9" />
        <path d="M12 7v10M8.5 9.5h5a2 2 0 1 1 0 4h-5m0 0h5a2 2 0 1 1 0 4h-5" />
      </svg>
    </div>
  )
}

export default function App() {
  const [tab, setTab] = useState<Tab>('Pools')

  return (
    <WalletProvider>
      <div className="min-h-screen max-w-5xl mx-auto">
        <div className="sticky top-0 z-50">
          <EpochBanner />
          <header className="bg-background/95 backdrop-blur border-b border-border px-4 py-3 flex items-center gap-3">
            <Logo />
            <div>
              <h1 className="text-base font-bold leading-tight">QPoolK6 testnet explorer</h1>
              <p className="text-xs text-muted-foreground leading-tight">
                A live devnet running QPoolK6 -- browse real pools and submit real signed transactions against it.
              </p>
            </div>
          </header>
        </div>
        <WalletPanel />
        <nav className="flex gap-1 px-3 pt-2 border-b border-border bg-background/60">
          {TABS.map((t) => (
            <button
              key={t}
              onClick={() => setTab(t)}
              className={`relative px-3.5 py-2 text-sm font-medium transition-colors ${
                tab === t ? 'text-foreground' : 'text-muted-foreground hover:text-foreground'
              }`}
            >
              {t}
              {tab === t && <span className="absolute bottom-0 left-2 right-2 h-0.5 rounded-full bg-primary" />}
            </button>
          ))}
        </nav>
        <main className="p-4">
          {tab === 'Pools' && <PoolsTab />}
          {tab === 'Market' && (
            <div className="space-y-4">
              <Marketplace />
              <QxMarket />
            </div>
          )}
          {tab === 'Tools' && (
            <div className="space-y-4">
              <WalletLookup />
              <RegisterDividendPayerForm />
              <ReleaseManagedSharesForm />
            </div>
          )}
          {tab === 'Stats' && <StatsPanel />}
        </main>
      </div>
    </WalletProvider>
  )
}
