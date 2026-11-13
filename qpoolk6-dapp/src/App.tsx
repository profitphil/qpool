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
import { IpoBoard } from './components/IpoBoard'
import { IconBook, IconPool, IconSeat, IconStats, IconTicket, IconTools } from './components/icons'

const TABS = [
  { key: 'Pools', label: 'Pools', icon: <IconPool />, tone: '#b0f9fe' },
  { key: 'QX', label: 'QX', icon: <IconBook />, tone: '#f97066' },
  { key: 'IPO', label: 'IPO', icon: <IconTicket />, tone: '#cda747' },
  { key: 'Marketplace', label: 'Marketplace', icon: <IconSeat />, tone: '#47cd89' },
  { key: 'Tools', label: 'Tools', icon: <IconTools />, tone: '#9b8cf0' },
  { key: 'Stats', label: 'Stats', icon: <IconStats />, tone: '#bdbdbd' },
] as const
type Tab = (typeof TABS)[number]['key']

function Logo() {
  return (
    <div className="flex h-9 w-9 items-center justify-center rounded-xl bg-gradient-to-br from-primary to-primary/70 shadow-[0_4px_12px_-2px_rgba(176,249,254,0.4)]">
      <svg width="19" height="19" viewBox="0 0 24 24" fill="none" stroke="#0c131b" strokeWidth="2.4" strokeLinecap="round" strokeLinejoin="round">
        <circle cx="12" cy="12" r="9" />
        <path d="M12 7v10M8.5 9.5h5a2 2 0 1 1 0 4h-5m0 0h5a2 2 0 1 1 0 4h-5" />
      </svg>
    </div>
  )
}

function Sidebar({ tab, setTab }: { tab: Tab; setTab: (t: Tab) => void }) {
  return (
    <aside className="md:w-56 shrink-0">
      <div className="md:sticky md:top-[76px] bg-card/90 backdrop-blur border border-border rounded-2xl shadow-[0_1px_0_rgba(255,255,255,0.04)_inset,0_8px_20px_-10px_rgba(0,0,0,0.6)] p-3 space-y-3">
        <div className="flex items-center gap-2 px-1 py-1">
          <Logo />
          <div className="min-w-0">
            <div className="text-sm font-extrabold leading-tight">QPoolK6</div>
            <div className="text-[11px] text-muted-foreground leading-tight">Testnet explorer</div>
          </div>
        </div>
        <div>
          <div className="px-2 pb-1 text-[10px] font-bold uppercase tracking-wider text-muted-foreground">Navigate</div>
          <nav className="flex md:flex-col gap-1 overflow-x-auto">
            {TABS.map((t) => {
              const active = tab === t.key
              return (
                <button
                  key={t.key}
                  onClick={() => setTab(t.key)}
                  className={`flex items-center gap-2.5 rounded-xl px-2.5 py-2 text-sm font-semibold whitespace-nowrap transition-colors ${
                    active ? 'bg-accent text-foreground' : 'text-muted-foreground hover:bg-muted hover:text-foreground'
                  }`}
                >
                  <span
                    className="flex h-7 w-7 items-center justify-center rounded-lg shrink-0"
                    style={{ backgroundColor: `${t.tone}17`, color: t.tone }}
                  >
                    {t.icon}
                  </span>
                  {t.label}
                </button>
              )
            })}
          </nav>
        </div>
      </div>
    </aside>
  )
}

export default function App() {
  const [tab, setTab] = useState<Tab>('Pools')

  return (
    <WalletProvider>
      <div className="min-h-screen">
        <div className="sticky top-0 z-50">
          <EpochBanner />
        </div>
        <div className="max-w-[1180px] mx-auto px-4 py-4 flex flex-col md:flex-row gap-4">
          <Sidebar tab={tab} setTab={setTab} />
          <main className="flex-1 min-w-0 space-y-4">
            <WalletPanel />
            {tab === 'Pools' && <PoolsTab />}
            {tab === 'QX' && <QxMarket />}
            {tab === 'IPO' && <IpoBoard />}
            {tab === 'Marketplace' && <Marketplace />}
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
      </div>
    </WalletProvider>
  )
}
