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

  refreshAll();
});
