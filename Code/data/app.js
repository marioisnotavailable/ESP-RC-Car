async function apiGetSaved() {
  const r = await fetch('/api/saved', { cache: 'no-store' });
  return r.json();
}
async function apiSave(ssid, pass) {
  const body = new URLSearchParams({ ssid, pass });
  const r = await fetch('/api/save', { method: 'POST', headers: { 'Content-Type': 'application/x-www-form-urlencoded' }, body });
  return r.json();
}
async function apiDelete(idx) {
  const body = new URLSearchParams({ i: String(idx) });
  const r = await fetch('/api/delete', { method: 'POST', headers: { 'Content-Type': 'application/x-www-form-urlencoded' }, body });
  return r.json();
}
async function apiMove(from, to) {
  const body = new URLSearchParams({ from: String(from), to: String(to) });
  const r = await fetch('/api/move', { method: 'POST', headers: { 'Content-Type': 'application/x-www-form-urlencoded' }, body });
  return r.json();
}
async function apiScan() {
  const r = await fetch('/api/scan', { method: 'POST' });
  return r.json();
}
async function apiReboot() {
  await fetch('/api/reboot', { method: 'POST' });
}
async function apiGetAdc() {
  const r = await fetch('/api/adc', { cache: 'no-store' });
  return r.json();
}
async function apiGetSettings() {
  const r = await fetch('/api/settings', { cache: 'no-store' });
  return r.json();
}
async function apiSaveSettings(data) {
  const body = new URLSearchParams(data);
  const r = await fetch('/api/settings', { method: 'POST', headers: { 'Content-Type': 'application/x-www-form-urlencoded' }, body });
  return r.json();
}

function fmtLastConn(epoch) {
  if (!epoch || epoch === 0) return 'Never';
  const d = new Date(epoch * 1000);
  const pad = n => String(n).padStart(2, '0');
  return `${pad(d.getDate())}.${pad(d.getMonth()+1)}.${d.getFullYear()} ${pad(d.getHours())}:${pad(d.getMinutes())}`;
}

function renderSaved(list) {
  const tb = document.querySelector('#saved-table tbody');
  tb.innerHTML = '';
  list.forEach((it, idx) => {
    const tr = document.createElement('tr');
    const upBtn = idx > 0 ? `<button data-up="${it.i}">&#9650;</button>` : '';
    const dnBtn = idx < list.length - 1 ? `<button data-dn="${it.i}">&#9660;</button>` : '';
    const lastConn = fmtLastConn(it.lastConn);
    tr.innerHTML = `<td>${it.i}</td><td>${it.ssid}</td><td class="ts">${lastConn}</td><td>${upBtn}${dnBtn}</td><td><button data-del="${it.i}">Delete</button></td>`;
    tb.appendChild(tr);
  });
}

function renderScan(list) {
  const tb = document.querySelector('#scan-table tbody');
  tb.innerHTML = '';
  list.forEach((it) => {
    const tr = document.createElement('tr');
    tr.innerHTML = `<td>${it.ssid}</td><td>${it.rssi}</td><td>${it.enc}</td><td><button data-pick="${it.ssid}">Pick</button></td>`;
    tb.appendChild(tr);
  });
}

async function refreshAll() {
  try {
    const saved = await apiGetSaved();
    renderSaved(saved);
  } catch (e) { console.error(e); }
}

