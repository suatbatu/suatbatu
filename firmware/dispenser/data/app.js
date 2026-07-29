"use strict";
const $ = (s) => document.querySelector(s);
const DOW = ["Pz", "Pt", "Sa", "Ça", "Pe", "Cu", "Ct"]; // bit0=Sun .. bit6=Sat

async function api(path, opts = {}) {
  const res = await fetch(path, { credentials: "same-origin", ...opts });
  return res;
}

// ── Auth / routing ────────────────────────────────────────────────────────────
async function boot() {
  const res = await api("/api/status");
  if (res.status === 401) return showLogin();
  showDash(await res.json());
}
function showLogin() {
  $("#login").classList.remove("hidden");
  $("#dash").classList.add("hidden");
}
async function showDash(status) {
  $("#login").classList.add("hidden");
  $("#dash").classList.remove("hidden");
  renderStatus(status);
  await loadSchedule();
  await loadEvents();
  startPolling();
}

$("#loginForm").addEventListener("submit", async (e) => {
  e.preventDefault();
  const f = new FormData(e.target);
  const body = new URLSearchParams({ user: f.get("user"), pass: f.get("pass") });
  const res = await api("/api/login", { method: "POST", body });
  if (res.ok) { e.target.reset(); boot(); }
  else if (res.status === 429) $("#loginErr").textContent = "Çok fazla deneme. Biraz bekleyin.";
  else $("#loginErr").textContent = "Kullanıcı adı veya parola hatalı.";
});

$("#logout").addEventListener("click", async () => {
  await api("/api/logout", { method: "POST" });
  location.reload();
});

// ── Status ────────────────────────────────────────────────────────────────────
let pollTimer = null;
function startPolling() {
  if (pollTimer) clearInterval(pollTimer);
  pollTimer = setInterval(async () => {
    const res = await api("/api/status");
    if (res.status === 401) return location.reload();
    if (res.ok) renderStatus(await res.json());
  }, 5000);
}
function renderStatus(s) {
  const map = { idle:"Beklemede", dispensing:"Veriliyor", waiting:"Onay bekleniyor",
                fault:"ARIZA", boot:"Başlatılıyor" };
  $("#st-state").textContent = map[s.state] || s.state;
  $("#st-slot").textContent  = `${s.slot} / ${s.slots}`;
  $("#st-next").textContent  = s.nextLabel ? `${s.nextLabel} · ${s.nextTime}` : "—";
  $("#st-time").textContent  = s.time || "—";
  $("#devId").textContent    = s.deviceId || "";
  if (s.battMv != null) {
    $("#st-batt-row").style.display = "";
    $("#st-batt").textContent = `%${s.battPct} · ${s.battMv} mV`;
  } else {
    $("#st-batt-row").style.display = "none";
  }
  togglePill("#p-wifi", s.wifi);
  togglePill("#p-mqtt", s.mqtt);
  togglePill("#p-rtc",  s.rtc);
  if (s.camUrl) {
    $("#cam").src = s.camUrl + "/snapshot?ts=" + Date.now();
    $("#camLink").href = s.camUrl;
  }
}
function togglePill(sel, on) {
  const el = $(sel);
  el.classList.toggle("on", !!on);
  el.classList.toggle("off", !on);
}

// ── Actions ───────────────────────────────────────────────────────────────────
$("#btnDispense").addEventListener("click", async (e) => {
  e.target.disabled = true;
  await api("/api/dispense", { method: "POST" });
  setTimeout(() => (e.target.disabled = false), 3000);
});
$("#btnHome").addEventListener("click", async () => {
  await api("/api/home", { method: "POST" });
});
async function jog(sign) {
  const n = Math.max(1, Math.min(2048, parseInt($("#jogSteps").value, 10) || 0));
  const body = new URLSearchParams({ steps: String(sign * n) });
  await api("/api/jog", { method: "POST", body });
}
$("#jogFwd").addEventListener("click", () => jog(1));
$("#jogBack").addEventListener("click", () => jog(-1));

