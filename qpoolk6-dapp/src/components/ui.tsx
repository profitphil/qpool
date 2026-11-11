import type { ReactNode } from 'react'

export function Card({ title, children }: { title?: string; children: ReactNode }) {
  return (
    <div className="border border-neutral-300 dark:border-neutral-700 rounded-lg p-4 space-y-3">
      {title && <h3 className="font-semibold text-sm uppercase tracking-wide text-neutral-500">{title}</h3>}
      {children}
    </div>
  )
}

export function Field({ label, children }: { label: string; children: ReactNode }) {
  return (
    <label className="block space-y-1">
      <span className="text-xs text-neutral-500">{label}</span>
      {children}
    </label>
  )
}

export function TextInput(props: React.InputHTMLAttributes<HTMLInputElement>) {
  return (
    <input
      {...props}
      className={`border border-neutral-300 dark:border-neutral-700 bg-transparent rounded px-2 py-1 w-full text-sm font-mono ${props.className ?? ''}`}
    />
  )
}

export function Select(props: React.SelectHTMLAttributes<HTMLSelectElement>) {
  return (
    <select
      {...props}
      className={`border border-neutral-300 dark:border-neutral-700 bg-transparent rounded px-2 py-1 w-full text-sm ${props.className ?? ''}`}
    />
  )
}

export function Button({ children, ...props }: React.ButtonHTMLAttributes<HTMLButtonElement>) {
  return (
    <button
      {...props}
      className={`border border-neutral-400 dark:border-neutral-600 rounded px-3 py-1.5 text-sm font-medium hover:bg-neutral-100 dark:hover:bg-neutral-800 disabled:opacity-40 disabled:cursor-not-allowed ${props.className ?? ''}`}
    >
      {children}
    </button>
  )
}

export function ErrorText({ children }: { children: ReactNode }) {
  if (!children) return null
  return <div className="text-red-600 dark:text-red-400 text-xs whitespace-pre-wrap">{String(children)}</div>
}

export function SuccessText({ children }: { children: ReactNode }) {
  if (!children) return null
  return <div className="text-green-700 dark:text-green-400 text-xs whitespace-pre-wrap">{children}</div>
}

export function KV({ k, v }: { k: string; v: ReactNode }) {
  return (
    <div className="flex justify-between gap-2 text-xs py-0.5 border-b border-neutral-200 dark:border-neutral-800 last:border-0">
      <span className="text-neutral-500">{k}</span>
      <span className="font-mono text-right break-all">{v}</span>
    </div>
  )
}
