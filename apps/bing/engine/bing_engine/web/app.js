/* Bing dashboard front-end. Talks to the engine's JSON/SSE API. */
"use strict";

const $ = (sel) => document.querySelector(sel);
const $$ = (sel) => Array.from(document.querySelectorAll(sel));

const ICONS = {
  router: "📶", phone: "📱", tablet: "📱", laptop: "💻", desktop: "🖥️",
  server: "🖧", printer: "🖨️", tv: "📺", cast: "📡", camera: "🎥",
  speaker: "🔊", media: "🎬", storage: "🗄️", iot: "💡", device: "📟",
};

const state = { devices: new Map(), net: null, scanning: false };

/* ---------- tab switching ---------- */
$("#tabs").addEventListener("click", (e) => {
  const btn = e.target.closest(".tab");
  if (!btn) return;
  $$(".tab").forEach((t) => t.classList.toggle("active", t === btn));
  const view = btn.dataset.view;
  $$(".view").forEach((v) => v.classList.toggle("active", v.id === `view-${view}`));
  if (view === "network") loadNetworkDetail();
});

/* ---------- network summary ---------- */
async function loadNet() {
  try {
    const net = await fetchJSON("/api/net");
    state.net = net;
    $("#sumSubnet").textContent = net.cidr || "—";
    $("#sumGateway").textContent = net.gateway || "—";
    $("#sumIp").textContent = net.primary_ipv4 || "—";
    $("#sumIsp").textContent = net.isp || "—";
  } catch (e) { /* ignore; dashboard still usable */ }
}

async function loadNetworkDetail() {
  const el = $("#netDetail");
  el.innerHTML = `<p class="muted">Loading…</p>`;
  try {
    const net = await fetchJSON("/api/net");
    const kv = (k, v) => `<div class="kv-row"><span class="k">${k}</span><span class="v">${esc(v ?? "—")}</span></div>`;
    let html = `<div class="kv-card"><h3>This device</h3>
      ${kv("Hostname", net.hostname)}
      ${kv("Local IP", net.primary_ipv4)}
      ${kv("Gateway", net.gateway)}
      ${kv("Netmask", net.netmask)}
      ${kv("Subnet", net.cidr)}
      ${kv("DNS servers", (net.dns_servers || []).join(", "))}
    </div>`;
    html += `<div class="kv-card"><h3>Internet</h3>
      ${kv("Public IP", net.public_ip)}
      ${kv("ISP", net.isp)}
      ${kv("Location", net.location)}
    </div>`;
    if (net.interfaces && net.interfaces.length) {
      html += `<div class="kv-card"><h3>Interfaces</h3>` +
        net.interfaces.map((i) =>
          kv(i.name, `${i.ipv4 || "—"}${i.mac ? " · " + i.mac : ""}`)).join("") +
        `</div>`;
    }
    el.innerHTML = html;
  } catch (e) {
    el.innerHTML = `<p class="bad">Failed to load: ${esc(e.message)}</p>`;
  }
}

/* ---------- scanning (SSE) ---------- */
$("#scanBtn").addEventListener("click", startScan);

function startScan() {
  if (state.scanning) return;
  state.scanning = true;
  state.devices.clear();
  renderDevices();
  const btn = $("#scanBtn");
  btn.classList.add("scanning");
  $("#devicesEmpty")?.remove();
  $("#scanProgress").hidden = false;
  $("#barFill").style.width = "6%";
  $("#scanStatus").textContent = "Discovering hosts…";

  const es = new EventSource("/api/scan/stream");
  let count = 0;

  es.addEventListener("start", () => { $("#scanStatus").textContent = "Sweeping subnet…"; });
  es.addEventListener("device", (ev) => {
    const dev = JSON.parse(ev.data);
    state.devices.set(dev.ip, dev);
    count++;
    $("#barFill").style.width = Math.min(90, 10 + count * 6) + "%";
    $("#scanStatus").textContent = `${count} device${count === 1 ? "" : "s"} found…`;
    renderDevices();
  });
  es.addEventListener("done", (ev) => {
    const { count: total } = JSON.parse(ev.data);
    finishScan();
    $("#scanStatus").textContent = `Done — ${total} device${total === 1 ? "" : "s"}.`;
    setTimeout(() => { $("#scanProgress").hidden = true; }, 1400);
    es.close();
  });
  es.addEventListener("error", () => {
    finishScan();
    if (state.devices.size === 0) {
      $("#scanStatus").textContent = "Scan finished (or connection closed).";
      setTimeout(() => { $("#scanProgress").hidden = true; }, 1400);
    }
    es.close();
  });
}

