"use strict";
// ForestGuard dashboard — dependency-free. Polls the local REST API and renders
// node status, alerts, an event log, and per-metric history charts (inline SVG).

const REFRESH_MS = 5000;
const TOKEN_KEY = "fg_admin_token";

const METRIC_META = {
  temp_c:    { label: "Temp",     unit: "°C", digits: 1 },
  humidity:  { label: "Humidity", unit: "%",  digits: 0 },
  smoke:     { label: "Smoke",    unit: "",   digits: 0 },
  gas:       { label: "Gas",      unit: "",   digits: 0 },
  flame:     { label: "Flame",    unit: "",   digits: 0, bool: true },
  battery_v: { label: "Battery",  unit: "V",  digits: 2 },
  rssi:      { label: "RSSI",     unit: "dBm", digits: 0 },
};
const CARD_ORDER = ["temp_c", "humidity", "smoke", "gas", "flame", "battery_v"];

let timer = null;
let lastOk = 0;

// ---- token / auth -----------------------------------------------------
function getToken() { return localStorage.getItem(TOKEN_KEY) || ""; }
function setToken(t) {
  if (t) localStorage.setItem(TOKEN_KEY, t);
  else localStorage.removeItem(TOKEN_KEY);
}
function authHeaders() {
  const t = getToken();
  return t ? { Authorization: "Bearer " + t } : {};
}

async function api(path) {
  const res = await fetch(path, { headers: authHeaders(), cache: "no-store" });
  if (res.status === 401) {
    promptToken(true);
    throw new Error("unauthorized");
  }
  if (!res.ok) throw new Error("HTTP " + res.status);
  return res.json();
}

function promptToken(force) {
  const cur = getToken();
  const t = window.prompt(
    "Admin token (leave blank if the dashboard is public):", cur);
  if (t !== null) { setToken(t.trim()); refresh(); }
  else if (force && !cur) { /* user cancelled with no token */ }
}

// ---- formatting -------------------------------------------------------
function fmtMetric(key, v) {
  const m = METRIC_META[key];
  if (m && m.bool) return v >= 1 ? "YES" : "no";
  if (m) return `${Number(v).toFixed(m.digits)}${m.unit ? " " + m.unit : ""}`;
  return String(v);
}
function labelFor(key) { return (METRIC_META[key] || {}).label || key; }

