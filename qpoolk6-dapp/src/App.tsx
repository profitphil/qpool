import { useState } from 'react'
import { WalletProvider } from './context/WalletContext'
import { WalletPanel } from './components/WalletPanel'
import { PoolsTab } from './components/PoolsTab'
import { StatsPanel } from './components/StatsPanel'
import { RegisterDividendPayerForm } from './components/RegisterDividendPayerForm'
import { ReleaseManagedSharesForm } from './components/ReleaseManagedSharesForm'
import { Button } from './components/ui'

const TABS = ['Pools', 'Tools', 'Stats'] as const
type Tab = (typeof TABS)[number]

export default function App() {
  const [tab, setTab] = useState<Tab>('Pools')

  return (
    <WalletProvider>
      <div className="min-h-screen max-w-5xl mx-auto">
        <header className="sticky top-0 z-50 bg-background border-b border-border p-4">
          <h1 className="text-lg font-bold">QPoolK6 testnet explorer</h1>
          <p className="text-xs text-muted-foreground">
            A live devnet running QPoolK6 -- browse real pools and submit real signed transactions against it.
          </p>
        </header>
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
          {tab === 'Tools' && (
            <div className="space-y-4">
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
