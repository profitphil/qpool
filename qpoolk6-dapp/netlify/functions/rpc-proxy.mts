// Same-origin proxy to core-lite's RPC. core-lite sends no CORS headers at
// all (confirmed directly with curl), so the browser can never call it
// cross-origin no matter how the backend is hosted -- this function is the
// fix: same origin as the deployed site, forwards server-to-server (which
// is never subject to CORS) to whatever backend the site owner currently
// has up.
//
// BACKEND_RPC_URL is a Netlify environment variable, not a build-time
// constant -- update it in the Netlify dashboard (Site configuration ->
// Environment variables) whenever the tunnel URL changes (a Cloudflare
// quick tunnel's URL changes every time it's restarted), then trigger a
// redeploy. No code change needed.
import type { Config, Context } from '@netlify/functions'

export default async (req: Request, _context: Context) => {
  const backend = process.env.BACKEND_RPC_URL
  if (!backend) {
    return new Response(JSON.stringify({ error: 'BACKEND_RPC_URL is not set on this Netlify site.' }), {
      status: 500,
      headers: { 'Content-Type': 'application/json' },
    })
  }

  const url = new URL(req.url)
  const forwardPath = url.pathname.replace(/^\/api\/rpc/, '')
  const target = `${backend.replace(/\/$/, '')}${forwardPath}${url.search}`

  const init: RequestInit = {
    method: req.method,
    headers: { 'Content-Type': 'application/json' },
  }
  if (req.method !== 'GET' && req.method !== 'HEAD') {
    init.body = await req.text()
  }

  try {
    const upstream = await fetch(target, init)
    const body = await upstream.text()
    return new Response(body, {
      status: upstream.status,
      headers: { 'Content-Type': upstream.headers.get('Content-Type') ?? 'application/json' },
    })
  } catch (err) {
    return new Response(JSON.stringify({ error: `Could not reach backend: ${String(err)}` }), {
      status: 502,
      headers: { 'Content-Type': 'application/json' },
    })
  }
}

export const config: Config = {
  path: '/api/rpc/*',
}
