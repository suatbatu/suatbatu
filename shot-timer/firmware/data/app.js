/* shot-timer web UI.
 *
 * The device is the source of truth for every number. The only thing the
 * browser computes on its own is the sweep of the running clock between
 * device ticks, and even that is re-anchored every time a tick arrives.
 */
'use strict';

const $ = (sel) => document.querySelector(sel);
const $$ = (sel) => Array.from(document.querySelectorAll(sel));

const fmt = (ms) => (ms / 1000).toFixed(2);
const blankString = () =>
  ({ count: 0, shots: [], firstMs: 0, totalMs: 0, bestSplitMs: 0, worstSplitMs: 0 });

const state = {
  ws: null,
  appState: 'ready',
  string: blankString(),
  clockAnchor: null,   // { perf: DOMHighResTimeStamp, elapsedMs: number }
  frozenMs: 0,
  pollTimer: null,
  settings: null,
  editingProfile: 0,
  drills: null,
  editingDrill: 0,
};

/* ----------------------------------------------------------------- tabs -- */
$$('.tab').forEach((tab) => {
  tab.addEventListener('click', () => {
    $$('.tab').forEach((t) => t.classList.toggle('is-active', t === tab));
    $$('.panel').forEach((p) => p.classList.toggle('is-active', p.id === tab.dataset.panel));
    if (tab.dataset.panel === 'history') loadHistory();
    if (tab.dataset.panel === 'drills') loadDrills();
    setMeterPolling(tab.dataset.panel === 'settings');
  });
});

/* -------------------------------------------------------------- socket -- */
async function connect() {
  try {
    const res = await fetch('/api/wstoken', { credentials: 'same-origin' });
    if (!res.ok) throw new Error('auth');
    const { token } = await res.json();

    const proto = location.protocol === 'https:' ? 'wss' : 'ws';
    const ws = new WebSocket(`${proto}://${location.host}/ws`);
    state.ws = ws;

    // The socket authenticates by sending the token as its first frame —
    // browsers cannot attach an Authorization header to a WebSocket handshake.
    ws.onopen = () => ws.send(token);
    ws.onmessage = (ev) => handleEvent(JSON.parse(ev.data));
    ws.onclose = () => {
      setConn(false);
      setTimeout(connect, 1500);
    };
    ws.onerror = () => ws.close();
  } catch (err) {
    setConn(false);
    setTimeout(connect, 3000);
  }
}

function setConn(ok) {
  const el = $('#conn');
  el.textContent = ok ? 'live' : 'offline';
  el.className = `pill ${ok ? 'pill--ok' : 'pill--bad'}`;
}

function handleEvent(msg) {
  setConn(true);
  switch (msg.type) {
    case 'state':
      applyStatus(msg);
      break;

    case 'beep':
      state.clockAnchor = { perf: performance.now(), elapsedMs: 0 };
      state.string = blankString();
      renderShots();
      break;

    case 'shot':
      state.string.shots[msg.index] = msg.atMs;
      state.string.count = state.string.shots.length;
      recomputeSummary();
      renderShots();
      break;

    case 'tick':
      state.clockAnchor = { perf: performance.now(), elapsedMs: msg.elapsedMs };
      break;

    case 'par':
      flashPar();
      break;

    case 'end':
      state.appState = 'review';
      if (msg.string) state.string = normaliseString(msg.string);
      state.clockAnchor = null;
      state.frozenMs = state.string.totalMs || 0;
      renderShots();
      renderState();
      if (msg.saveFailed) $('#settings-status').textContent = 'String was not saved (storage full?).';
      break;
  }
}

function normaliseString(s) {
  return {
    count: s.count || 0,
    shots: s.shots || [],
    firstMs: s.firstMs || 0,
    totalMs: s.totalMs || 0,
    bestSplitMs: s.bestSplitMs || 0,
    worstSplitMs: s.worstSplitMs || 0,
  };
}

