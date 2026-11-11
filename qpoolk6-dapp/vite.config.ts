import { fileURLToPath } from 'node:url'
import react from '@vitejs/plugin-react'
import tailwindcss from '@tailwindcss/vite'
import { defineConfig } from 'vite'

export default defineConfig({
  plugins: [react(), tailwindcss()],
  define: {
    global: 'globalThis',
  },
  resolve: {
    alias: {
      buffer: 'buffer',
      // @qubic-lib/qubic-ts-library's qubicHelper.js requires('bignumber.js')
      // via CJS. bignumber.js's own package.json "exports" map sends Vite's
      // resolver to its ESM build (bignumber.mjs) regardless -- a dual-package
      // hazard that broke dependency pre-bundling ("does not provide an
      // export named 'n'", reproduced identically on esbuild AND Rolldown).
      // Force resolution to the CJS build qubicHelper.js actually expects.
      'bignumber.js': fileURLToPath(new URL('./node_modules/bignumber.js/bignumber.js', import.meta.url)),
    },
  },
  server: {
    proxy: {
      '/api/rpc': {
        target: process.env.VITE_BACKEND_RPC_URL || 'http://localhost:41841',
        changeOrigin: true,
        rewrite: (path) => path.replace(/^\/api\/rpc/, ''),
      },
    },
  },
})