function finishScan() {
  state.scanning = false;
  $("#scanBtn").classList.remove("scanning");
  $("#barFill").style.width = "100%";
}

function renderDevices() {
  const list = $("#deviceList");
  const devices = Array.from(state.devices.values())
    .sort((a, b) => ipNum(a.ip) - ipNum(b.ip));
  $("#sumCount").textContent = devices.length;
  if (!devices.length) {
    if (!state.scanning) list.innerHTML = `<div class="empty" id="devicesEmpty">
      <div class="empty-icon">📡</div><p>Press <strong>Scan network</strong> to discover devices.</p></div>`;
    return;
  }
  list.innerHTML = devices.map(deviceRow).join("");
  $$(".device").forEach((el) => el.addEventListener("click", () =>
    openDrawer(el.dataset.ip)));
}

function deviceRow(d) {
  const icon = ICONS[d.icon] || ICONS.device;
  const name = d.hostname || d.vendor || d.device_type || "Unknown device";
  const sub = [d.vendor, d.mac].filter(Boolean).join(" · ") || "—";
  const badges = (d.is_gateway ? `<span class="badge gw">gateway</span>` : "") +
                 (d.is_self ? `<span class="badge self">this device</span>` : "");
  const ports = (d.open_ports || []).slice(0, 5).join(", ");
  return `<div class="device" data-ip="${esc(d.ip)}">
    <div class="dev-icon">${icon}</div>
    <div class="dev-main">
      <div class="dev-name">${esc(name)} ${badges}</div>
      <div class="dev-sub">${esc(d.device_type)} · ${esc(sub)}</div>
    </div>
    <div class="dev-right">
      <span class="dev-ip">${esc(d.ip)}</span>
      ${ports ? `<span class="ports-mini">:${esc(ports)}</span>` : ""}
    </div>
  </div>`;
}

/* ---------- device drawer (deep scan) ---------- */
async function openDrawer(ip) {
  const d = state.devices.get(ip);
  if (!d) return;
  const drawer = $("#drawer");
  drawer.hidden = false;
  const icon = ICONS[d.icon] || ICONS.device;
  $("#drawerContent").innerHTML = `
    <div class="dev-icon" style="width:56px;height:56px;font-size:28px;margin-bottom:10px">${icon}</div>
    <h2>${esc(d.hostname || d.vendor || d.device_type)}</h2>
    <div class="d-meta">${esc(d.ip)}${d.mac ? " · " + esc(d.mac) : ""}</div>
    <div class="kv-card">
      <div class="kv-row"><span class="k">Type</span><span class="v">${esc(d.device_type)}</span></div>
      <div class="kv-row"><span class="k">Vendor</span><span class="v">${esc(d.vendor || "—")}</span></div>
      <div class="kv-row"><span class="k">Hostname</span><span class="v">${esc(d.hostname || "—")}</span></div>
      <div class="kv-row"><span class="k">Latency</span><span class="v">${d.rtt_ms != null ? d.rtt_ms + " ms" : "—"}</span></div>
    </div>
    <div class="d-actions">
      <button id="btnDeep">🔍 Deep scan (ports + security)</button>
      ${d.mac ? `<button id="btnWake">⏰ Wake</button>` : ""}
    </div>
    <div id="deepResult"></div>`;

  $("#btnDeep").addEventListener("click", () => deepScan(ip));
  const wake = $("#btnWake");
  if (wake) wake.addEventListener("click", async () => {
    wake.textContent = "Sending…";
    try { await fetchJSON(`/api/wol?mac=${encodeURIComponent(d.mac)}`); wake.textContent = "✅ Magic packet sent"; }
    catch { wake.textContent = "❌ Failed"; }
  });
}