function applyStatus(st) {
  state.appState = st.state || 'ready';
  if (st.string) state.string = normaliseString(st.string);
  if (state.appState === 'running') {
    state.clockAnchor = { perf: performance.now(), elapsedMs: st.elapsedMs || 0 };
  } else {
    state.clockAnchor = null;
    state.frozenMs = st.elapsedMs || 0;
  }
  if (st.detector) renderDetector(st.detector);
  renderBattery(st.battery);
  renderState();
  renderShots();
}

/* ------------------------------------------------------------ rendering -- */
function recomputeSummary() {
  const shots = state.string.shots;
  state.string.firstMs = shots.length ? shots[0] : 0;
  state.string.totalMs = shots.length ? shots[shots.length - 1] : 0;
  // Shot 1 is a draw, not a split — it never competes for best/worst.
  let best = 0;
  let worst = 0;
  for (let i = 1; i < shots.length; i++) {
    const split = shots[i] - shots[i - 1];
    if (!best || split < best) best = split;
    if (split > worst) worst = split;
  }
  state.string.bestSplitMs = best;
  state.string.worstSplitMs = worst;
}

function renderState() {
  const el = $('#state');
  el.textContent = state.appState;
  el.dataset.state = state.appState;
  $('#countdown-note').hidden = state.appState !== 'countdown';
  const busy = state.appState === 'countdown' || state.appState === 'running';
  $('#btn-start').disabled = busy;
  $('#btn-stop').disabled = !busy;
}

function renderShots() {
  const { shots, count, firstMs, totalMs, bestSplitMs, worstSplitMs } = state.string;
  $('#sum-count').textContent = count;
  $('#sum-first').textContent = firstMs ? fmt(firstMs) : '–';
  $('#sum-best').textContent = bestSplitMs ? fmt(bestSplitMs) : '–';

  const rows = shots.map((t, i) => {
    const split = i === 0 ? t : t - shots[i - 1];
    let cls = '';
    if (i > 0 && shots.length > 2) {
      if (split === bestSplitMs) cls = 'is-best';
      else if (split === worstSplitMs) cls = 'is-worst';
    }
    const splitLabel = i === 0 ? `${fmt(split)} draw` : fmt(split);
    return `<tr><td>${i + 1}</td><td>${fmt(t)}</td><td class="${cls}">${splitLabel}</td></tr>`;
  });
  $('#shots').innerHTML = rows.join('');

  if (!state.clockAnchor) $('#clock').textContent = fmt(totalMs || state.frozenMs || 0);
}

function renderDetector(d) {
  $('#profile-badge').textContent = d.profile || '–';

  const rejected = (d.rejectedEcho || 0) + (d.rejectedOffAxis || 0);
  const note = $('#reject-note');
  if (rejected > 0) {
    note.hidden = false;
    note.textContent =
      `Rejected since boot: ${d.rejectedEcho || 0} as echo, ` +
      `${d.rejectedOffAxis || 0} as off-axis.`;
  } else {
    note.hidden = true;
  }

  const gateLive = d.directionGate && d.secondMic;
  const rows = [
    ['Active profile', d.profile || '–'],
    ['Direction gate', d.directionGate ? (gateLive ? 'on' : 'on, but only one mic detected') : 'off'],
    ['Second microphone', d.secondMic ? 'detected' : 'not detected'],
    ['Accepted shots', d.accepted || 0],
    ['Rejected as echo', d.rejectedEcho || 0],
    ['Rejected as off-axis', d.rejectedOffAxis || 0],
    ['Last arrival lag', `${d.lastLagSamples || 0} samples`],
    ['Last angle', `${d.lastAngleDeg || 0}°`],
    ['Last correlation', `${d.lastConfidence || 0}%`],
  ];
  $('#diag').innerHTML = rows
    .map(([k, v]) => `<dt>${k}</dt><dd>${v}</dd>`)
    .join('');

  const gateNote = $('#gate-note');
  if (!d.directionGate) {
    gateNote.textContent =
      'Off. Every sound loud enough to clear the threshold counts as a shot.';
  } else if (!d.secondMic) {
    gateNote.textContent =
      'On, but no second microphone is detected — the gate fails open, so nothing is being rejected.';
  } else {
    const maxLag = state.settings ? state.settings.maxLagSamples : '?';
    gateNote.textContent = `On. Accepting arrivals within ±${maxLag} samples of centre.`;
  }
}

