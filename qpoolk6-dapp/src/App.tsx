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
        <header className="p-4 border-b border-neutral-300 dark:border-neutral-700">
          <h1 className="text-lg font-bold">QPoolK6 testnet explorer</h1>
          <p className="text-xs text-neutral-500">
            A live devnet running QPoolK6 -- browse real pools and submit real signed transactions against it.
          </p>
        </header>
        <WalletPanel />
        <nav className="flex gap-1 p-3 border-b border-neutral-300 dark:border-neutral-700">
          {TABS.map((t) => (
            <Button key={t} onClick={() => setTab(t)} className={tab === t ? 'bg-neutral-200 dark:bg-neutral-700' : ''}>
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
