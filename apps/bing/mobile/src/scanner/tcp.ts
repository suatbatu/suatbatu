/** TCP reachability probing for on-device scanning.
 *
 *  Preferred path: `react-native-tcp-socket` — real connect() probes, so we can
 *  tell "open", "refused" (host alive, port closed) and "timeout" apart, exactly
 *  like the desktop engine. This needs a custom dev/release build (the native
 *  module isn't in Expo Go).
 *
 *  Fallback path: `fetch()` with an AbortController timeout. Pure JS, so it runs
 *  even in Expo Go, but it can only find HTTP(S)-ish ports and can't distinguish
 *  a refusal from a timeout. `hasNativeTcp()` lets the UI tell the user which
 *  mode is active. */

export type ProbeResult = { open: boolean; alive: boolean; rttMs: number | null };

let _tcp: any | undefined;
let _tcpResolved = false;

function getTcp(): any | null {
  if (_tcpResolved) return _tcp ?? null;
  _tcpResolved = true;
  try {
    // Lazy require so the app still loads when the native module is absent.
    _tcp = require("react-native-tcp-socket").default;
  } catch {
    _tcp = undefined;
  }
  return _tcp ?? null;
}

export function hasNativeTcp(): boolean {
  return getTcp() !== null;
}

/** Probe a single TCP port. Resolves — never rejects. */
export function probePort(host: string, port: number, timeout = 700): Promise<ProbeResult> {
  const tcp = getTcp();
  if (tcp) return probeWithSocket(tcp, host, port, timeout);
  return probeWithFetch(host, port, timeout);
}

function probeWithSocket(tcp: any, host: string, port: number, timeout: number): Promise<ProbeResult> {
  return new Promise((resolve) => {
    const start = Date.now();
    let done = false;
    const finish = (r: ProbeResult) => {
      if (done) return;
      done = true;
      try { client.destroy(); } catch {}
      resolve(r);
    };
    const client = tcp.createConnection({ host, port, timeout }, () => {
      finish({ open: true, alive: true, rttMs: Date.now() - start });
    });
    client.on("error", (err: any) => {
      const code = String(err?.message || err?.code || "");
      // A refused connection still proves the host is up.
      const refused = /ECONNREFUSED|refused|reset|ECONNRESET/i.test(code);
      finish({ open: false, alive: refused, rttMs: refused ? Date.now() - start : null });
    });
    client.on("timeout", () => finish({ open: false, alive: false, rttMs: null }));
    client.on("close", () => finish({ open: false, alive: false, rttMs: null }));
  });
}

async function probeWithFetch(host: string, port: number, timeout: number): Promise<ProbeResult> {
  const start = Date.now();
  const scheme = port === 443 || port === 8443 ? "https" : "http";
  const ctrl = new AbortController();
  const timer = setTimeout(() => ctrl.abort(), timeout);
  try {
    // We don't care about the HTTP response — only whether the socket answered.
    await fetch(`${scheme}://${host}:${port}/`, { method: "HEAD", signal: ctrl.signal });
    return { open: true, alive: true, rttMs: Date.now() - start };
  } catch (e: any) {
    // Abort => timed out (assume down). Any other error settled fast => the host
    // answered at the transport layer (reset/refused/TLS), so it's alive.
    if (e?.name === "AbortError") return { open: false, alive: false, rttMs: null };
    return { open: false, alive: true, rttMs: Date.now() - start };
  } finally {
    clearTimeout(timer);
  }
}

/** Run probes with a bounded concurrency pool. Returns results in input order. */
export async function probeMany<T>(
  items: T[],
  worker: (item: T) => Promise<void>,
  concurrency = 24,
): Promise<void> {
  let idx = 0;
  const runNext = async (): Promise<void> => {
    const i = idx++;
    if (i >= items.length) return;
    await worker(items[i]);
    return runNext();
  };
  const pool = Array.from({ length: Math.min(concurrency, items.length) }, runNext);
  await Promise.all(pool);
}