async function deepScan(ip) {
  const box = $("#deepResult");
  box.innerHTML = `<div class="d-section"><span class="spinner-inline"></span> Scanning ports…</div>`;
  try {
    const r = await fetchJSON(`/api/device?ip=${encodeURIComponent(ip)}&ports=top100`);
    const ports = r.open_ports || [];
    const sec = r.security || {};
    let html = `<div class="d-section"><h4>Open ports (${ports.length})</h4>`;
    html += ports.length
      ? ports.map((p) => `<span class="port-pill">${p.port}<span class="svc">${esc(p.service || "?")}</span></span>`).join("")
      : `<span class="muted">No open ports found.</span>`;
    html += `</div>`;
    html += `<div class="d-section"><h4>Security</h4>
      <div class="score-ring">
        <span class="score-num grade-${esc(sec.grade)}">${sec.score ?? "—"}</span>
        <div><strong class="grade-${esc(sec.grade)}">Grade ${esc(sec.grade || "?")}</strong><br>
        <span class="muted">${(sec.findings || []).length} issue(s) flagged</span></div>
      </div>`;
    for (const f of sec.findings || []) {
      html += `<div class="finding"><span class="sev ${esc(f.severity)}">${esc(f.severity)}</span>
        <span><strong>:${f.port}</strong> ${esc(f.message)}</span></div>`;
    }
    for (const alt of sec.encrypted_alternatives || []) {
      html += `<div class="finding"><span class="sev low">tip</span><span>${esc(alt)}</span></div>`;
    }
    html += `</div>`;
    box.innerHTML = html;
  } catch (e) {
    box.innerHTML = `<p class="bad">Deep scan failed: ${esc(e.message)}</p>`;
  }
}

$("#drawer").addEventListener("click", (e) => {
  if (e.target.dataset.close !== undefined) $("#drawer").hidden = true;
});
document.addEventListener("keydown", (e) => { if (e.key === "Escape") $("#drawer").hidden = true; });

/* ---------- tools ---------- */
$("#view-tools").addEventListener("click", async (e) => {
  const btn = e.target.closest("button[data-run]");
  if (!btn) return;
  const tool = btn.dataset.run;
  const out = $(`#out-${tool}`);
  btn.disabled = true;
  out.innerHTML = `<span class="spinner-inline"></span> working…`;
  try {
    await runTool(tool, out);
  } catch (err) {
    out.innerHTML = `<span class="bad">Error: ${esc(err.message)}</span>`;
  } finally {
    btn.disabled = false;
  }
});