async function refreshAdcMonitor() {
  try {
    const adc = await apiGetAdc();
    adcApiFailCount = 0;
    const vAdc = Number(adc.vAdc);
    const vBatt = Number(adc.vBatt);
    const percent = Number(adc.percent);

    const adcVAdcEl = document.getElementById('adc-vAdc');
    const adcVBattEl = document.getElementById('adc-vBatt');
    const cfgVBattEl = document.getElementById('cfg-vBatt');
    const cfgBattPercentEl = document.getElementById('cfg-battPercent');

    if (adcVAdcEl && Number.isFinite(vAdc)) adcVAdcEl.textContent = vAdc.toFixed(3);
    if (adcVBattEl && Number.isFinite(vBatt)) adcVBattEl.textContent = vBatt.toFixed(2);
    if (cfgVBattEl && Number.isFinite(vBatt)) cfgVBattEl.textContent = vBatt.toFixed(2);
    if (cfgBattPercentEl && Number.isFinite(percent)) cfgBattPercentEl.textContent = String(Math.round(percent));
  } catch (e) {
    console.error('ADC refresh failed', e);
    adcApiFailCount++;

    if (adcApiFailCount >= API_RECONNECT_FAIL_THRESHOLD && ws && ws.readyState === WebSocket.OPEN) {
      console.warn('ADC API unreachable while WS is open, forcing reconnect');
      setTerminalState(false);
      appendTerminal('[TERM] API unreachable, reconnecting...\n');
      try { ws.close(); } catch (err) {}
      ws = null;
      wsWasOnline = false;
      scheduleWsReconnect();
    }
  }
}

const MAX_TERMINAL_CHARS = 20000;
const WS_RECONNECT_MIN_MS = 100;
const WS_RECONNECT_MAX_MS = 2500;
const WS_GUARD_INTERVAL_MS = 500;
const WS_STALE_TIMEOUT_MS = 3000;
const WS_CONNECT_TIMEOUT_MS = 2500;
const API_RECONNECT_FAIL_THRESHOLD = 2;

// WebSocket for battery + terminal streaming
let ws;
let termOutputEl = null;
let termStateEl = null;
let terminalCardEl = null;
let termFullscreenBtn = null;
let wsReconnectTimer = null;
let wsReconnectDelayMs = WS_RECONNECT_MIN_MS;
let wsWasOnline = false;
let wsEverConnected = false;
let lastWsRxMs = 0;
let wsConnectingSinceMs = 0;
let adcApiFailCount = 0;
let refreshUiAfterReconnect = null;

function setTerminalFullscreenUi(active) {
  if (!termFullscreenBtn) return;
  termFullscreenBtn.textContent = active ? 'Exit Full Screen' : 'Full Screen';
}

function isTerminalFallbackFullscreen() {
  return !!(terminalCardEl && terminalCardEl.classList.contains('is-fullscreen'));
}

function isTerminalFullscreen() {
  if (terminalCardEl && document.fullscreenElement) {
    return document.fullscreenElement === terminalCardEl;
  }
  return isTerminalFallbackFullscreen();
}

async function toggleTerminalFullscreen() {
  if (!terminalCardEl) return;

  try {
    if (document.fullscreenEnabled && terminalCardEl.requestFullscreen) {
      if (document.fullscreenElement === terminalCardEl) {
        await document.exitFullscreen();
      } else {
        await terminalCardEl.requestFullscreen();
      }
      setTerminalFullscreenUi(isTerminalFullscreen());
      return;
    }
  } catch (e) {
    console.warn('Fullscreen API failed, fallback mode used', e);
  }

  terminalCardEl.classList.toggle('is-fullscreen');
  document.body.classList.toggle('no-scroll', isTerminalFallbackFullscreen());
  setTerminalFullscreenUi(isTerminalFullscreen());
}

function appendTerminal(text) {
  if (!termOutputEl || !text) return;
  const stickToBottom = termOutputEl.scrollTop + termOutputEl.clientHeight >= termOutputEl.scrollHeight - 8;
  termOutputEl.textContent += text;
  if (termOutputEl.textContent.length > MAX_TERMINAL_CHARS) {
    termOutputEl.textContent = termOutputEl.textContent.slice(-MAX_TERMINAL_CHARS);
  }
  if (stickToBottom) {
    termOutputEl.scrollTop = termOutputEl.scrollHeight;
  }
}

function setTerminalState(connected) {
  if (!termStateEl) return;
  termStateEl.textContent = connected ? 'online' : 'offline';
  termStateEl.classList.toggle('ok', connected);
  termStateEl.classList.toggle('bad', !connected);
}

function sendTerminalCommand(cmd) {
  if (!ws || ws.readyState !== WebSocket.OPEN) {
    appendTerminal('[TERM] WebSocket not connected\n');
    return false;
  }
  ws.send(`CMD:${cmd}`);
  return true;
}