// Absent `battery` means no cell on the divider — a board on USB, or one not
// built yet. Showing "0%" there would read as a warning rather than a
// non-measurement, so the badge is hidden instead.
function renderBle(b) {
  const el = $('#ble-badge');
  if (!b || !b.active) { el.hidden = true; return; }
  el.hidden = false;
  el.textContent = b.connected ? 'BT linked' : 'BT';
  el.className = `pill ${b.connected ? 'pill--ok' : ''}`;
}

function renderBattery(b) {
  const el = $('#battery-badge');
  if (!b) { el.hidden = true; return; }
  el.hidden = false;
  el.textContent = `${b.percent}% · ${b.volts.toFixed(2)}V`;
  el.className = `pill ${b.critical ? 'pill--bad' : (b.low ? 'pill--warn' : '')}`;
}

function flashPar() {
  const el = $('#state');
  el.style.color = 'var(--accent)';
  setTimeout(() => { el.style.color = ''; }, 200);
}

function sweep() {
  if (state.clockAnchor) {
    const ms = state.clockAnchor.elapsedMs + (performance.now() - state.clockAnchor.perf);
    $('#clock').textContent = fmt(ms);
  }
  requestAnimationFrame(sweep);
}

/* ------------------------------------------------------------- control -- */
$('#btn-start').addEventListener('click', () => post('/api/start'));
$('#btn-stop').addEventListener('click', () => post('/api/stop'));

async function post(url, body) {
  const res = await fetch(url, {
    method: 'POST',
    credentials: 'same-origin',
    headers: body ? { 'Content-Type': 'application/json' } : {},
    body: body ? JSON.stringify(body) : undefined,
  });
  return res.ok ? res.json() : null;
}

/* ------------------------------------------------------------- history -- */
async function loadHistory() {
  const res = await fetch('/api/strings', { credentials: 'same-origin' });
  if (!res.ok) return;
  const { strings } = await res.json();
  // The device streams the log in file order (oldest first) because that is
  // the order it can stream without buffering. Newest-first is a view choice,
  // so it happens here.
  strings.reverse();

  $('#history-empty').hidden = strings.length > 0;
  $('#history-stats').textContent = strings.length
    ? `${strings.length} strings stored.` : '';

  $('#strings').innerHTML = strings.map((s) => `
    <li data-id="${s.id}">
      <div class="hd"><span>#${s.id}</span><span>${s.count} shots &middot; ${fmt(s.totalMs)}s</span></div>
      <div class="sub">first ${fmt(s.firstMs)}s${s.bestSplitMs ? ` &middot; best split ${fmt(s.bestSplitMs)}s` : ''}</div>
      <div class="detail" hidden></div>
    </li>`).join('');

  $$('#strings li').forEach((li) => {
    li.addEventListener('click', () => expandString(li));
  });
}

async function expandString(li) {
  const detail = li.querySelector('.detail');
  if (!detail.hidden) { detail.hidden = true; return; }

  const res = await fetch(`/api/string?id=${li.dataset.id}`, { credentials: 'same-origin' });
  if (!res.ok) return;
  const s = await res.json();
  const shots = s.shots || [];
  detail.innerHTML = `<table class="shots"><tbody>${shots.map((t, i) => {
    const split = i === 0 ? t : t - shots[i - 1];
    return `<tr><td>${i + 1}</td><td>${fmt(t)}</td><td>${i === 0 ? `${fmt(split)} draw` : fmt(split)}</td></tr>`;
  }).join('')}</tbody></table>`;
  detail.hidden = false;
}

