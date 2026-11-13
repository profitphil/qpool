import { useState } from 'react'
import type { KnownAsset } from '../qubic/knownAssets'
import { Avatar, Identity, TextInput } from './ui'
import { IconSearch } from './icons'

// Lets you pick a known token by name instead of having to already know and
// paste its 60-char issuer identity -- the issuer/symbol fields stay below,
// editable, so picking one is a shortcut, not the only way in.
export function AssetPicker({ assets, onSelect }: { assets: KnownAsset[]; onSelect: (issuer: string, symbol: string) => void }) {
  const [query, setQuery] = useState('')
  const [open, setOpen] = useState(false)
  const matches = assets.filter((a) => a.assetNameStr.toUpperCase().includes(query.trim().toUpperCase()))

  function pick(a: KnownAsset) {
    onSelect(a.issuer, a.assetNameStr)
    setQuery(a.assetNameStr)
    setOpen(false)
  }

  return (
    <div className="relative">
      <div className="relative">
        <span className="absolute left-2.5 top-1/2 -translate-y-1/2 text-muted-foreground"><IconSearch /></span>
        <TextInput
          value={query}
          onFocus={() => setOpen(true)}
          onBlur={() => setTimeout(() => setOpen(false), 120)}
          onChange={(e) => {
            setQuery(e.target.value)
            setOpen(true)
          }}
          placeholder={assets.length === 0 ? 'No known tokens yet' : 'Pick a known token...'}
          className="pl-8"
        />
      </div>
      {open && matches.length > 0 && (
        <div className="absolute z-10 mt-1 w-full bg-card border border-border rounded-xl shadow-[0_12px_32px_-12px_rgba(0,0,0,0.7)] max-h-56 overflow-auto">
          {matches.map((a, i) => (
            // A div, not a button -- Identity below is itself a button (the
            // copy affordance), and a button can't nest inside a button.
            <div
              key={i}
              role="button"
              tabIndex={0}
              onClick={() => pick(a)}
              onKeyDown={(e) => e.key === 'Enter' && pick(a)}
              className="w-full flex items-center gap-2 px-3 py-2 text-left text-sm hover:bg-accent/40 transition-colors cursor-pointer"
            >
              <Avatar seed={a.assetNameStr} size={22} />
              <span className="font-medium">{a.assetNameStr}</span>
              <span className="ml-auto" onClick={(e) => e.stopPropagation()}>
                <Identity id={a.issuer} />
              </span>
            </div>
          ))}
        </div>
      )}
    </div>
  )
}
