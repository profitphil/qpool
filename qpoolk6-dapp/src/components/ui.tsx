import type { ReactNode } from 'react'
import { useState } from 'react'

function cx(...parts: Array<string | false | null | undefined>): string {
  return parts.filter(Boolean).join(' ')
}

const TONE_COLORS: Record<string, string> = {
  info: '#b0f9fe',
  success: '#47cd89',
  warning: '#cda747',
  danger: '#f97066',
  neutral: '#bdbdbd',
}

export function Card({
  title, icon, iconTone = 'info', right, interactive = false, children,
}: {
  title?: ReactNode
  icon?: ReactNode
  iconTone?: 'info' | 'success' | 'warning' | 'danger' | 'neutral'
  right?: ReactNode
  interactive?: boolean
  children: ReactNode
}) {
  const c = TONE_COLORS[iconTone]
  return (
    <div
      className={cx(
        'bg-card text-card-foreground border border-border rounded-[18px] shadow-[0_1px_0_rgba(255,255,255,0.04)_inset,0_8px_20px_-10px_rgba(0,0,0,0.6)] overflow-hidden',
        interactive && 'transition-all duration-200 hover:border-primary/40 hover:shadow-[0_1px_0_rgba(255,255,255,0.04)_inset,0_12px_28px_-10px_rgba(176,249,254,0.15)]',
      )}
    >
      {(title || right) && (
        <div className="flex items-center justify-between gap-2 px-4 py-3 border-b border-border/70 bg-muted/30">
          <h3 className="flex items-center gap-2 font-semibold text-[11.5px] uppercase tracking-wider text-muted-foreground">
            {icon && (
              <span
                className="flex h-6 w-6 items-center justify-center rounded-lg shrink-0"
                style={{ backgroundColor: `${c}17`, color: c }}
              >
                {icon}
              </span>
            )}
            {title}
          </h3>
          {right}
        </div>
      )}
      <div className="p-4 space-y-3">{children}</div>
    </div>
  )
}

export function Field({ label, hint, children }: { label: string; hint?: string; children: ReactNode }) {
  return (
    <label className="block space-y-1">
      <span className="text-xs text-muted-foreground">{label}</span>
      {children}
      {hint && <span className="block text-[11px] text-muted-foreground/70">{hint}</span>}
    </label>
  )
}

export function TextInput(props: React.InputHTMLAttributes<HTMLInputElement>) {
  return (
    <input
      {...props}
      className={`border border-border bg-input/50 rounded-lg px-2.5 py-1.5 w-full text-sm font-mono text-foreground placeholder:text-muted-foreground outline-none transition-shadow focus:ring-2 focus:ring-ring/40 focus:border-ring/60 ${props.className ?? ''}`}
    />
  )
}

export function Select(props: React.SelectHTMLAttributes<HTMLSelectElement>) {
  return (
    <select
      {...props}
      className={`border border-border bg-input/50 rounded-lg px-2.5 py-1.5 w-full text-sm text-foreground outline-none transition-shadow focus:ring-2 focus:ring-ring/40 focus:border-ring/60 ${props.className ?? ''}`}
    />
  )
}

export function Spinner({ className = '' }: { className?: string }) {
  return (
    <svg className={`animate-spin ${className}`} viewBox="0 0 24 24" fill="none" width="14" height="14">
      <circle cx="12" cy="12" r="9" stroke="currentColor" strokeOpacity="0.25" strokeWidth="3" />
      <path d="M21 12a9 9 0 0 0-9-9" stroke="currentColor" strokeWidth="3" strokeLinecap="round" />
    </svg>
  )
}

export function Button({
  children, variant = 'secondary', size = 'md', loading = false, icon, ...props
}: React.ButtonHTMLAttributes<HTMLButtonElement> & {
  variant?: 'primary' | 'secondary' | 'ghost' | 'danger'
  size?: 'sm' | 'md'
  loading?: boolean
  icon?: ReactNode
}) {
  const base = 'inline-flex items-center justify-center gap-1.5 rounded-xl font-semibold transition-all duration-150 disabled:opacity-40 disabled:cursor-not-allowed active:scale-[0.97]'
  const sizes = size === 'sm' ? 'px-2.5 py-1 text-xs' : 'px-3.5 py-2 text-sm'
  const styles = {
    primary: 'bg-primary text-primary-foreground shadow-[0_1px_0_rgba(255,255,255,0.4)_inset] hover:shadow-[0_1px_0_rgba(255,255,255,0.4)_inset,0_6px_16px_-4px_rgba(176,249,254,0.5)] hover:bg-primary/90',
    secondary: 'border border-border bg-card text-foreground hover:border-primary/40 hover:bg-accent/40',
    ghost: 'text-muted-foreground hover:text-foreground hover:bg-muted',
    danger: 'border border-destructive/30 bg-destructive/5 text-destructive hover:bg-destructive/10',
  }[variant]
  return (
    <button {...props} disabled={props.disabled || loading} className={`${base} ${sizes} ${styles} ${props.className ?? ''}`}>
      {loading ? <Spinner /> : icon}
      {children}
    </button>
  )
}

