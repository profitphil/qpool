# QPoolK6 testnet explorer

A browser dapp for the QPoolK6 smart contract: browse real pools/holdings/proposals and submit real signed transactions against a live devnet, entirely from the browser. No backend code of your own to run besides this static site plus one small proxy function.

**Status:** fully verified live against the real devkit this session -- identity derivation, transaction signing (K12 + SchnorrQ via `@qubic-lib/qubic-ts-library`), payload encoding, and broadcast all confirmed correct by actually creating a pool, contributing to it, and reading the results back on-chain.

## How it fits together

```
Visitor's browser
  -> signs transactions locally (seed never leaves the tab)
  -> fetches/broadcasts via /api/rpc/* (same origin, no CORS problem)
       -> Netlify Function (netlify/functions/rpc-proxy.mts)
            -> forwards to BACKEND_RPC_URL (an env var you set)
                 -> your core-lite node, reachable via a Cloudflare Tunnel
```

core-lite's own HTTP API sends no CORS headers at all (confirmed directly), so the browser can never call it cross-origin no matter how the tunnel is set up. The Netlify Function exists specifically to be same-origin with the deployed site and make that server-to-server hop instead, which isn't subject to CORS.

## Running it yourself (dev)

```bash
npm install
VITE_BACKEND_RPC_URL=https://your-tunnel-url.trycloudflare.com npm run dev
```

Vite's own dev-server proxy forwards `/api/rpc/*` to that URL (see `vite.config.ts`) -- no Netlify Function needed locally.

## Exposing your local devkit to the internet

Your devkit (`core-lite` + friends) only listens on `localhost` right now. Netlify hosts the static frontend; it cannot run your Qubic node for you. You need *some* way for the deployed site to reach your node's RPC (port 41841 by default).

**Quick tunnel (free, no account, good for a first deploy):**

```bash
cloudflared tunnel --url http://localhost:41841
```

This prints a random `https://*.trycloudflare.com` URL. **It changes every time you restart `cloudflared`** -- quick tunnels aren't meant for a stable, permanent address. When it changes:

1. Netlify dashboard -> Site configuration -> Environment variables -> update `BACKEND_RPC_URL`.
2. Trigger a redeploy (or just redeploy the functions) so the new value takes effect.

Your site only works while **both** your machine is on *and* `cloudflared` *and* your devkit's docker compose stack are all running. Closing your laptop takes the live site down for visitors -- that's the tradeoff for not standing up a dedicated always-on server.

**A note on load:** a free quick tunnel rate-limits request bursts (confirmed live: 429s past a handful of simultaneous requests). This app already paces its own scans conservatively and retries on 429 with backoff, but if several visitors browse at once, things will feel slow. That's the tunnel, not the app -- a paid/named tunnel or a real server removes this ceiling if it matters to you.

## Deploying to Netlify

1. Push this directory to a git repo (or use `netlify deploy` directly from here with the Netlify CLI, already installed as a dev dependency: `npx netlify deploy --prod`).
2. If using git: connect the repo in Netlify's dashboard. Build command and publish directory are already set in `netlify.toml` (`npm run build`, `dist`), and the function directory is `netlify/functions`.
3. Set the **`BACKEND_RPC_URL`** environment variable (Site configuration -> Environment variables) to your current tunnel URL, e.g. `https://your-tunnel-url.trycloudflare.com`.
4. Deploy. Visit the site, open the **Stats** tab -- if it loads real numbers, the whole pipeline is working.

## Security model, read this before sharing the link

- **Seeds never leave the browser.** Every form signs locally using `@qubic-lib/qubic-ts-library`; only the already-signed transaction bytes and read-only queries go over the network. There is nothing server-side that could log or leak a pasted seed.
- **This is one shared devnet.** Every visitor sees and can affect the same pools/holdings/proposals -- there's no per-visitor isolation. Treat it as a sandbox, not a private testing ground.
- **Never paste a real seed.** Testnet only. The wallet panel says this too, but it's worth repeating here: a seed is a private key. Only use throwaway testnet identities.
- **The scope is intentionally narrow.** This covers CreatePool, Contribute, DepositHoldingAsset, RegisterDividendPayer, Propose/Vote/Tally/Execute, ClaimPayout, and ReleaseManagedShares -- the core pool/governance/dividend lifecycle. QX-side actions (issuing a token, granting management rights, placing raw QX orders) aren't covered here; a depositor needs to have already done those through some other tool (e.g. this devkit's own `tools-bin/qt.py`-based scripts) before `DepositHoldingAsset` will work for them.

## A real limitation worth understanding: no transaction receipts

Qubic has no concept of "query this transaction's result." Broadcasting successfully (`peersBroadcasted: 1`) only means the network *accepted and processed* the transaction at its target tick -- it does **not** mean the contract's own logic approved it. A `ClaimPayout` on a holding with nothing newly owed broadcasts and processes completely normally; it just doesn't pay out anything, because the contract's own check refused it internally with no visible error anywhere.

Every form in this app is worded around that: it says "submitted at tick N," never "succeeded," and tells you which number on screen to check afterward to see whether anything actually happened. This matches how the devkit's own Python test suite works too -- it never trusts a return code, only ever re-queries state and compares before/after.

## Extending it

The procedure/function ID constants, payload encoders, and response decoders (`src/qubic/config.ts`, `encode.ts`, `decode.ts`) mirror `tools-bin/qpoolk6_lib.py` byte-for-byte on purpose. If you add a new procedure to QPoolK6.h, add its encoder/decoder here the same way, then wire up a form the same way the existing ones are built (see any file in `src/components/*Form.tsx` for the pattern: a `useAction` hook, a call to `submitProcedureCall`, and an honest "submitted, go check X" result message).

Not yet covered, straightforward to add following the same pattern: OfferPosition/CancelOffer/BuyPosition (the seat market), RebidIpo/SettleIpo, EvictSquat, CancelQxBid/CancelQxAsk, SetPoolAdmins, BuyAndBurnQpay.
