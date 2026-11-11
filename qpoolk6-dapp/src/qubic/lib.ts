// Single point of contact with @qubic-lib/qubic-ts-library. Import
// everything through its one default export, not deep sub-paths --
// mixing deep-path named imports (as this package's OWN internal files use
// internally, via CJS require()) with ESM named imports from the same
// files confused Vite's dependency pre-bundler (a real, reproducible
// "does not provide an export named ..." error, independent of esbuild vs
// Rolldown -- reproduced on both). One default import sidesteps it entirely.
import QubicLib from '@qubic-lib/qubic-ts-library'

export const QubicHelper = QubicLib.QubicHelper
export const QubicTransaction = QubicLib.QubicTransaction
export const DynamicPayload = QubicLib.DynamicPayload
export const PublicKey = QubicLib.PublicKey
export const Long = QubicLib.Long