function scheduleWsReconnect() {
  if (wsReconnectTimer) return;
  const waitMs = wsReconnectDelayMs;
  wsReconnectTimer = setTimeout(() => {
    wsReconnectTimer = null;
    connectWebSocket();
  }, waitMs);
  wsReconnectDelayMs = Math.min(Math.round(wsReconnectDelayMs * 1.25), WS_RECONNECT_MAX_MS);
}

function connectWebSocket() {
  if (ws && (ws.readyState === WebSocket.OPEN || ws.readyState === WebSocket.CONNECTING)) {
    return;
  }

  if (wsReconnectTimer) {
    clearTimeout(wsReconnectTimer);
    wsReconnectTimer = null;
  }

  const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
  const wsUrl = `${protocol}//${window.location.hostname}:81/`;
  try {
    wsConnectingSinceMs = Date.now();
    ws = new WebSocket(wsUrl);
  } catch (e) {
    console.error('WebSocket create failed:', e);
    scheduleWsReconnect();
    return;
  }

  ws.onopen = async () => {
    console.log('WebSocket connected');
    const firstConnection = !wsEverConnected;
    wsEverConnected = true;
    wsWasOnline = true;
    adcApiFailCount = 0;
    lastWsRxMs = Date.now();
    wsConnectingSinceMs = 0;
    wsReconnectDelayMs = WS_RECONNECT_MIN_MS;
    setTerminalState(true);
    appendTerminal(firstConnection ? '[TERM] Connected\n' : '[TERM] Reconnected\n');
    if (typeof refreshUiAfterReconnect === 'function') {
      try {
        await refreshUiAfterReconnect();
      } catch (e) {
        console.error('UI refresh after reconnect failed:', e);
      }
    }
  };

  ws.onmessage = (event) => {
    adcApiFailCount = 0;
    lastWsRxMs = Date.now();
    const msg = event.data;

    if (typeof msg !== 'string') return;

    // Parse battery percentage message: "BATT:XX"
    if (msg.startsWith('BATT:')) {
      const percent = parseInt(msg.substring(5), 10);
      if (!isNaN(percent)) {
        const el = document.getElementById('cfg-battPercent');
        if (el) el.textContent = percent;
      }
      return;
    }

    // Terminal stream chunk
    if (msg.startsWith('TERM:')) {
      appendTerminal(msg.substring(5));
    }
  };

  ws.onerror = (error) => {
    console.error('WebSocket error:', error);
  };

  ws.onclose = () => {
    const wasOnline = wsWasOnline;
    wsWasOnline = false;
    wsConnectingSinceMs = 0;
    ws = null;
    console.log('WebSocket disconnected, reconnecting...');
    setTerminalState(false);
    if (wasOnline) appendTerminal('[TERM] Disconnected, retrying...\n');
    scheduleWsReconnect();
  };
}