$('#btn-clear').addEventListener('click', async () => {
  if (!confirm('Delete every stored string? This cannot be undone.')) return;
  await fetch('/api/strings', { method: 'DELETE', credentials: 'same-origin' });
  loadHistory();
});

/* -------------------------------------------------------------- drills -- */
const drillForm = $('#drill-form');

async function loadDrills() {
  const res = await fetch('/api/drills', { credentials: 'same-origin' });
  if (!res.ok) return;
  const d = await res.json();
  state.drills = d;
  if (state.editingDrill == null) state.editingDrill = d.activeDrill;

  const sel = $('#drill-select');
  sel.innerHTML = d.drills
    .map((x, i) => `<option value="${i}">${i + 1}. ${x.name}</option>`).join('');
  sel.value = String(state.editingDrill);
  showDrill(state.editingDrill);
  await loadTrend();
}

function showDrill(index) {
  const d = state.drills?.drills?.[index];
  if (!d) return;
  state.editingDrill = index;
  drillForm.elements.name.value = d.name;
  drillForm.elements.expectedShots.value = d.expectedShots;
  (d.parMs || []).forEach((v, i) => {
    const el = drillForm.elements[`par${i}`];
    if (el) el.value = v;
  });
  const active = state.drills.activeDrill === index;
  $('#drill-active-note').textContent = active ? 'active' : `active: ${state.drills.drills[state.drills.activeDrill].name}`;
  $('#drill-active-note').className = `pill ${active ? 'pill--ok' : ''}`;
}

$('#drill-select').addEventListener('change', async (e) => {
  showDrill(Number(e.target.value));
  await loadTrend();
});

$('#btn-drill-activate').addEventListener('click', async () => {
  const out = await post('/api/drills', { activeDrill: state.editingDrill });
  if (out) { state.drills = out; showDrill(state.editingDrill); }
});

drillForm.addEventListener('submit', async (e) => {
  e.preventDefault();
  const num = (n) => Number(drillForm.elements[n].value);
  const out = await post('/api/drills', {
    drill: {
      index: state.editingDrill,
      name: drillForm.elements.name.value,
      expectedShots: num('expectedShots'),
      parMs: [num('par0'), num('par1'), num('par2'), num('par3')],
    },
  });
  $('#drill-status').textContent = out ? 'Saved.' : 'Save failed.';
  if (out) {
    state.drills = out;
    const sel = $('#drill-select');
    sel.innerHTML = out.drills
      .map((x, i) => `<option value="${i}">${i + 1}. ${x.name}</option>`).join('');
    sel.value = String(state.editingDrill);
    showDrill(state.editingDrill);
  }
});

/* --------------------------------------------------------------- trend -- */
// Only the last MAX_TREND runs are plotted: a training trend is about the
// recent past, and 200 points in 300 px of phone screen is a smear.
const MAX_TREND = 20;

async function loadTrend() {
  const res = await fetch('/api/strings', { credentials: 'same-origin' });
  if (!res.ok) return;
  const { strings } = await res.json();
  const name = state.drills?.drills?.[state.editingDrill]?.name;

  // Matched on the stored name rather than the index, so renaming slot 3 does
  // not silently graft its history onto a different drill.
  const runs = strings
    .filter((s) => s.drill && s.drill === name && s.count > 0)
    .slice(-MAX_TREND);

  $('#trend-empty').hidden = runs.length > 0;
  $('#trend').hidden = runs.length === 0;
  if (!runs.length) return;

  const parMs = (state.drills.drills[state.editingDrill].parMs || [])[0] || 0;
  drawTrend(runs, parMs);

  $('#trend-runs').textContent = runs.length;
  $('#trend-best').textContent = fmt(Math.min(...runs.map((r) => r.totalMs)));
  $('#trend-first').textContent = fmt(Math.min(...runs.map((r) => r.firstMs)));
  $('#trend-table tbody').innerHTML = runs
    .map((r, i) => `<tr><td>${i + 1}</td><td>${fmt(r.firstMs)}</td><td>${fmt(r.totalMs)}</td><td>${r.count}</td></tr>`)
    .reverse().join('');
}

