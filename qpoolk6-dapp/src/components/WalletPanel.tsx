import { useState, type ReactNode } from 'react'
import { useWallet } from '../context/WalletContext'
import { isValidSeedFormat } from '../qubic/identity'
import { DEMO_WALLETS } from '../qubic/demoWallets'
import { AssetList } from './AssetList'
import { Avatar, Button, ErrorText, Field, Identity, Select, TextInput } from './ui'
import { IconKey, IconMetaMask, IconVaultFile, IconWalletConnect } from './icons'

// Connect flow modeled on useqpay.com's dashboard login (itself ported from
// Qraffle's ConnectModal): a chooser -> one of a few method screens, rather
// than one flat form. MetaMask's Qubic snap and WalletConnect both need a
// real wallet app talking to a real network, so on this local testnet they're
// shown (for the same look) but explain why they can't work here instead of
// silently failing. Private Seed is just our existing connect() reskinned;
// Vault File is wired for real, using the same dynamically-imported
// @qubic-lib/qubic-ts-vault-library qpay's own page loads from esm.sh.
type Screen = 'choice' | 'seed' | 'vault' | 'accounts'

interface VaultAccount {
  publicId: string
  alias?: string
}

function Divider({ label }: { label: string }) {
  return (
    <div className="flex items-center gap-3 py-1">
      <div className="h-px flex-1 bg-border" />
      <span className="text-[10.5px] font-bold uppercase tracking-wider text-muted-foreground">{label}</span>
      <div className="h-px flex-1 bg-border" />
    </div>
  )
}

function ChoiceButton({ icon, label, onClick }: { icon: ReactNode; label: string; onClick: () => void }) {
  return (
    <button
      type="button"
      onClick={onClick}
      className="w-full flex items-center justify-center gap-2.5 border border-border rounded-xl px-4 py-3 text-sm font-semibold bg-card hover:bg-accent/40 hover:-translate-y-px transition-all"
    >
      <span className="text-primary">{icon}</span>
      {label}
    </button>
  )
}