function ago(ts) {
  if (!ts) return "never";
  const s = Math.max(0, Date.now() / 1000 - ts);
  if (s < 60) return Math.floor(s) + "s ago";
  if (s < 3600) return Math.floor(s / 60) + "m ago";
  if (s < 86400) return Math.floor(s / 3600) + "h ago";
  return Math.floor(s / 86400) + "d ago";
}
function el(tag, cls, html) {
  const e = document.createElement(tag);
  if (cls) e.className = cls;
  if (html !== undefined) e.innerHTML = html;
  return e;
}
function esc(s) {
  return String(s).replace(/[&<>"]/g, c =>
    ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" }[c]));
}

// ---- render -----------------------------------------------------------
function render(summary, nodes) {
  const worst = summary.worst_level || "ok";
  const banner = document.getElementById("banner");
  banner.className = "banner level-" + worst;
  const alertN = (summary.active_alerts || []).length;
  document.getElementById("banner-text").textContent =
    worst === "ok" && alertN === 0
      ? `All clear — ${summary.online}/${summary.node_count} nodes online`
      : `${worst.toUpperCase()} — ${alertN} active alert${alertN === 1 ? "" : "s"}, `
        + `${summary.online}/${summary.node_count} nodes online`;

  // tiles
  const c = summary.counts || {};
  const tiles = [
    ["Nodes", summary.node_count, ""],
    ["Online", summary.online, "ok"],
    ["Critical", c.critical || 0, "critical"],
    ["Warning", c.warning || 0, "warning"],
    ["Elevated", c.elevated || 0, "elevated"],
    ["Offline", summary.offline, "offline"],
  ];
  const twrap = document.getElementById("tiles");
  twrap.innerHTML = "";
  for (const [k, n, lvl] of tiles) {
    const t = el("div", "tile");
    const num = el("div", "n", esc(n));
    if (lvl) num.style.color = `var(--${lvl})`;
    t.appendChild(num);
    t.appendChild(el("div", "k", esc(k)));
    twrap.appendChild(t);
  }

  // cards
  const wrap = document.getElementById("nodes");
  wrap.innerHTML = "";
  const sorted = [...nodes].sort((a, b) => rank(b.level) - rank(a.level));
  for (const n of sorted) wrap.appendChild(nodeCard(n));
  document.getElementById("node-count").textContent = `(${nodes.length})`;

  // alerts
  const aWrap = document.getElementById("alerts");
  aWrap.innerHTML = "";
  const alerts = summary.active_alerts || [];
  if (!alerts.length) aWrap.appendChild(el("p", "muted", "None."));
  for (const a of alerts) {
    const d = el("div", "alert");
    d.style.setProperty("--c", `var(--${a.level})`);
    d.innerHTML = `<span class="badge level-${a.level}">${a.level}</span> ` +
      `<span class="name">${esc(a.name)}</span><br>` +
      `<span class="muted">${esc((a.reasons || []).join(", "))}</span>`;
    d.onclick = () => openNode(a.id);
    aWrap.appendChild(d);
  }

  // events
  const evWrap = document.getElementById("events");
  evWrap.innerHTML = "";
  for (const e of summary.recent_events || []) {
    const li = el("li");
    li.innerHTML =
      `<span class="dot" style="--c:var(--${e.level})"></span>${esc(e.message)}` +
      `<div class="when">${esc(new Date(e.ts * 1000).toLocaleString())}</div>`;
    evWrap.appendChild(li);
  }

  // cloud + footer
  const cl = summary.cloud || {};
  document.getElementById("cloud-status").textContent = cl.enabled
    ? `☁ cloud redundancy: ${cl.pending} queued, ${cl.sent} synced` +
      (cl.last_error ? ` (last error: ${cl.last_error})` : "")
    : "☁ cloud sync off (local-only)";
  document.getElementById("mode").textContent = cl.mode || "";
  document.getElementById("updated").textContent =
    "updated " + new Date().toLocaleTimeString();
}

function rank(level) {
  return { ok: 0, elevated: 1, warning: 2, offline: 2, critical: 3 }[level] ?? 0;
}

function nodeCard(n) {
  const card = el("div", "card level-" + n.level);
  const top = el("div", "top");
  top.appendChild(el("span", "name", esc(n.name)));
  top.appendChild(el("span", "badge level-" + n.level, esc(n.level)));
  card.appendChild(top);

  const mwrap = el("div", "metrics");
  const keys = CARD_ORDER.filter(k => k in n.metrics)
    .concat(Object.keys(n.metrics).filter(k => !CARD_ORDER.includes(k)));
  for (const k of keys.slice(0, 6)) {
    const m = el("div", "m");
    m.appendChild(el("span", "mk", esc(labelFor(k))));
    m.appendChild(el("span", "mv", esc(fmtMetric(k, n.metrics[k]))));
    mwrap.appendChild(m);
  }
  card.appendChild(mwrap);

  if (n.reasons && n.reasons.length)
    card.appendChild(el("div", "reasons", esc(n.reasons.join(" · "))));
  card.appendChild(el("div", "foot",
    `score ${n.score} · ${n.online ? "seen " + ago(n.last_seen) : "OFFLINE " + ago(n.last_seen)}`));
  card.onclick = () => openNode(n.id);
  return card;
}

// ---- modal + chart ----------------------------------------------------
let modalNode = null;

async function openNode(id) {
  modalNode = id;
  const modal = document.getElementById("modal");
  modal.classList.remove("hidden");
  document.getElementById("modal-title").textContent = id;
  try {
    const node = await api(`/api/v1/nodes/${encodeURIComponent(id)}`);
    document.getElementById("modal-title").textContent =
      `${node.name} — ${node.level.toUpperCase()}`;
    const sel = document.getElementById("metric-select");
    sel.innerHTML = "";
    const metrics = node.metrics_available || Object.keys(node.metrics || {});
    for (const m of metrics) sel.appendChild(new Option(labelFor(m), m));
    if (metrics.includes("temp_c")) sel.value = "temp_c";
    document.getElementById("modal-meta").innerHTML =
      `<b>Reasons:</b> ${esc((node.reasons || []).join(", ") || "nominal")}` +
      (node.lat != null ? `<br><b>Location:</b> ${node.lat}, ${node.lon}` : "");
    loadChart();
  } catch (e) { /* handled in api() */ }
}

async function loadChart() {
  if (!modalNode) return;
  const metric = document.getElementById("metric-select").value;
  const win = document.getElementById("window-select").value;
  if (!metric) return;
  try {
    const data = await api(
      `/api/v1/nodes/${encodeURIComponent(modalNode)}/history` +
      `?metric=${encodeURIComponent(metric)}&since=${win}`);
    drawChart(document.getElementById("chart"), data.points || [], metric);
  } catch (e) { /* handled */ }
}

function drawChart(container, points, metric) {
  const W = 720, H = 240, pad = { l: 46, r: 12, t: 14, b: 24 };
  if (!points.length) {
    container.innerHTML =
      `<svg viewBox="0 0 ${W} ${H}"><text x="${W/2}" y="${H/2}" fill="#8ba79a" ` +
      `text-anchor="middle">no data in window</text></svg>`;
    return;
  }
  const xs = points.map(p => p.ts), ys = points.map(p => p.value);
  let minY = Math.min(...ys), maxY = Math.max(...ys);
  if (minY === maxY) { minY -= 1; maxY += 1; }
  const minX = Math.min(...xs), maxX = Math.max(...xs);
  const sx = t => pad.l + (maxX === minX ? 0 : (t - minX) / (maxX - minX)) * (W - pad.l - pad.r);
  const sy = v => pad.t + (1 - (v - minY) / (maxY - minY)) * (H - pad.t - pad.b);

  const line = points.map((p, i) => (i ? "L" : "M") + sx(p.ts).toFixed(1) + " " + sy(p.value).toFixed(1)).join(" ");
  const area = `M${sx(points[0].ts).toFixed(1)} ${(H - pad.b)} ` +
    points.map(p => "L" + sx(p.ts).toFixed(1) + " " + sy(p.value).toFixed(1)).join(" ") +
    ` L${sx(points[points.length-1].ts).toFixed(1)} ${(H - pad.b)} Z`;

  // gridlines + y labels (3 steps)
  let grid = "";
  for (let i = 0; i <= 3; i++) {
    const v = minY + (maxY - minY) * i / 3;
    const y = sy(v).toFixed(1);
    grid += `<line x1="${pad.l}" y1="${y}" x2="${W - pad.r}" y2="${y}" stroke="#26382f"/>`;
    grid += `<text x="${pad.l - 6}" y="${(+y + 4)}" fill="#8ba79a" font-size="11" text-anchor="end">${v.toFixed((METRIC_META[metric]||{}).digits ?? 1)}</text>`;
  }
  // x labels (start / end)
  const t0 = new Date(minX * 1000).toLocaleTimeString([], { hour: "2-digit", minute: "2-digit" });
  const t1 = new Date(maxX * 1000).toLocaleTimeString([], { hour: "2-digit", minute: "2-digit" });

  container.innerHTML =
    `<svg viewBox="0 0 ${W} ${H}" preserveAspectRatio="none">` +
    grid +
    `<path d="${area}" fill="rgba(63,178,127,.14)"/>` +
    `<path d="${line}" fill="none" stroke="#3fb27f" stroke-width="2"/>` +
    `<text x="${pad.l}" y="${H - 6}" fill="#8ba79a" font-size="11">${t0}</text>` +
    `<text x="${W - pad.r}" y="${H - 6}" fill="#8ba79a" font-size="11" text-anchor="end">${t1}</text>` +
    `</svg>`;
}

// ---- lifecycle --------------------------------------------------------
async function refresh() {
  const conn = document.getElementById("conn");
  try {
    const [summary, nodes] = await Promise.all([
      api("/api/v1/summary"),
      api("/api/v1/nodes"),
    ]);
    render(summary, nodes.nodes || []);
    conn.textContent = "● live";
    conn.className = "pill ok";
    lastOk = Date.now();
  } catch (e) {
    conn.textContent = e.message === "unauthorized" ? "🔒 auth" : "● offline";
    conn.className = "pill bad";
  }
}

function scheduleRefresh() {
  if (timer) clearInterval(timer);
  if (document.getElementById("autorefresh").checked)
    timer = setInterval(refresh, REFRESH_MS);
}

document.getElementById("token-btn").onclick = () => promptToken(false);
document.getElementById("modal-close").onclick = () =>
  document.getElementById("modal").classList.add("hidden");
document.getElementById("modal").onclick = (e) => {
  if (e.target.id === "modal") e.target.classList.add("hidden");
};
document.getElementById("metric-select").onchange = loadChart;
document.getElementById("window-select").onchange = loadChart;
document.getElementById("autorefresh").onchange = scheduleRefresh;

refresh();
scheduleRefresh();