window.addEventListener('DOMContentLoaded', () => {
  const addForm = document.getElementById('add-form');
  const ssid = document.getElementById('ssid');
  const pass = document.getElementById('pass');
  const addMsg = document.getElementById('add-msg');
  const scanBtn = document.getElementById('scan-btn');
  const savedTable = document.getElementById('saved-table');
  const scanTable = document.getElementById('scan-table');
  const rebootForm = document.getElementById('reboot-form');
  const termForm = document.getElementById('term-form');
  const termInput = document.getElementById('term-input');
  const termClear = document.getElementById('term-clear');
  termFullscreenBtn = document.getElementById('term-fullscreen');
  terminalCardEl = document.getElementById('terminal-card');
  termOutputEl = document.getElementById('term-output');
  termStateEl = document.getElementById('term-state');
  setTerminalState(false);
  setTerminalFullscreenUi(false);

  addForm.addEventListener('submit', async (e) => {
    e.preventDefault();
    addMsg.textContent = '';
    const res = await apiSave(ssid.value.trim(), pass.value);
    if (res && res.ok) {
      addMsg.textContent = 'Saved';
      addMsg.classList.remove('err');
      await refreshAll();
    } else {
      addMsg.textContent = 'Failed to save';
      addMsg.classList.add('err');
    }
  });

  scanBtn.addEventListener('click', async () => {
    try {
      const list = await apiScan();
      renderScan(list);
    } catch (e) { console.error(e); }
  });

  scanTable.addEventListener('click', (e) => {
    const t = e.target;
    if (t && t.matches('button[data-pick]')) {
      ssid.value = t.getAttribute('data-pick') || '';
      ssid.focus();
    }
  });

  savedTable.addEventListener('click', async (e) => {
    const t = e.target;
    if (t && t.matches('button[data-del]')) {
      const idx = Number(t.getAttribute('data-del'));
      await apiDelete(idx);
      await refreshAll();
    } else if (t && t.matches('button[data-up]')) {
      const idx = Number(t.getAttribute('data-up'));
      await apiMove(idx, idx - 1);
      await refreshAll();
    } else if (t && t.matches('button[data-dn]')) {
      const idx = Number(t.getAttribute('data-dn'));
      await apiMove(idx, idx + 1);
      await refreshAll();
    }
  });

  rebootForm.addEventListener('submit', async (e) => {
    e.preventDefault();
    await apiReboot();
  });

  document.getElementById('restart-charge-btn').addEventListener('click', async () => {
    await fetch('/api/restart-charge', { method: 'POST' });
  });

  if (termForm && termInput && termClear) {
    termForm.addEventListener('submit', (e) => {
      e.preventDefault();
      const cmd = termInput.value.trim();
      if (!cmd) return;
      appendTerminal(`> ${cmd}\n`);
      sendTerminalCommand(cmd);
      termInput.value = '';
      termInput.focus();
    });

    termClear.addEventListener('click', () => {
      if (termOutputEl) termOutputEl.textContent = '';
      termInput.focus();
    });
  }

  if (termFullscreenBtn) {
    termFullscreenBtn.addEventListener('click', () => {
      toggleTerminalFullscreen();
    });
  }

  document.addEventListener('fullscreenchange', () => {
    if (!document.fullscreenElement && terminalCardEl) {
      terminalCardEl.classList.remove('is-fullscreen');
      document.body.classList.remove('no-scroll');
    }
    setTerminalFullscreenUi(isTerminalFullscreen());
  });

  document.addEventListener('keydown', (e) => {
    if (e.key === 'Escape' && isTerminalFallbackFullscreen() && !document.fullscreenElement) {
      terminalCardEl.classList.remove('is-fullscreen');
      document.body.classList.remove('no-scroll');
      setTerminalFullscreenUi(false);
    }
  });

  // Settings
  const cfgIds = ['cfg-ota','cfg-otaInterval','cfg-txPower','cfg-failsafe','cfg-beacon',
    'cfg-apPrefix','cfg-alwaysPanel','cfg-steerInvert','cfg-steerGain','cfg-steerDeadzone','cfg-steerFilter',
    'cfg-battWarn','cfg-battOff','cfg-maxThrottle'];
  const cfg = {};
  cfgIds.forEach(id => cfg[id] = document.getElementById(id));
  const cfgVersion = document.getElementById('cfg-version');
  const cfgSave = document.getElementById('cfg-save');
  const cfgMsg = document.getElementById('cfg-msg');

  const applySettingsToUi = (s) => {
    if (!s) return;
    cfgVersion.textContent = s.version;
    cfg['cfg-ota'].checked = s.otaEnabled;
    cfg['cfg-otaInterval'].value = Math.round(s.otaIntervalMs / 60000);
    cfg['cfg-txPower'].value = String(s.wifiTxPower);
    cfg['cfg-failsafe'].value = String(s.failsafeMs);
    cfg['cfg-beacon'].value = String(s.beaconIntervalMs);
    cfg['cfg-apPrefix'].value = s.apPrefix;
    cfg['cfg-alwaysPanel'].checked = !!s.alwaysStartPanel;
    cfg['cfg-steerInvert'].checked = s.steerInvert;
    cfg['cfg-steerGain'].value = s.steerGain;
    cfg['cfg-steerDeadzone'].value = s.steerDeadzone;
    cfg['cfg-steerFilter'].value = s.steerFilter;
    cfg['cfg-battWarn'].value = s.battWarnV;
    cfg['cfg-battOff'].value = s.battOffV;
    cfg['cfg-maxThrottle'].value = String(s.maxThrottlePct);
    document.getElementById('cfg-adcCorr').textContent = s.adcCorrFactor.toFixed(4);
    document.getElementById('cfg-vBatt').textContent = s.vBatt.toFixed(2);
  };

  const refreshSettingsUi = async () => {
    try {
      const s = await apiGetSettings();
      applySettingsToUi(s);
    } catch (e) {
      console.error('Settings refresh failed', e);
    }
  };

  refreshSettingsUi();

  refreshUiAfterReconnect = async () => {
    await refreshAll();
    await refreshAdcMonitor();
    await refreshSettingsUi();
  };

  cfgSave.addEventListener('click', async () => {
    cfgMsg.textContent = '';
    const res = await apiSaveSettings({
      otaEnabled: cfg['cfg-ota'].checked ? '1' : '0',
      otaIntervalMs: Math.max(1, Math.min(1440, parseInt(cfg['cfg-otaInterval'].value))) * 60000,
      wifiTxPower: cfg['cfg-txPower'].value,
      failsafeMs: cfg['cfg-failsafe'].value,
      beaconIntervalMs: cfg['cfg-beacon'].value,
      apPrefix: cfg['cfg-apPrefix'].value,
      alwaysStartPanel: cfg['cfg-alwaysPanel'].checked ? '1' : '0',
      steerInvert: cfg['cfg-steerInvert'].checked ? '1' : '0',
      steerGain: cfg['cfg-steerGain'].value,
      steerDeadzone: cfg['cfg-steerDeadzone'].value,
      steerFilter: cfg['cfg-steerFilter'].value,
      battWarnV: cfg['cfg-battWarn'].value,
      battOffV: cfg['cfg-battOff'].value,
      maxThrottlePct: Math.max(10, Math.min(100, parseInt(cfg['cfg-maxThrottle'].value)))
    });
    if (res && res.ok) {
      cfgMsg.textContent = 'Settings saved';
      cfgMsg.classList.remove('err');
    } else {
      cfgMsg.textContent = 'Failed to save';
      cfgMsg.classList.add('err');
    }
  });

  document.getElementById('cfg-calibrate').addEventListener('click', async () => {
    const realV = document.getElementById('cfg-realVoltage').value;
    if (!realV) return;
    const res = await apiSaveSettings({ adcRealVoltage: realV });
    if (res && res.ok) {
      const s = await apiGetSettings();
      document.getElementById('cfg-adcCorr').textContent = s.adcCorrFactor.toFixed(4);
      document.getElementById('cfg-vBatt').textContent = s.vBatt.toFixed(2);
      cfgMsg.textContent = 'Calibrated';
      cfgMsg.classList.remove('err');
    }
  });

  refreshAll();
  setInterval(refreshAll, 15000);
  refreshAdcMonitor();
  setInterval(refreshAdcMonitor, 2000);
  connectWebSocket();
  setInterval(() => {
    const now = Date.now();

    if (!ws) {
      connectWebSocket();
      return;
    }

    if (ws.readyState === WebSocket.OPEN) {
      if (lastWsRxMs > 0 && (now - lastWsRxMs) > WS_STALE_TIMEOUT_MS) {
        console.warn('WebSocket stale, forcing reconnect');
        setTerminalState(false);
        appendTerminal('[TERM] Connection stale, reconnecting...\n');
        try { ws.close(); } catch (e) {}
        ws = null;
        wsWasOnline = false;
        scheduleWsReconnect();
      }
      return;
    }

    if (ws.readyState === WebSocket.CONNECTING) {
      if (wsConnectingSinceMs > 0 && (now - wsConnectingSinceMs) > WS_CONNECT_TIMEOUT_MS) {
        console.warn('WebSocket connect timeout, forcing reconnect');
        try { ws.close(); } catch (e) {}
        ws = null;
        wsConnectingSinceMs = 0;
        scheduleWsReconnect();
      }
      return;
    }

    // CLOSING or CLOSED
    connectWebSocket();
  }, WS_GUARD_INTERVAL_MS);
});
