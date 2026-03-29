async function apiGetSaved() {
  const r = await fetch('/api/saved');
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
async function apiScan() {
  const r = await fetch('/api/scan', { method: 'POST' });
  return r.json();
}
async function apiReboot() {
  await fetch('/api/reboot', { method: 'POST' });
}
async function apiGetSettings() {
  const r = await fetch('/api/settings');
  return r.json();
}
async function apiSaveSettings(data) {
  const body = new URLSearchParams(data);
  const r = await fetch('/api/settings', { method: 'POST', headers: { 'Content-Type': 'application/x-www-form-urlencoded' }, body });
  return r.json();
}

function renderSaved(list) {
  const tb = document.querySelector('#saved-table tbody');
  tb.innerHTML = '';
  list.forEach((it) => {
    const tr = document.createElement('tr');
    tr.innerHTML = `<td>${it.i}</td><td>${it.ssid}</td><td><button data-del="${it.i}">Delete</button></td>`;
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

// WebSocket for ADC monitoring
let ws; 
function connectWebSocket() {
  const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
  const wsUrl = `${protocol}//${window.location.hostname}:81/`;
  ws = new WebSocket(wsUrl);
  
  ws.onopen = () => {
    console.log('WebSocket connected');
  };
  
  ws.onmessage = (event) => {
    const msg = event.data;
    // Parse ADC message: "ADC:vAdc,vBatt,samples" (e.g., "ADC:3.058,8.56,5211")
    if (msg.startsWith('ADC:')) {
      const parts = msg.substring(4).split(',');
      if (parts.length === 3) {
        const vAdc = parseFloat(parts[0]);
        const vBatt = parseFloat(parts[1]);
        const samples = parseInt(parts[2]);
        
        if (!isNaN(vAdc) && !isNaN(vBatt) && !isNaN(samples)) {
          document.getElementById('adc-vAdc').textContent = vAdc.toFixed(3);
          document.getElementById('adc-vBatt').textContent = vBatt.toFixed(2);
          document.getElementById('adc-samples').textContent = samples;
        }
      }
    }
  };
  
  ws.onerror = (error) => {
    console.error('WebSocket error:', error);
  };
  
  ws.onclose = () => {
    console.log('WebSocket disconnected, reconnecting in 2s...');
    setTimeout(connectWebSocket, 2000);
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
    }
  });

  rebootForm.addEventListener('submit', async (e) => {
    e.preventDefault();
    await apiReboot();
  });

  // Settings
  const cfgIds = ['cfg-ota','cfg-otaInterval','cfg-txPower','cfg-failsafe','cfg-beacon',
    'cfg-apPrefix','cfg-steerInvert','cfg-steerGain','cfg-steerDeadzone','cfg-steerFilter',
    'cfg-battWarn','cfg-battOff','cfg-maxThrottle'];
  const cfg = {};
  cfgIds.forEach(id => cfg[id] = document.getElementById(id));
  const cfgVersion = document.getElementById('cfg-version');
  const cfgSave = document.getElementById('cfg-save');
  const cfgMsg = document.getElementById('cfg-msg');

  apiGetSettings().then(s => {
    cfgVersion.textContent = s.version;
    cfg['cfg-ota'].checked = s.otaEnabled;
    cfg['cfg-otaInterval'].value = Math.round(s.otaIntervalMs / 60000);
    cfg['cfg-txPower'].value = String(s.wifiTxPower);
    cfg['cfg-failsafe'].value = String(s.failsafeMs);
    cfg['cfg-beacon'].value = String(s.beaconIntervalMs);
    cfg['cfg-apPrefix'].value = s.apPrefix;
    cfg['cfg-steerInvert'].checked = s.steerInvert;
    cfg['cfg-steerGain'].value = s.steerGain;
    cfg['cfg-steerDeadzone'].value = s.steerDeadzone;
    cfg['cfg-steerFilter'].value = s.steerFilter;
    cfg['cfg-battWarn'].value = s.battWarnV;
    cfg['cfg-battOff'].value = s.battOffV;
    cfg['cfg-maxThrottle'].value = String(s.maxThrottlePct);
    document.getElementById('cfg-adcCorr').textContent = s.adcCorrFactor.toFixed(4);
    document.getElementById('cfg-vBatt').textContent = s.vBatt.toFixed(2);
  }).catch(() => {});

  cfgSave.addEventListener('click', async () => {
    cfgMsg.textContent = '';
    const res = await apiSaveSettings({
      otaEnabled: cfg['cfg-ota'].checked ? '1' : '0',
      otaIntervalMs: Math.max(1, Math.min(1440, parseInt(cfg['cfg-otaInterval'].value))) * 60000,
      wifiTxPower: cfg['cfg-txPower'].value,
      failsafeMs: cfg['cfg-failsafe'].value,
      beaconIntervalMs: cfg['cfg-beacon'].value,
      apPrefix: cfg['cfg-apPrefix'].value,
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
  connectWebSocket();  // Connect to WebSocket for ADC monitoring
});