// ── Schedule editor ───────────────────────────────────────────────────────────
async function loadSchedule() {
  const res = await api("/api/schedule");
  if (!res.ok) return;
  const doses = await res.json();
  const tb = $("#schedTable tbody");
  tb.innerHTML = "";
  doses.forEach(addRow);
}
function addRow(d = { label:"Doz", hour:8, minute:0, dowMask:0x7F, enabled:true }) {
  const tr = document.createElement("tr");

  const tdLabel = document.createElement("td");
  const inLabel = Object.assign(document.createElement("input"),
    { type:"text", value:d.label, maxLength:23 });
  tdLabel.appendChild(inLabel);

  const tdTime = document.createElement("td");
  const inTime = Object.assign(document.createElement("input"), { type:"time" });
  inTime.value = String(d.hour).padStart(2,"0") + ":" + String(d.minute).padStart(2,"0");
  tdTime.appendChild(inTime);

  const tdDow = document.createElement("td");
  const dowWrap = document.createElement("div"); dowWrap.className = "dow";
  let mask = d.dowMask;
  DOW.forEach((name, bit) => {
    const b = document.createElement("button");
    b.type = "button"; b.textContent = name;
    b.className = (mask & (1<<bit)) ? "" : "off";
    b.addEventListener("click", () => {
      mask ^= (1<<bit);
      b.className = (mask & (1<<bit)) ? "" : "off";
      tr.dataset.mask = mask;
    });
    dowWrap.appendChild(b);
  });
  tr.dataset.mask = mask;
  tdDow.appendChild(dowWrap);

  const tdEn = document.createElement("td");
  const inEn = Object.assign(document.createElement("input"), { type:"checkbox", checked:d.enabled });
  tdEn.appendChild(inEn);

  const tdDel = document.createElement("td");
  const del = document.createElement("button");
  del.type = "button"; del.className = "ghost"; del.textContent = "✕";
  del.addEventListener("click", () => tr.remove());
  tdDel.appendChild(del);

  tr._get = () => {
    const [h, m] = inTime.value.split(":").map(Number);
    return { label:inLabel.value || "Doz", hour:h||0, minute:m||0,
             dowMask:Number(tr.dataset.mask), enabled:inEn.checked };
  };
  tr.append(tdLabel, tdTime, tdDow, tdEn, tdDel);
  $("#schedTable tbody").appendChild(tr);
}
$("#addDose").addEventListener("click", () => addRow());
$("#saveSched").addEventListener("click", async () => {
  const rows = [...document.querySelectorAll("#schedTable tbody tr")].map((tr) => tr._get());
  const res = await api("/api/schedule", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(rows),
  });
  $("#schedMsg").textContent = res.ok ? "Kaydedildi ✓" : "Kaydedilemedi — saatleri kontrol edin.";
  setTimeout(() => ($("#schedMsg").textContent = ""), 4000);
});

// ── Events ────────────────────────────────────────────────────────────────────
async function loadEvents() {
  const res = await api("/api/events");
  if (!res.ok) return;
  const evs = (await res.json()).slice().reverse();
  const ul = $("#events");
  ul.innerHTML = "";
  const fmt = (ts) => new Date(ts * 1000).toLocaleString("tr-TR");
  evs.forEach((e) => {
    const li = document.createElement("li");
    li.innerHTML = `<time>${fmt(e.ts)}</time>
      <span class="badge ${e.state}">${e.state}</span>
      <span>${e.label || ""}</span>`;
    ul.appendChild(li);
  });
}

// ── Password ──────────────────────────────────────────────────────────────────
$("#pwForm").addEventListener("submit", async (e) => {
  e.preventDefault();
  const f = new FormData(e.target);
  const body = new URLSearchParams({ current: f.get("current"), new: f.get("new") });
  const res = await api("/api/password", { method: "POST", body });
  $("#pwMsg").textContent = res.ok ? "Parola güncellendi ✓"
    : (res.status === 401 ? "Mevcut parola hatalı." : "Yeni parola zayıf (min 8).");
  if (res.ok) e.target.reset();
});

boot();