// Two series on ONE shared axis — both are seconds, so there is no excuse for
// a second scale. Colours are categorical slots 1 and 2, validated against
// this UI's surface; the par line is a reference, not a series, so it wears
// the accent token and carries its own text label.
const SERIES = [
  { key: 'firstMs', label: 'First shot', color: '#3987e5' },
  { key: 'totalMs', label: 'Total', color: '#d95926' },
];

function drawTrend(runs, parMs) {
  const W = 340, H = 150;
  const padL = 30, padR = 46, padT = 10, padB = 24;
  const x0 = padL, x1 = W - padR, y0 = padT, y1 = H - padB;

  const values = runs.flatMap((r) => SERIES.map((s) => r[s.key] / 1000));
  if (parMs > 0) values.push(parMs / 1000);
  // A non-zero baseline is deliberate: the whole point of a training trend is
  // tenths of a second, which a zero-based axis would flatten into a line.
  // Both axis ticks are always labelled so the range is never implied.
  let lo = Math.min(...values), hi = Math.max(...values);
  const span = Math.max(hi - lo, 0.2);
  lo = Math.max(0, lo - span * 0.15);
  hi = hi + span * 0.15;

  const sx = (i) => runs.length === 1
    ? (x0 + x1) / 2
    : x0 + (i * (x1 - x0)) / (runs.length - 1);
  const sy = (v) => y1 - ((v - lo) / (hi - lo)) * (y1 - y0);

  const parts = [];

  // Gridlines: hairline, solid, recessive — never dashed.
  const ticks = [lo, (lo + hi) / 2, hi];
  ticks.forEach((v) => {
    const y = sy(v).toFixed(1);
    parts.push(`<line x1="${x0}" y1="${y}" x2="${x1}" y2="${y}" class="g-grid"/>`);
    parts.push(`<text x="${x0 - 6}" y="${y}" class="g-tick" text-anchor="end" dominant-baseline="middle">${v.toFixed(2)}</text>`);
  });

  if (parMs > 0) {
    const y = sy(parMs / 1000).toFixed(1);
    parts.push(`<line x1="${x0}" y1="${y}" x2="${x1}" y2="${y}" class="g-par"/>`);
    parts.push(`<text x="${x1 + 4}" y="${y}" class="g-parlabel" dominant-baseline="middle">par</text>`);
  }

  SERIES.forEach((s) => {
    const pts = runs.map((r, i) => [sx(i), sy(r[s.key] / 1000)]);
    if (pts.length > 1) {
      const d = pts.map(([x, y], i) => `${i ? 'L' : 'M'}${x.toFixed(1)} ${y.toFixed(1)}`).join(' ');
      parts.push(`<path d="${d}" fill="none" stroke="${s.color}" stroke-width="2" stroke-linejoin="round" stroke-linecap="round"/>`);
    }
    // End marker with a 2px surface ring so the two series stay legible where
    // they cross, plus a single direct label at the end — never one per point.
    const [ex, ey] = pts[pts.length - 1];
    parts.push(`<circle cx="${ex.toFixed(1)}" cy="${ey.toFixed(1)}" r="4" fill="${s.color}" stroke="var(--surface)" stroke-width="2"/>`);
    parts.push(`<text x="${(ex + 8).toFixed(1)}" y="${ey.toFixed(1)}" class="g-endlabel" dominant-baseline="middle">${(runs[runs.length - 1][s.key] / 1000).toFixed(2)}</text>`);
  });

  parts.push(`<text x="${x0}" y="${H - 6}" class="g-tick">run 1</text>`);
  if (runs.length > 1) {
    parts.push(`<text x="${x1}" y="${H - 6}" class="g-tick" text-anchor="end">run ${runs.length}</text>`);
  }

  // Hover layer: a full-height hit band per run, wider than the marks.
  parts.push(`<line id="trend-cross" class="g-cross" y1="${y0}" y2="${y1}" x1="0" x2="0" visibility="hidden"/>`);
  runs.forEach((r, i) => {
    const bandW = runs.length === 1 ? x1 - x0 : (x1 - x0) / (runs.length - 1);
    parts.push(`<rect class="g-hit" x="${(sx(i) - bandW / 2).toFixed(1)}" y="${y0}" width="${bandW.toFixed(1)}" height="${y1 - y0}" data-i="${i}"/>`);
  });

  $('#trend-chart').innerHTML =
    `<svg viewBox="0 0 ${W} ${H}" role="img" aria-label="Run times for this drill">${parts.join('')}</svg>` +
    `<div class="tip" id="trend-tip" hidden></div>`;

  // Legend is always present for two series — identity is never colour alone.
  $('#trend-legend').innerHTML = SERIES
    .map((s) => `<span class="legend__item"><span class="legend__key" style="background:${s.color}"></span>${s.label}</span>`)
    .join('');

  const tip = $('#trend-tip');
  const cross = $('#trend-cross');
  $$('#trend-chart .g-hit').forEach((hit) => {
    hit.addEventListener('pointerenter', () => {
      const i = Number(hit.dataset.i);
      const r = runs[i];
      cross.setAttribute('x1', sx(i)); cross.setAttribute('x2', sx(i));
      cross.setAttribute('visibility', 'visible');
      tip.hidden = false;
      tip.style.left = `${(sx(i) / W) * 100}%`;
      tip.innerHTML = `<b>run ${i + 1}</b> · #${r.id}<br>first ${fmt(r.firstMs)} · total ${fmt(r.totalMs)}<br>${r.count} shots`;
    });
  });
  $('#trend-chart').addEventListener('pointerleave', () => {
    tip.hidden = true;
    cross.setAttribute('visibility', 'hidden');
  });
}