export function WalletPanel() {
  const { wallet, balance, connecting, error, connect, disconnect, refreshBalance } = useWallet()
  const [demoIndex, setDemoIndex] = useState(0)
  const [screen, setScreen] = useState<Screen>('choice')
  const [notice, setNotice] = useState('')

  const [seedInput, setSeedInput] = useState('')
  const [seedErr, setSeedErr] = useState('')

  const [vaultFile, setVaultFile] = useState<File | null>(null)
  const [vaultPassword, setVaultPassword] = useState('')
  const [vaultErr, setVaultErr] = useState('')
  const [vaultBusy, setVaultBusy] = useState(false)
  // eslint-disable-next-line @typescript-eslint/no-explicit-any
  const [vaultInstance, setVaultInstance] = useState<any>(null)
  const [accounts, setAccounts] = useState<VaultAccount[]>([])

  if (wallet) {
    return (
      <div className="bg-card border border-border rounded-[18px] shadow-[0_1px_0_rgba(255,255,255,0.04)_inset,0_8px_20px_-10px_rgba(0,0,0,0.6)] p-4 space-y-2.5 text-sm">
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

  function goChoice() {
    setScreen('choice')
    setNotice('')
    setSeedInput('')
    setSeedErr('')
    setVaultFile(null)
    setVaultPassword('')
    setVaultErr('')
    setVaultInstance(null)
    setAccounts([])
  }

  async function unlockSeed() {
    setSeedErr('')
    if (!isValidSeedFormat(seedInput)) {
      setSeedErr('Seed must be exactly 55 lowercase letters (a-z).')
      return
    }
    try {
      await connect(seedInput)
    } catch (e) {
      setSeedErr(e instanceof Error ? e.message : String(e))
    }
  }

  function chooseVaultFile(e: React.ChangeEvent<HTMLInputElement>) {
    const file = e.target.files?.[0] ?? null
    if (file && !file.name.toLowerCase().endsWith('.qubic-vault')) {
      setVaultErr('Choose a .qubic-vault file.')
      setVaultFile(null)
      e.target.value = ''
      return
    }
    setVaultErr('')
    setVaultFile(file)
  }

  async function unlockVault() {
    if (!vaultFile) { setVaultErr('Choose a vault file first.'); return }
    if (!vaultPassword) { setVaultErr('Enter the vault password.'); return }
    setVaultErr('')
    setVaultBusy(true)
    try {
      // @ts-expect-error -- runtime-only ESM import, no local types for it
      const mod = await import(/* @vite-ignore */ 'https://esm.sh/@qubic-lib/qubic-ts-vault-library@1.0.2')
      const vault = new mod.QubicVault()
      await vault.importAndUnlock(true, vaultPassword, null, vaultFile, true)
      const seeds = vault.getSeeds()
      if (!seeds || seeds.length === 0) throw new Error('This vault has no accounts.')
      if (seeds.length === 1) {
        const seed = await vault.revealSeed(seeds[0].publicId)
        await connect(seed)
      } else {
        setVaultInstance(vault)
        setAccounts(seeds.map((s: VaultAccount) => ({ publicId: s.publicId, alias: s.alias })))
        setScreen('accounts')
      }
    } catch (e) {
      const msg = e instanceof Error ? e.message : String(e)
      setVaultErr(/password/i.test(msg) ? 'Wrong vault password.' : msg)
    } finally {
      setVaultBusy(false)
    }
  }

  async function pickAccount(publicId: string) {
    if (!vaultInstance) return
    const seed = await vaultInstance.revealSeed(publicId)
    await connect(seed)
  }

  return (
    <div className="flex justify-center py-6">
      <div className="w-full max-w-[448px] bg-card/90 backdrop-blur border border-border rounded-[22px] shadow-[0_1px_0_rgba(255,255,255,0.05)_inset,0_24px_60px_-20px_rgba(0,0,0,0.7)] p-8 space-y-4">
        <div className="space-y-1 text-center">
          <div className="text-[10.5px] font-bold uppercase tracking-wider text-primary">Sign in</div>
          <h2 className="text-xl font-extrabold">Connect to QPoolK6</h2>
          <p className="text-[13px] text-muted-foreground">Connect your Qubic wallet to get started.</p>
        </div>

        {screen === 'choice' && (
          <>
            <div className="space-y-2">
              <Field label="Demo wallet" hint="Shared pool -- if someone else is on the same number, pick a different one.">
                <Select value={demoIndex} onChange={(e) => setDemoIndex(Number(e.target.value))}>
                  {DEMO_WALLETS.map((w, i) => (
                    <option key={w.identity} value={i}>{w.label}</option>
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

            <Divider label="or connect a wallet" />

            <div className="space-y-2 text-left">
              <ChoiceButton
                icon={<IconMetaMask />}
                label="MetaMask"
                onClick={() => setNotice("MetaMask's Qubic snap needs a real wallet extension talking to a real network -- not available on this local testnet. Use Demo wallet or Private Seed instead.")}
              />
              <ChoiceButton
                icon={<IconWalletConnect />}
                label="Wallet Connect"
                onClick={() => setNotice('WalletConnect needs a live relay and a real wallet app on the other end -- not available on this local testnet. Use Demo wallet or Private Seed instead.')}
              />
              <Divider label="use with care" />
              <ChoiceButton icon={<IconKey />} label="Private Seed" onClick={() => { setNotice(''); setScreen('seed') }} />
              <ChoiceButton icon={<IconVaultFile />} label="Vault File" onClick={() => { setNotice(''); setScreen('vault') }} />
            </div>
            {notice && (
              <div className="text-[11px] text-warning bg-warning/10 border border-warning/25 rounded-lg px-2.5 py-2">{notice}</div>
            )}
          </>
        )}

        {screen === 'seed' && (
          <div className="space-y-2 text-left">
            <Field label="Your 55-character private key (seed)">
              <TextInput
                type="password"
                value={seedInput}
                onChange={(e) => { setSeedInput(e.target.value); setSeedErr('') }}
                autoComplete="off"
                spellCheck={false}
                placeholder="55-char lowercase seed"
              />
            </Field>
            <ErrorText>{seedErr || error}</ErrorText>
            <div className="grid grid-cols-2 gap-2">
              <Button variant="ghost" onClick={goChoice}>Cancel</Button>
              <Button variant="primary" loading={connecting} onClick={() => void unlockSeed()}>Unlock</Button>
            </div>
          </div>
        )}

        {screen === 'vault' && (
          <div className="space-y-2 text-left">
            <p className="text-[13px] text-muted-foreground text-center">Load your Qubic vault file:</p>
            <input id="vault-file-input" type="file" accept=".qubic-vault" className="hidden" onChange={chooseVaultFile} />
            <div className="flex items-center gap-2">
              <Button type="button" onClick={() => document.getElementById('vault-file-input')?.click()}>Choose File</Button>
              <div className="flex-1 min-w-0 truncate text-xs text-muted-foreground border border-border rounded-lg px-2.5 py-2 bg-input/30">
                {vaultFile ? vaultFile.name : 'No file selected'}
              </div>
            </div>
            <Field label="Vault password">
              <TextInput
                type="password"
                value={vaultPassword}
                onChange={(e) => setVaultPassword(e.target.value)}
                autoComplete="off"
              />
            </Field>
            <ErrorText>{vaultErr || error}</ErrorText>
            <div className="grid grid-cols-2 gap-2">
              <Button variant="ghost" onClick={goChoice}>Cancel</Button>
              <Button variant="primary" loading={vaultBusy || connecting} onClick={() => void unlockVault()}>Unlock</Button>
            </div>
          </div>
        )}

        {screen === 'accounts' && (
          <div className="space-y-2 text-left">
            <p className="text-[13px] text-muted-foreground text-center">Select an account:</p>
            <div className="space-y-2 max-h-64 overflow-y-auto">
              {accounts.map((a) => (
                <div
                  key={a.publicId}
                  role="button"
                  tabIndex={0}
                  onClick={() => void pickAccount(a.publicId)}
                  onKeyDown={(e) => e.key === 'Enter' && void pickAccount(a.publicId)}
                  className="border border-border rounded-xl p-3 hover:bg-accent/40 transition-colors cursor-pointer"
                >
                  {a.alias && <div className="text-xs font-semibold text-muted-foreground mb-0.5">{a.alias}</div>}
                  <span onClick={(e) => e.stopPropagation()}>
                    <Identity id={a.publicId} />
                  </span>
                </div>
              ))}
            </div>
            <Button variant="ghost" className="w-full" onClick={goChoice}>Cancel</Button>
          </div>
        )}

        <div className="flex items-start gap-1.5 text-[11px] text-warning bg-warning/10 border border-warning/25 rounded-lg px-2.5 py-2">
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
