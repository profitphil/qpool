// Wallet derivation from a seed. TESTNET ONLY: the seed is kept in memory and
// (if the caller opts in) localStorage -- it is NEVER sent to any server; all
// signing happens in this browser tab.
import { QubicHelper } from './lib'

const qHelper = new QubicHelper()

export interface Wallet {
  seed: string
  identity: string
  publicKey: Uint8Array
}

const SEED_PATTERN = /^[a-z]{55}$/

export function isValidSeedFormat(seed: string): boolean {
  return SEED_PATTERN.test(seed)
}

export async function deriveWallet(seed: string): Promise<Wallet> {
  if (!isValidSeedFormat(seed)) {
    throw new Error('Seed must be exactly 55 lowercase letters (a-z).')
  }
  const pkg = await qHelper.createIdPackage(seed)
  return { seed, identity: pkg.publicId, publicKey: pkg.publicKey }
}