async function runTool(tool, out) {
  if (tool === "ping") {
    const host = val("#pingHost"); if (!host) return warn(out, "Enter a host.");
    const r = await fetchJSON(`/api/ping?host=${enc(host)}&count=5`);
    if (!r.address) return warn(out, "Could not resolve host.");
    const s = r.summary;
    out.innerHTML =
      `${esc(host)} [${esc(r.address)}] via ${r.method}\n` +
      `${r.transmitted} sent, ${r.received} received, ` +
      `<span class="${r.loss_pct > 0 ? "warn" : "ok"}">${r.loss_pct}% loss</span>\n` +
      (s.avg != null ? `rtt min/avg/max = ${s.min}/${s.avg}/${s.max} ms · jitter ${s.jitter} ms` : "no replies");
  } else if (tool === "ports") {
    const host = val("#portsHost"); if (!host) return warn(out, "Enter a host.");
    const spec = val("#portsSpec") || "common";
    const r = await fetchJSON(`/api/ports?host=${enc(host)}&ports=${enc(spec)}`);
    const open = r.open || [];
    let t = open.length ? open.map((p) =>
      `<span class="ok">OPEN</span> ${String(p.port).padStart(5)}/tcp  ${(p.service || "?").padEnd(14)}` +
      (p.banner ? `<span class="dim">${esc(p.banner)}</span>` : "")).join("\n")
      : "No open ports found.";
    const sec = r.security || {};
    t += `\n\nSecurity: <span class="grade-${esc(sec.grade)}">${sec.score}/100 (${sec.grade})</span>`;
    for (const f of sec.findings || []) t += `\n<span class="${f.severity === "high" ? "bad" : "warn"}">!</span> :${f.port} ${esc(f.message)}`;
    out.innerHTML = t;
  } else if (tool === "dns") {
    const name = val("#dnsName"); if (!name) return warn(out, "Enter a name.");
    const type = val("#dnsType") || "A";
    const r = await fetchJSON(`/api/dns?name=${enc(name)}&type=${enc(type)}`);
    const recs = r.records || [];
    let t = recs.length ? recs.map((x) => `${x.type.padEnd(6)} ${String(x.ttl).padStart(6)}  ${esc(x.value)}`).join("\n") : "No records.";
    if (r.errors && r.errors.length) t += `\n<span class="dim">${r.errors.map(esc).join("\n")}</span>`;
    out.innerHTML = t;
  } else if (tool === "trace") {
    const host = val("#traceHost"); if (!host) return warn(out, "Enter a host.");
    const r = await fetchJSON(`/api/trace?host=${enc(host)}`);
    if (!r.address) return warn(out, r.note || "Could not resolve host.");
    let t = (r.hops || []).map((h) =>
      `${String(h.ttl).padStart(2)}. ${(h.address || "*").padEnd(16)} ${h.hostname ? "(" + esc(h.hostname) + ") " : ""}${h.rtt_ms != null ? h.rtt_ms + " ms" : "*"}${h.final ? " ✔" : ""}`).join("\n");
    if (r.note) t += `\n<span class="dim">${esc(r.note)}</span>`;
    out.innerHTML = t;
  } else if (tool === "speed") {
    out.innerHTML = `<span class="spinner-inline"></span> testing (this takes a few seconds)…`;
    const quick = $("#speedQuick").checked ? "1" : "0";
    const r = await fetchJSON(`/api/speed?quick=${quick}`, 40000);
    if (r.error) return warn(out, r.error);
    out.innerHTML =
      `Server:   ${esc(r.server || "—")}\nISP:      ${esc(r.isp || "—")}\n` +
      `Latency:  <span class="ok">${r.latency_ms} ms</span> (jitter ${r.jitter_ms} ms)\n` +
      `Download: <span class="ok">${r.download_mbps ?? "—"} Mbps</span>\n` +
      `Upload:   <span class="ok">${r.upload_mbps ?? "—"} Mbps</span>`;
  } else if (tool === "wol") {
    const mac = val("#wolMac"); if (!mac) return warn(out, "Enter a MAC address.");
    const r = await fetchJSON(`/api/wol?mac=${enc(mac)}`);
    out.innerHTML = r.error ? `<span class="bad">${esc(r.error)}</span>` : `<span class="ok">Magic packet sent to ${esc(r.mac)}.</span>`;
  } else if (tool === "vendor") {
    const mac = val("#vendorMac"); if (!mac) return warn(out, "Enter a MAC address.");
    const r = await fetchJSON(`/api/vendor?mac=${enc(mac)}`);
    out.innerHTML = r.mac
      ? `${esc(r.mac)} → ${r.vendor ? `<span class="ok">${esc(r.vendor)}</span>` : `<span class="warn">unknown</span>`}`
      : `<span class="bad">Invalid MAC address.</span>`;
  }
}

/* ---------- helpers ---------- */
async function fetchJSON(url, timeout = 20000) {
  const ctrl = new AbortController();
  const t = setTimeout(() => ctrl.abort(), timeout);
  try {
    const res = await fetch(url, { signal: ctrl.signal });
    const data = await res.json();
    if (!res.ok) throw new Error(data.error || res.statusText);
    return data;
  } finally { clearTimeout(t); }
}
const val = (sel) => $(sel).value.trim();
const enc = (s) => encodeURIComponent(s);
const warn = (out, msg) => { out.innerHTML = `<span class="warn">${esc(msg)}</span>`; };
const esc = (s) => String(s ?? "").replace(/[&<>"']/g, (c) =>
  ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[c]));
const ipNum = (ip) => ip.split(".").reduce((a, o) => (a << 8) + (+o || 0), 0) >>> 0;

/* ---------- boot ---------- */
fetch("/api/net?public=0").then(() => {}).catch(() => {});
$("#footVer").textContent = "";
loadNet();