/* ------------------------------------------------------------ settings -- */
const form = $('#settings-form');

const PROFILE_FIELDS = ['sensitivity', 'refractoryMs', 'echoRejectDb', 'echoDecayMs',
                        'directionGate', 'maxOffAxisDeg'];

async function loadSettings() {
  const res = await fetch('/api/settings', { credentials: 'same-origin' });
  if (!res.ok) return;
  const s = await res.json();
  state.settings = s;
  state.editingProfile = s.activeProfile;

  const sel = $('#profile-select');
  sel.innerHTML = (s.profiles || [])
    .map((p, i) => `<option value="${i}">${i + 1}. ${p.name}</option>`).join('');
  sel.value = String(s.activeProfile);

  for (const [k, v] of Object.entries(s)) {
    const el = form.elements[k];
    if (!el || Array.isArray(v) || typeof v === 'object') continue;
    if (el.type === 'checkbox') el.checked = !!v;
    else el.value = v;
  }
  (s.parMs || []).forEach((v, i) => {
    const el = form.elements[`par${i}`];
    if (el) el.value = v;
  });

  showProfile(s.activeProfile);
  $('#fw').textContent = `firmware v${s.version}`;
}

function showProfile(index) {
  const p = state.settings?.profiles?.[index];
  if (!p) return;
  state.editingProfile = index;
  form.elements.profileName.value = p.name;
  PROFILE_FIELDS.forEach((f) => {
    const el = form.elements[f];
    if (!el) return;
    if (el.type === 'checkbox') el.checked = !!p[f];
    else el.value = p[f];
  });
  form.elements['sensitivity-out'].value = p.sensitivity;
}