export function Badge({
  children, color = '#b0f9fe', tone, dot = false,
}: { children: ReactNode; color?: string; tone?: 'success' | 'warning' | 'info' | 'danger' | 'neutral'; dot?: boolean }) {
  const c = tone ? TONE_COLORS[tone] : color
  return (
    <span
      className="inline-flex items-center gap-1 rounded-full px-2 py-0.5 text-[11px] font-medium border"
      style={{ backgroundColor: `${c}14`, color: c, borderColor: `${c}33` }}
    >
      {dot && <span className="h-1.5 w-1.5 rounded-full" style={{ backgroundColor: c }} />}
      {children}
    </span>
  )
}

export function ErrorText({ children }: { children: ReactNode }) {
  if (!children) return null
  return (
    <div className="flex items-start gap-1.5 text-destructive text-xs whitespace-pre-wrap bg-destructive/5 border border-destructive/20 rounded-lg px-2 py-1.5">
      <span className="leading-none mt-px">&#9888;</span>
      {children}
    </div>
  )
}

export function SuccessText({ children }: { children: ReactNode }) {
  if (!children) return null
  return (
    <div className="flex items-start gap-1.5 text-success text-xs whitespace-pre-wrap bg-success/5 border border-success/20 rounded-lg px-2 py-1.5">
      <span className="leading-none mt-px">&#10003;</span>
      {children}
    </div>
  )
}

export function KV({ k, v }: { k: ReactNode; v: ReactNode }) {
  return (
    <div className="flex justify-between items-baseline gap-3 text-xs py-1.5 border-b border-border/50 last:border-0">
      <span className="text-muted-foreground shrink-0">{k}</span>
      <span className="font-mono text-right break-all text-foreground/90">{v}</span>
    </div>
  )
}

// A 60-char Qubic identity, truncated with a one-click copy -- raw full-length
// addresses dumped inline are the single biggest "unfinished" tell in a wallet UI.
export function Identity({ id, label }: { id: string; label?: string }) {
  const [copied, setCopied] = useState(false)
  const short = id.length > 16 ? `${id.slice(0, 6)}…${id.slice(-6)}` : id
  async function copy() {
    await navigator.clipboard.writeText(id)
    setCopied(true)
    setTimeout(() => setCopied(false), 1200)
  }
  return (
    <button
      type="button"
      onClick={() => void copy()}
      title={id}
      className="inline-flex items-center gap-1 font-mono text-xs text-foreground/80 hover:text-primary transition-colors group"
    >
      {label && <span className="text-muted-foreground">{label}</span>}
      {short}
      <span className="opacity-0 group-hover:opacity-100 transition-opacity text-[10px]">{copied ? '✓' : '⧉'}</span>
    </button>
  )
}

const AVATAR_HUES = [191, 262, 330, 24, 142, 48, 280, 10]
function hashHue(seed: string): number {
  let h = 0
  for (let i = 0; i < seed.length; i++) h = (h * 31 + seed.charCodeAt(i)) >>> 0
  return AVATAR_HUES[h % AVATAR_HUES.length]
}

// Deterministic colored initials -- gives every token/identity a stable visual
// anchor instead of a wall of identical monospace text.
export function Avatar({ seed, size = 32 }: { seed: string; size?: number }) {
  const hue = hashHue(seed)
  const initials = seed.replace(/[^A-Za-z0-9]/g, '').slice(0, 2).toUpperCase() || '??'
  return (
    <div
      className="flex items-center justify-center rounded-full font-bold shrink-0 text-black/80"
      style={{
        width: size, height: size, fontSize: size * 0.34,
        background: `linear-gradient(135deg, hsl(${hue} 85% 65%), hsl(${hue} 85% 45%))`,
      }}
    >
      {initials}
    </div>
  )
}

export function EmptyState({ icon = '○', title, hint }: { icon?: ReactNode; title: string; hint?: string }) {
  return (
    <div className="flex flex-col items-center justify-center text-center gap-1 py-8 text-muted-foreground">
      <div className="text-2xl opacity-40">{icon}</div>
      <div className="text-sm">{title}</div>
      {hint && <div className="text-xs opacity-70 max-w-xs">{hint}</div>}
    </div>
  )
}

export function Skeleton({ className = '' }: { className?: string }) {
  return <div className={`animate-pulse rounded-lg bg-muted ${className}`} />
}

export function Stat({ label, value, accent }: { label: string; value: ReactNode; accent?: string }) {
  return (
    <div className="space-y-0.5">
      <div className="text-[11px] uppercase tracking-wide text-muted-foreground">{label}</div>
      <div className="text-lg font-semibold tabular-nums" style={accent ? { color: accent } : undefined}>
        {value}
      </div>
    </div>
  )
}
