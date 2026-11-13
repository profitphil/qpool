// There is no "list every issued asset" query anywhere on this chain (QX's
// own order-book functions all require already knowing issuer+name, and
// core-lite's /live/v1/assets/ownerships HTTP route -- the one endpoint that
// could enumerate the whole universe -- has a live bug: it always computes a
// filter-by-issuance-index even when no issuer/name was given, so it silently
// returns zero rows; confirmed live, not something fixable from this
// frontend). The one thing we DO have is a fixed, known roster of every
// identity this devkit's own scripts ever fund -- so we ask each of THOSE
// directly what they hold, which finds any asset regardless of whether it
// ever touched a pool. This is complete for this closed devnet (nobody holds
// an asset here who isn't one of these identities) but will miss a token
// held only by a brand new, never-listed-here wallet.
import { getOwnedAssets } from './rpc'
import { nameToU64 } from './codec'
import { DEMO_WALLETS } from './demoWallets'
import { mapLimited, SCAN_CONCURRENCY } from './scan'

export const QX_ASSET_ISSUER = 'XNRSGVWMDYZVPFIBJHDSZURWXPRDFLZHJEABSANYKFRCGBEFSHWFGXQCOTNG'

export const KNOWN_IDENTITIES: { label: string; identity: string }[] = [
  ...DEMO_WALLETS.map((w) => ({ label: w.label.replace(/ \(.*\)/, ''), identity: w.identity })),
  { label: 'DEMO token issuer', identity: QX_ASSET_ISSUER },
]

export interface KnownAsset {
  issuer: string
  assetName: bigint
  assetNameStr: string
  holders: { label: string; identity: string; units: bigint; managingContractIndex: number }[]
  totalUnits: bigint
}

export async function scanKnownAssetCatalog(): Promise<KnownAsset[]> {
  const perWallet = await mapLimited(KNOWN_IDENTITIES, SCAN_CONCURRENCY, (w) => getOwnedAssets(w.identity))
  const byKey = new Map<string, KnownAsset>()
  KNOWN_IDENTITIES.forEach((w, i) => {
    for (const a of perWallet[i]) {
      if (a.numberOfUnits === 0n) continue
      const key = `${a.issuerIdentity}:${a.name}`
      let entry = byKey.get(key)
      if (!entry) {
        entry = { issuer: a.issuerIdentity, assetName: nameToU64(a.name), assetNameStr: a.name, holders: [], totalUnits: 0n }
        byKey.set(key, entry)
      }
      entry.holders.push({ label: w.label, identity: w.identity, units: a.numberOfUnits, managingContractIndex: a.managingContractIndex })
      entry.totalUnits += a.numberOfUnits
    }
  })
  return Array.from(byKey.values()).sort((a, b) => a.assetNameStr.localeCompare(b.assetNameStr))
}