$('#profile-select').addEventListener('change', (e) => showProfile(Number(e.target.value)));

form.elements.sensitivity.addEventListener('input', (e) => {
  form.elements['sensitivity-out'].value = e.target.value;
});

form.addEventListener('submit', async (e) => {
  e.preventDefault();
  const num = (name) => Number(form.elements[name].value);
  const payload = {
    activeProfile: Number($('#profile-select').value),
    // Only the profile being edited is sent, so saving here cannot clobber a
    // different profile someone changed on the device menu meanwhile.
    profile: {
      index: state.editingProfile,
      name: form.elements.profileName.value,
      sensitivity: num('sensitivity'),
      refractoryMs: num('refractoryMs'),
      echoRejectDb: num('echoRejectDb'),
      echoDecayMs: num('echoDecayMs'),
      directionGate: form.elements.directionGate.checked,
      maxOffAxisDeg: num('maxOffAxisDeg'),
    },
    micSpacingMm: num('micSpacingMm'),
    micOffsetMs: num('micOffsetMs'),
    delayMode: num('delayMode'),
    delayMinMs: num('delayMinMs'),
    delayMaxMs: num('delayMaxMs'),
    delayFixedMs: num('delayFixedMs'),
    parEnabled: form.elements.parEnabled.checked,
    parMs: [num('par0'), num('par1'), num('par2'), num('par3')],
    beepFreqHz: num('beepFreqHz'),
    beepMs: num('beepMs'),
    beepVolume: num('beepVolume'),
    autoStopSec: num('autoStopSec'),
    autoSave: form.elements.autoSave.checked,
    bleEnabled: form.elements.bleEnabled.checked,
    bleName: form.elements.bleName.value,
    displayFlipped: form.elements.displayFlipped.checked,
    displayContrast: num('displayContrast'),
    autoDim: form.elements.autoDim.checked,
    webUser: form.elements.webUser.value,
    wifiSsid: form.elements.wifiSsid.value,
  };
  // Passwords are only sent when actually typed, so saving the form does not
  // wipe credentials the server never disclosed back to us.
  if (form.elements.wifiPass.value) payload.wifiPass = form.elements.wifiPass.value;
  if (form.elements.webPass.value) payload.webPass = form.elements.webPass.value;

  payload.parCount = Math.max(1, payload.parMs.filter((v) => v > 0).length);

  const out = await post('/api/settings', payload);
  const status = $('#settings-status');
  if (!out) {
    status.textContent = 'Save failed.';
  } else if (out.accepted === false) {
    status.textContent = 'Saved, but at least one value was rejected — a web password must be '
      + '8+ characters, and a Bluetooth name must start with "Commander" or "AMG Lab Comm".';
  } else {
    status.textContent = 'Saved.';
    form.elements.wifiPass.value = '';
    form.elements.webPass.value = '';
    state.settings = out;
    const sel = $('#profile-select');
    sel.innerHTML = (out.profiles || [])
      .map((p, i) => `<option value="${i}">${i + 1}. ${p.name}</option>`).join('');
    sel.value = String(state.editingProfile);
  }
});

/* --------------------------------------------------- meter + diagnostics -- */
function setMeterPolling(on) {
  if (state.pollTimer) { clearInterval(state.pollTimer); state.pollTimer = null; }
  if (!on) return;
  state.pollTimer = setInterval(async () => {
    const res = await fetch('/api/status', { credentials: 'same-origin' });
    if (!res.ok) return;
    const st = await res.json();
    $('#meter-bar').style.width = `${(st.levelPerMille || 0) / 10}%`;
    $('#meter-floor').style.left = `${(st.floorPerMille || 0) / 10}%`;
    if (st.detector) renderDetector(st.detector);
    renderBattery(st.battery);
    renderBle(st.ble);
  }, 250);
}

/* ------------------------------------------------------------ start up -- */
loadSettings();
connect();
requestAnimationFrame(sweep);
