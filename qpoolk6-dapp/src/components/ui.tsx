import type { ReactNode } from 'react'

export function Card({ title, children }: { title?: string; children: ReactNode }) {
  return (
    <div className="bg-card text-card-foreground border border-border rounded-xl shadow-sm p-4 space-y-3">
      {title && <h3 className="font-semibold text-xs uppercase tracking-wide text-muted-foreground">{title}</h3>}
      {children}
    </div>
  )
}

export function Field({ label, children }: { label: string; children: ReactNode }) {
  return (
    <label className="block space-y-1">
      <span className="text-xs text-muted-foreground">{label}</span>
      {children}
    </label>
  )
}

export function TextInput(props: React.InputHTMLAttributes<HTMLInputElement>) {
  return (
    <input
      {...props}
      className={`border border-border bg-input/50 rounded-md px-2 py-1.5 w-full text-sm font-mono text-foreground placeholder:text-muted-foreground outline-none focus:ring-2 focus:ring-ring/50 focus:border-ring ${props.className ?? ''}`}
    />
  )
}

export function Select(props: React.SelectHTMLAttributes<HTMLSelectElement>) {
  return (
    <select
      {...props}
      className={`border border-border bg-input/50 rounded-md px-2 py-1.5 w-full text-sm text-foreground outline-none focus:ring-2 focus:ring-ring/50 focus:border-ring ${props.className ?? ''}`}
    />
  )
}

export function Button({
  children,
  variant = 'secondary',
  ...props
}: React.ButtonHTMLAttributes<HTMLButtonElement> & { variant?: 'primary' | 'secondary' }) {
  const base = 'rounded-md px-3 py-1.5 text-sm font-medium transition-colors disabled:opacity-40 disabled:cursor-not-allowed'
  const styles =
    variant === 'primary'
      ? 'bg-primary text-primary-foreground hover:bg-primary/90'
      : 'border border-border bg-input/30 text-foreground hover:bg-input/60'
  return (
    <button {...props} className={`${base} ${styles} ${props.className ?? ''}`}>
      {children}
    </button>
  )
}

export function Badge({ children, color = '#61f0fe' }: { children: ReactNode; color?: string }) {
  return (
    <span
      className="rounded-full px-2 py-0.5 text-xs font-medium"
      style={{ backgroundColor: `${color}1a`, color }}
    >
      {children}
    </span>
  )
}

export function ErrorText({ children }: { children: ReactNode }) {
  if (!children) return null
  return <div className="text-destructive text-xs whitespace-pre-wrap">{String(children)}</div>
}

export function SuccessText({ children }: { children: ReactNode }) {
  if (!children) return null
  return <div className="text-success text-xs whitespace-pre-wrap">{children}</div>
}

export function KV({ k, v }: { k: string; v: ReactNode }) {
  return (
    <div className="flex justify-between gap-2 text-xs py-1 border-b border-border/60 last:border-0">
      <span className="text-muted-foreground">{k}</span>
      <span className="font-mono text-right break-all">{v}</span>
    </div>
  )
}
