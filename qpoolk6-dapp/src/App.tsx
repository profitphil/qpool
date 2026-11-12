import { useState } from 'react'
import { WalletProvider } from './context/WalletContext'
import { EpochBanner } from './components/EpochBanner'
import { WalletPanel } from './components/WalletPanel'
import { PoolsTab } from './components/PoolsTab'
import { StatsPanel } from './components/StatsPanel'
import { RegisterDividendPayerForm } from './components/RegisterDividendPayerForm'
import { ReleaseManagedSharesForm } from './components/ReleaseManagedSharesForm'
import { WalletLookup } from './components/WalletLookup'
import { QxMarket } from './components/QxMarket'
import { Button } from './components/ui'

const TABS = ['Pools', 'Market', 'Tools', 'Stats'] as const
type Tab = (typeof TABS)[number]

export default function App() {
  const [tab, setTab] = useState<Tab>('Pools')

  return (
    <WalletProvider>
      <div className="min-h-screen max-w-5xl mx-auto">
        <div className="sticky top-0 z-50">
          <EpochBanner />
          <header className="bg-background border-b border-border p-4">
            <h1 className="text-lg font-bold">QPoolK6 testnet explorer</h1>
            <p className="text-xs text-muted-foreground">
              A live devnet running QPoolK6 -- browse real pools and submit real signed transactions against it.
            </p>
          </header>
        </div>
        <WalletPanel />
        <nav className="flex gap-1 p-3 border-b border-border">
          {TABS.map((t) => (
            <Button key={t} variant={tab === t ? 'primary' : 'secondary'} onClick={() => setTab(t)}>
              {t}
            </Button>
          ))}
        </nav>
        <main className="p-4">
          {tab === 'Pools' && <PoolsTab />}
          {tab === 'Market' && <QxMarket />}
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
