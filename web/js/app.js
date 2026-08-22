/* =====================================================================
 * NEXUS CSI Sensor - dashboard controller
 *
 * Talks to the firmware's REST API and the /live WebSocket. Uses Chart.js
 * when it loaded from the CDN, otherwise falls back to a tiny built-in canvas
 * renderer so charts still work in offline / soft-AP mode. All mutating calls
 * carry the per-session CSRF token obtained from /api/session.
 * ===================================================================== */
(function () {
  "use strict";

  /* ---------------- small helpers ---------------- */
  const $ = (id) => document.getElementById(id);
  const fmt = (v, d = 0) => (v == null || isNaN(v) ? "-" : Number(v).toFixed(d));
  const HISTORY_CAP = 120;

  function fmtBytes(b) {
    if (b == null) return "-";
    if (b < 1024) return b + " B";
    if (b < 1048576) return (b / 1024).toFixed(1) + " KB";
    return (b / 1048576).toFixed(2) + " MB";
  }
  function fmtDur(s) {
    s = Math.max(0, s | 0);
    if (s < 60) return s + "s";
    const m = (s / 60) | 0;
    if (m < 60) return m + "m " + (s % 60) + "s";
    const h = (m / 60) | 0;
    return h + "h " + (m % 60) + "m";
  }

  function toast(msg, kind) {
    const t = document.createElement("div");
    t.className = "toast " + (kind || "");
    t.textContent = msg;
    $("toasts").appendChild(t);
    setTimeout(() => {
      t.style.transition = "opacity .3s";
      t.style.opacity = "0";
      setTimeout(() => t.remove(), 300);
    }, 3400);
  }

  let csrf = "";

  async function apiGet(url) {
    const r = await fetch(url, { credentials: "same-origin" });
    if (r.status === 401) { location.replace("/login"); throw new Error("unauth"); }
    if (!r.ok) throw new Error("HTTP " + r.status);
    return r.json();
  }
  async function apiPost(url, body) {
    const r = await fetch(url, {
      method: "POST",
      credentials: "same-origin",
      headers: { "Content-Type": "application/json", "X-CSRF-Token": csrf },
      body: body ? JSON.stringify(body) : "{}",
    });
    if (r.status === 401) { location.replace("/login"); throw new Error("unauth"); }
    const j = await r.json().catch(() => ({}));
    if (!r.ok || j.ok === false) throw new Error(j.message || "HTTP " + r.status);
    return j;
  }

  /* ---------------- charts (Chart.js or fallback) ---------------- */
  function makeTrend(canvasId, color, max) {
    const canvas = $(canvasId);

    if (window.Chart) {
      const chart = new window.Chart(canvas.getContext("2d"), {
        type: "line",
        data: { labels: [], datasets: [{
          data: [], borderColor: color, backgroundColor: color + "22",
          fill: true, tension: 0.35, pointRadius: 0, borderWidth: 2,
        }] },
        options: {
          responsive: true, maintainAspectRatio: false, animation: false,
          scales: {
            x: { display: false },
            y: { beginAtZero: true, suggestedMax: max,
                 grid: { color: "rgba(148,178,255,0.08)" },
                 ticks: { color: "#5f6f8a", maxTicksLimit: 5 } },
          },
          plugins: { legend: { display: false }, tooltip: { enabled: false } },
        },
      });
      return {
        set(arr) {
          chart.data.labels = arr.map((_, i) => i);
          chart.data.datasets[0].data = arr.slice(-HISTORY_CAP);
          chart.update("none");
        },
        push(v) {
          const d = chart.data.datasets[0].data;
          d.push(v);
          while (d.length > HISTORY_CAP) d.shift();
          chart.data.labels = d.map((_, i) => i);
          chart.update("none");
        },
      };
    }

    /* ---- built-in fallback renderer ---- */
    const data = [];
    function draw() {
      const dpr = window.devicePixelRatio || 1;
      const w = canvas.clientWidth || 300;
      const h = canvas.clientHeight || 180;
      canvas.width = w * dpr; canvas.height = h * dpr;
      const ctx = canvas.getContext("2d");
      ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
      ctx.clearRect(0, 0, w, h);
      ctx.strokeStyle = "rgba(148,178,255,0.08)"; ctx.lineWidth = 1;
      for (let i = 0; i <= 4; i++) {
        const y = (h * i) / 4 + 0.5;
        ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(w, y); ctx.stroke();
      }
      if (data.length < 2) return;
      const peak = Math.max(max, ...data) || 1;
      const stepX = w / Math.max(1, data.length - 1);
      const yOf = (v) => h - (v / peak) * h * 0.95;
      ctx.beginPath();
      data.forEach((v, i) => { const x = i * stepX, y = yOf(v); i ? ctx.lineTo(x, y) : ctx.moveTo(x, y); });
      ctx.lineTo((data.length - 1) * stepX, h); ctx.lineTo(0, h); ctx.closePath();
      const g = ctx.createLinearGradient(0, 0, 0, h);
      g.addColorStop(0, color + "55"); g.addColorStop(1, color + "00");
      ctx.fillStyle = g; ctx.fill();
      ctx.beginPath();
      data.forEach((v, i) => { const x = i * stepX, y = yOf(v); i ? ctx.lineTo(x, y) : ctx.moveTo(x, y); });
      ctx.strokeStyle = color; ctx.lineWidth = 2; ctx.stroke();
    }
    window.addEventListener("resize", draw);
    return {
      set(arr) { data.length = 0; arr.slice(-HISTORY_CAP).forEach((v) => data.push(v)); draw(); },
      push(v) { data.push(v); while (data.length > HISTORY_CAP) data.shift(); draw(); },
    };
  }

  let motionTrend, varTrend;

  /* ---------------- status rendering ---------------- */
  const STATE_CLASS = ["s-idle", "s-presence", "s-motion", "s-high"];
  const STATE_GLYPH = ["○", "◍", "◉", "✸"];
  let lastChartPush = 0;

  function setConn(ok, s) {
    const dot = $("connDot"), txt = $("connText");
    if (ok) {
      const live = s && s.csi && s.csi.active;
      dot.className = "dot " + (live ? "live" : "");
      txt.textContent = live ? "CSI live" : "connected";
    } else {
      dot.className = "dot down";
      txt.textContent = "reconnecting…";
    }
  }

  function renderStatus(s) {
    setConn(true, s);
    const st = s.motion.state | 0;
    const cls = STATE_CLASS[st] || "s-idle";

    const ring = $("stateRing");
    ring.className = "ring " + cls;
    ring.textContent = STATE_GLYPH[st] || "○";
    const stext = $("stateText");
    stext.className = "big " + cls;
    stext.textContent = (s.motion.state_str || "idle").toUpperCase();
    $("stateSince").textContent = fmtDur(s.motion.state_since_s);

    const pres = $("mPresence");
    pres.textContent = s.motion.presence ? "YES" : "no";
    pres.className = "v " + (s.motion.presence ? "s-presence" : "");
    $("mIntensity").innerHTML = fmt(s.motion.intensity, 0) + "<small>%</small>";
    $("mActivity").innerHTML = fmt(s.motion.activity, 0) + "<small>%</small>";
    $("mSignal").innerHTML = fmt(s.motion.signal_quality, 0) + "<small>%</small>";

    $("cScore").textContent = fmt(s.motion.score, 2);
    const bar = $("cScoreBar");
    bar.style.width = Math.min(100, s.motion.score * 100) + "%";
    bar.style.background = st >= 3
      ? "linear-gradient(90deg,#fb7185,#f43f5e)"
      : st === 2 ? "linear-gradient(90deg,#fbbf24,#f59e0b)"
      : "linear-gradient(90deg,#38bdf8,#22d3ee)";

    $("cPps").innerHTML = fmt(s.csi.pps, 1) + "<small> /s</small>";
    $("cPackets").textContent = s.csi.packets_total + " total";
    $("cRssi").innerHTML = s.csi.rssi + "<small> dBm</small>";
    const csiState = $("cCsiState");
    csiState.textContent = s.csi.active ? "CSI live" : "no CSI";
    csiState.style.color = s.csi.active ? "var(--ok)" : "var(--alert)";
    $("cVariance").textContent = fmt(s.motion.variance, 3);
    $("cBaseline").textContent = fmt(s.motion.baseline, 3);

    $("iDevice").textContent = s.device;
    $("iIp").textContent = s.wifi.ip || "-";
    $("iWifi").textContent =
      s.wifi.role === "STA" ? s.wifi.ssid + " (" + s.wifi.rssi + " dBm)"
      : s.wifi.role === "AP" ? "Setup access point" : "-";
    $("iChannel").textContent = s.wifi.channel || "-";
    $("iAmp").textContent = fmt(s.csi.amp_mean, 1) + " ± " + fmt(s.csi.amp_std, 1);
    $("iHeap").textContent = fmtBytes(s.heap_free);
    $("iPart").textContent = s.partition;

    $("deviceSub").textContent = s.device + " · " + (s.wifi.ip || s.wifi.role);
    $("uptimeTop").textContent = s.uptime;
    $("footDevice").textContent = s.device;
    $("footFw").textContent = s.fw;

    $("sFw").textContent = s.fw;
    $("sPart").textContent = s.partition;
    $("sUptime").textContent = s.uptime;
    $("sHeap").textContent = fmtBytes(s.heap_free);
    $("sHeapMin").textContent = fmtBytes(s.heap_min);
    $("sSessions").textContent = s.sessions;

    /* Feed charts at ~1 Hz to match the server-side history cadence. */
    const now = Date.now();
    if (now - lastChartPush > 900) {
      lastChartPush = now;
      if (motionTrend) motionTrend.push(s.motion.score);
      if (varTrend) varTrend.push(s.motion.variance);
    }
  }

  /* ---------------- live connection (WS + poll fallback) ---------------- */
  let ws = null, wsRetry = null;

  function openWs() {
    const proto = location.protocol === "https:" ? "wss" : "ws";
    try {
      ws = new WebSocket(proto + "://" + location.host + "/live");
    } catch (_) { scheduleReconnect(); return; }

    ws.onmessage = (ev) => {
      try { renderStatus(JSON.parse(ev.data)); } catch (_) {}
    };
    ws.onclose = () => { setConn(false); scheduleReconnect(); };
    ws.onerror = () => { try { ws.close(); } catch (_) {} };
  }
  function scheduleReconnect() {
    if (wsRetry) return;
    wsRetry = setTimeout(() => { wsRetry = null; openWs(); }, 2500);
  }

  /* Safety-net poll: if the socket is not open, pull status over REST. */
  async function pollTick() {
    if (ws && ws.readyState === WebSocket.OPEN) return;
    try { renderStatus(await apiGet("/api/status")); } catch (_) {}
  }

  /* ---------------- navigation ---------------- */
  const TITLES = { dashboard: "Dashboard", logs: "Event Log", settings: "Settings", system: "System & OTA" };
  function switchView(name) {
    document.querySelectorAll(".view").forEach((v) => v.classList.remove("active"));
    const view = $("view-" + name);
    if (view) view.classList.add("active");
    document.querySelectorAll("#nav button").forEach((b) =>
      b.classList.toggle("active", b.dataset.view === name));
    $("pageTitle").textContent = TITLES[name] || "";
    closeSidebar();
    if (name === "logs") loadLogs();
    if (name === "settings") loadConfig();
  }

  /* ---------------- logs ---------------- */
  async function loadLogs() {
    const rows = $("logRows");
    try {
      const j = await apiGet("/api/logs");
      if (!j.logs || !j.logs.length) {
        rows.innerHTML = '<tr><td colspan="4" style="color:var(--text-faint);">No events logged yet.</td></tr>';
        return;
      }
      rows.innerHTML = j.logs.map((e) =>
        "<tr><td class='mono'>" + e.seq + "</td><td class='mono'>" + fmtDur((e.t / 1000) | 0) +
        "</td><td><span class='lvl " + e.level + "'>" + e.level + "</span></td><td>" +
        escapeHtml(e.msg) + "</td></tr>").join("");
    } catch (_) {
      rows.innerHTML = '<tr><td colspan="4" style="color:var(--alert);">Failed to load log.</td></tr>';
    }
  }
  function escapeHtml(s) {
    return String(s).replace(/[&<>"']/g, (c) =>
      ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[c]));
  }

  /* ---------------- settings ---------------- */
  let cfgLoaded = null;
  async function loadConfig() {
    try {
      const c = await apiGet("/api/config");
      cfgLoaded = c;
      $("fDeviceName").value = c.device_name || "";
      $("fSsid").value = c.wifi_ssid || "";
      $("fPass").value = "";
      $("fPassHint").textContent = c.wifi_pass_set
        ? "A password is currently stored (leave blank to keep)." : "No password stored.";
      setRange("fPresence", "lPresence", c.presence_threshold, 2);
      setRange("fMotion", "lMotion", c.motion_threshold, 2);
      setRange("fHigh", "lHigh", c.high_motion_threshold, 2);
      $("fRate").value = c.sampling_rate_hz;
      $("fAuto").checked = !!c.auto_calibration;
      setRange("fBright", "lBright", c.display_brightness, 0, "%");
      $("fAdminUser").value = c.admin_user || "";
      $("fAdminPass").value = "";
    } catch (_) { toast("Could not load configuration", "err"); }
  }
  function setRange(inputId, labelId, val, dec, suffix) {
    const el = $(inputId);
    el.value = val;
    $(labelId).textContent = Number(val).toFixed(dec) + (suffix || "");
  }
  function bindRange(inputId, labelId, dec, suffix) {
    const el = $(inputId);
    el.addEventListener("input", () =>
      ($(labelId).textContent = Number(el.value).toFixed(dec) + (suffix || "")));
  }

  async function saveSettings() {
    const body = {
      device_name: $("fDeviceName").value.trim(),
      wifi_ssid: $("fSsid").value.trim(),
      presence_threshold: parseFloat($("fPresence").value),
      motion_threshold: parseFloat($("fMotion").value),
      high_motion_threshold: parseFloat($("fHigh").value),
      sampling_rate_hz: parseInt($("fRate").value, 10),
      auto_calibration: $("fAuto").checked,
      display_brightness: parseInt($("fBright").value, 10),
      admin_user: $("fAdminUser").value.trim(),
    };
    const pw = $("fPass").value;
    if (pw) body.wifi_pass = pw;
    const adminPw = $("fAdminPass").value;
    if (adminPw) {
      if (adminPw.length < 6) { toast("Admin password too short (min 6).", "err"); return; }
      body.admin_pass = adminPw;
    }
    const btn = $("saveBtn");
    btn.disabled = true;
    try {
      const j = await apiPost("/api/config", body);
      toast(j.message || "Settings saved", "ok");
      loadConfig();
    } catch (e) { toast(e.message || "Save failed", "err"); }
    btn.disabled = false;
  }

  /* ---------------- OTA + reboot ---------------- */
  function waitForReboot() {
    let tries = 0;
    const iv = setInterval(async () => {
      tries++;
      try {
        const r = await fetch("/api/session", { credentials: "same-origin", cache: "no-store" });
        if (r.status === 200 || r.status === 401) { clearInterval(iv); location.replace("/"); }
      } catch (_) {}
      if (tries > 30) { clearInterval(iv); location.replace("/"); }
    }, 2000);
  }

  function doOta() {
    const f = $("fwFile").files[0];
    if (!f) { toast("Choose a .bin file first", "err"); return; }
    const xhr = new XMLHttpRequest();
    xhr.open("POST", "/api/ota");
    xhr.setRequestHeader("X-CSRF-Token", csrf);
    xhr.upload.onprogress = (e) => {
      if (e.lengthComputable) {
        const p = Math.round((e.loaded / e.total) * 100);
        $("otaBar").style.width = p + "%";
        $("otaMsg").textContent = "Uploading " + p + "% (" + fmtBytes(e.loaded) + ")";
      }
    };
    xhr.onload = () => {
      let j = {};
      try { j = JSON.parse(xhr.responseText); } catch (_) {}
      if (xhr.status === 200 && j.ok) {
        $("otaBar").style.width = "100%";
        $("otaMsg").textContent = "Verified - device rebooting into new firmware…";
        toast("Firmware flashed. Rebooting…", "ok");
        waitForReboot();
      } else {
        $("otaMsg").textContent = j.message || "Update failed (" + xhr.status + ")";
        toast(j.message || "Update failed", "err");
        $("otaBtn").disabled = false;
      }
    };
    xhr.onerror = () => {
      $("otaMsg").textContent = "Upload error";
      toast("Upload error", "err");
      $("otaBtn").disabled = false;
    };
    $("otaBtn").disabled = true;
    $("otaMsg").textContent = "Starting upload…";
    xhr.send(f);
  }

  /* ---------------- sidebar (mobile) ---------------- */
  function openSidebar() {
    $("sidebar").classList.add("open");
    const bd = document.createElement("div");
    bd.className = "backdrop"; bd.id = "backdrop";
    bd.onclick = closeSidebar;
    document.body.appendChild(bd);
  }
  function closeSidebar() {
    $("sidebar").classList.remove("open");
    const bd = $("backdrop"); if (bd) bd.remove();
  }

  /* ---------------- wire up ---------------- */
  function bindEvents() {
    document.querySelectorAll("#nav button").forEach((b) =>
      b.addEventListener("click", () => switchView(b.dataset.view)));
    $("hamburger").addEventListener("click", () =>
      $("sidebar").classList.contains("open") ? closeSidebar() : openSidebar());

    bindRange("fPresence", "lPresence", 2);
    bindRange("fMotion", "lMotion", 2);
    bindRange("fHigh", "lHigh", 2);
    bindRange("fBright", "lBright", 0, "%");

    $("saveBtn").addEventListener("click", saveSettings);
    $("calibrateBtn").addEventListener("click", async () => {
      try { await apiPost("/api/calibrate"); toast("Recalibration started", "ok"); }
      catch (e) { toast(e.message, "err"); }
    });
    $("factoryBtn").addEventListener("click", async () => {
      if (!confirm("Reset ALL settings to factory defaults and reboot?")) return;
      try { await apiPost("/api/factory-reset"); toast("Factory reset - rebooting…", "ok"); waitForReboot(); }
      catch (e) { toast(e.message, "err"); }
    });
    $("rebootBtn").addEventListener("click", async () => {
      if (!confirm("Reboot the device now?")) return;
      try { await apiPost("/api/reboot"); toast("Rebooting…", "ok"); waitForReboot(); }
      catch (e) { toast(e.message, "err"); }
    });
    $("logRefresh").addEventListener("click", loadLogs);
    $("logDownload").addEventListener("click", () => { window.location = "/api/logs.csv"; });
    $("logoutBtn").addEventListener("click", async () => {
      try { await apiPost("/api/logout"); } catch (_) {}
      location.replace("/login");
    });
    $("fwFile").addEventListener("change", () => {
      $("otaBtn").disabled = !$("fwFile").files.length;
    });
    $("otaBtn").addEventListener("click", doOta);
  }

  /* ---------------- boot ---------------- */
  async function init() {
    try {
      const sess = await apiGet("/api/session"); /* redirects to /login on 401 */
      csrf = sess.csrf || "";
    } catch (_) { return; }

    bindEvents();
    motionTrend = makeTrend("chartMotion", "#38bdf8", 1);
    varTrend = makeTrend("chartVar", "#22d3ee", 0.02);

    /* Seed charts from persisted history. */
    try {
      const h = await apiGet("/api/history");
      if (h.count > 0) {
        motionTrend.set(h.score || []);
        varTrend.set(h.variance || []);
      }
    } catch (_) {}

    openWs();
    pollTick();
    setInterval(pollTick, 3000);
  }

  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", init);
  } else {
    init();
  }
})();
