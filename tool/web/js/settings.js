// ============================================================
// Connection settings (doc/ruby_asterism/report/cf2.md)
// ============================================================
// Writes the board's WiFi settings (/home/wifi.toml), its Asterism settings
// (/home/asterism/config.toml) and its Asterism credentials
// (/home/asterism/<name>.tar) over the file service.
//
// The board takes these only while its settings window is open (the desktop
// menu's "BLE Setup", a few minutes). The window is asked for with
// CMD_SETTINGS; the board refuses the writes itself when it is closed, so the
// buttons here being disabled is a courtesy, not the lock.
//
// The .tar (the private key) is never read back: the board does not hand it
// out, and the Asterism console can issue it again. The WiFi password is read
// only while the window is open (the owner asked for it on the board); the
// rest of the WiFi settings (SSID, which file, the link) comes with every
// CMD_SETTINGS answer and never carries the password.

const CMD_SETTINGS = 0x17;

const WIFI_HOME_PATH   = '/home/wifi.toml';
const WIFI_ETC_PATH    = '/etc/wifi.toml';
const ASTERISM_DIR     = '/home/asterism';
const ASTERISM_CONFIG  = '/home/asterism/config.toml';
const ASTERISM_KEYS    = ['router', 'node', 'name'];
const SETTINGS_POLL_MS = 2000;
const TAR_MAX_SIZE     = 256 * 1024;

let settingsState = {
  supported: null,   // null = not asked yet, false = firmware has no window
  open: false,
  left: 0,
  leftAt: 0,         // Date.now() when `left` was read
  wifi: null,        // '/home/wifi.toml' | '/etc/wifi.toml' | '' (none)
  wifiInfo: null,    // { src, on, ssid, conn, ip } from CMD_SETTINGS, null on old firmware
  password: null,    // the current password, read while the window is open
  tars: [],          // [{ n, s }] in /home/asterism
  config: null,      // { router, node, name } from config.toml, or null
};
let settingsPollTimer = null;
let settingsBusy = false;

// ---- what goes into the files (pure; the browser test calls these) ----

// A TOML basic string: wifi_task.c reads the file with a full TOML parser.
function settingsTomlString(s) {
  let out = '"';
  for (const ch of String(s)) {
    const c = ch.codePointAt(0);
    if (ch === '"') out += '\\"';
    else if (ch === '\\') out += '\\\\';
    else if (c < 0x20 || c === 0x7f) out += '\\u' + c.toString(16).padStart(4, '0');
    else out += ch;
  }
  return out + '"';
}

function settingsWifiToml(ssid, password) {
  return '# This machine\'s WiFi, written by the Family mruby Console\n' +
         '[wifi]\n' +
         'enable = true\n' +
         'ssid = ' + settingsTomlString(ssid) + '\n' +
         'password = ' + settingsTomlString(password) + '\n';
}

// The same shape as FmrbAsterism.config_text (picoruby-fmrb-asterism): a
// header line, then the keys that have a value, in a fixed order. The
// board's reader undoes only \" and \\, so nothing else is escaped.
function settingsAsterismToml(c) {
  let out = '# This machine\'s Asterism settings (doc/ruby_asterism)\n';
  for (const k of ASTERISM_KEYS) {
    const v = (c[k] || '').trim();
    if (v === '') continue;
    out += k + ' = "' + v.replace(/\\/g, '\\\\').replace(/"/g, '\\"') + '"\n';
  }
  return out;
}

// What config.toml says: { router, node, name } with only the keys written.
// The same reading as FmrbAsterism.toml_string.
function settingsParseConfig(text) {
  const out = {};
  for (const raw of String(text).split('\n')) {
    const line = raw.trim();
    for (const k of ASTERISM_KEYS) {
      if (k in out || !line.startsWith(k)) continue;
      let rest = line.slice(k.length).trim();
      if (!rest.startsWith('=')) continue;
      rest = rest.slice(1).trim();
      if (!rest.startsWith('"')) continue;
      let v = '';
      for (let i = 1; i < rest.length; i++) {
        const ch = rest[i];
        if (ch === '\\' && i + 1 < rest.length) { v += rest[++i]; continue; }
        if (ch === '"') { out[k] = v; break; }
        v += ch;
      }
    }
  }
  return out;
}

// The name the .tar gets on the board, or null when it cannot be one.
function settingsTarName(fileName) {
  const base = String(fileName).split(/[\\/]/).pop();
  if (!/\.tar$/i.test(base)) return null;
  const clean = base.slice(0, -4).replace(/[^A-Za-z0-9._-]/g, '_');
  if (clean === '' || clean.startsWith('.')) return null;
  return clean + '.tar';
}

// Returns an error message, or '' when the WiFi fields can be written.
// One key's string value from a TOML file (a basic or a literal string), or
// null. Enough for wifi.toml, which wifi_task.c writes and reads.
function settingsTomlValue(text, key) {
  for (const raw of String(text).split('\n')) {
    const line = raw.trim();
    if (!line.startsWith(key)) continue;
    let rest = line.slice(key.length).trim();
    if (!rest.startsWith('=')) continue;
    rest = rest.slice(1).trim();
    if (rest.startsWith("'")) {
      const end = rest.indexOf("'", 1);
      return end > 0 ? rest.slice(1, end) : null;
    }
    if (!rest.startsWith('"')) return null;
    let v = '';
    for (let i = 1; i < rest.length; i++) {
      const ch = rest[i];
      if (ch === '"') return v;
      if (ch !== '\\') { v += ch; continue; }
      const e = rest[++i];
      const simple = { n: '\n', t: '\t', r: '\r', b: '\b', f: '\f' };
      if (e in simple) v += simple[e];
      else if (e === 'u' || e === 'U') {
        const len = e === 'u' ? 4 : 8;
        v += String.fromCodePoint(parseInt(rest.slice(i + 1, i + 1 + len), 16));
        i += len;
      } else v += e;
    }
    return null;
  }
  return null;
}

// Returns an error message, or '' when the WiFi fields can be written. An
// empty password is not "keep the old one": the console only keeps it when
// it has read it (see settingsWriteWifi).
function settingsCheckWifi(ssid, password) {
  const enc = new TextEncoder();
  if (ssid === '') return 'SSID is empty';
  if (enc.encode(ssid).length > 32) return 'SSID is longer than 32 bytes';
  const n = enc.encode(password).length;
  if (n === 0) return 'enter the password';
  if (n < 8) return 'the password is shorter than 8 characters';
  if (n > 64) return 'the password is longer than 64 characters';
  return '';
}

function settingsCheckAsterism(c) {
  for (const k of ASTERISM_KEYS) {
    if (/[\r\n]/.test(c[k] || '')) return k + ' has a line break';
  }
  const r = (c.router || '').trim();
  if (r !== '' && !/^(tls|tcp|udp|ws|wss|quic)\//.test(r)) {
    return 'router should look like tls/host:port or tcp/host:port';
  }
  return '';
}

// ---- talking to the board ----

function settingsWindowLeft() {
  if (!settingsState.open) return 0;
  const gone = Math.floor((Date.now() - settingsState.leftAt) / 1000);
  return Math.max(0, settingsState.left - gone);
}

async function settingsAskWindow() {
  const { meta } = await sendCommand(CMD_SETTINGS, {});
  if (!meta.ok) {
    settingsState.supported = false;
    settingsState.open = false;
    return;
  }
  settingsState.supported = true;
  settingsState.open = !!meta.open;
  settingsState.wifiInfo = meta.wifi || null;
  settingsState.left = meta.left || 0;
  settingsState.leftAt = Date.now();
}

async function settingsList(path) {
  const { meta } = await sendCommand(CMD_LS, { path });
  return meta.ok ? (meta.entries || []) : null;
}

async function settingsReadState() {
  await settingsAskWindow();

  const home = await settingsList('/home') || [];
  const etc = await settingsList('/etc') || [];
  const has = (list, name) => list.some(e => e.n === name && e.t === 'f');
  settingsState.wifi = settingsState.wifiInfo ? settingsState.wifiInfo.src
                     : has(home, 'wifi.toml') ? WIFI_HOME_PATH
                     : has(etc, 'wifi.toml') ? WIFI_ETC_PATH : '';
  await settingsReadPassword();

  const dir = await settingsList(ASTERISM_DIR);
  settingsState.tars = (dir || []).filter(e => e.t === 'f' && /\.tar$/i.test(e.n));
  settingsState.config = null;
  if (dir && dir.some(e => e.n === 'config.toml')) {
    try {
      const bytes = await deviceReadFile(ASTERISM_CONFIG);
      settingsState.config = settingsParseConfig(new TextDecoder().decode(bytes));
    } catch (e) {
      log('settings: could not read ' + ASTERISM_CONFIG + ': ' + e.message, 'err');
    }
  }
}

// The current password, read while the window is open (the board refuses
// otherwise). Forgotten as soon as the window closes.
async function settingsReadPassword() {
  if (!settingsState.open || !settingsState.wifi || settingsState.password !== null) return;
  try {
    const bytes = await deviceReadFile(settingsState.wifi);
    const pw = settingsTomlValue(new TextDecoder().decode(bytes), 'password');
    settingsState.password = pw === null ? '' : pw;
    const field = settingsEl('setPassword');
    if (field && field.value === '') field.value = settingsState.password;
  } catch (e) {
    log('settings: could not read the WiFi password: ' + e.message, 'err');
  }
}

function settingsForgetPassword() {
  if (settingsState.password === null) return;
  settingsState.password = null;
  const field = settingsEl('setPassword');
  if (field) field.value = '';
}

// ---- the pane ----

function settingsEl(id) { return document.getElementById(id); }

function settingsSetMsg(id, text, cls) {
  const el = settingsEl(id);
  if (!el) return;
  el.textContent = text;
  el.className = 'settings-msg' + (cls ? ' ' + cls : '');
}

function settingsRender() {
  const connected = !!(rxChar && txChar);
  const st = settingsState;
  const win = settingsEl('setWindow');
  const left = settingsWindowLeft();
  const open = connected && st.supported && st.open && left > 0;

  if (!connected) {
    win.textContent = 'Not connected.';
    win.className = 'settings-window';
  } else if (st.supported === false) {
    win.textContent = 'This firmware has no settings window: update the board\'s firmware to write settings from here.';
    win.className = 'settings-window closed';
  } else if (open) {
    const m = Math.floor(left / 60);
    const s = String(left % 60).padStart(2, '0');
    win.textContent = 'The board is accepting settings: ' + m + ':' + s + ' left.';
    win.className = 'settings-window open';
  } else {
    win.textContent = 'The board is not accepting settings. On the board, open the menu ' +
                      'and choose "BLE Setup" (it stays open for 5 minutes).';
    win.className = 'settings-window closed';
  }

  const wifiNow = settingsEl('setWifiNow');
  const wi = st.wifiInfo;
  let where;
  if (st.wifi === WIFI_HOME_PATH) where = WIFI_HOME_PATH;
  else if (st.wifi === WIFI_ETC_PATH) where = 'the firmware\'s ' + WIFI_ETC_PATH + ' (writing here puts ' + WIFI_HOME_PATH + ' in front of it)';
  if (!connected || st.wifi === null) wifiNow.textContent = '-';
  else if (!st.wifi) wifiNow.textContent = 'none';
  else if (!wi) wifiNow.textContent = 'set (' + where + ')';
  else {
    wifiNow.textContent = 'SSID "' + wi.ssid + '" from ' + where +
      (wi.on ? '' : ', disabled (enable = false)') +
      (wi.conn ? '; connected, IP ' + wi.ip : '; not connected now');
  }
  const pw = settingsEl('setPassword');
  if (pw) {
    pw.placeholder = !st.wifi ? '' : (st.password !== null ? '' : 'set (not shown; open BLE Setup on the board to load it)');
  }
  const pwNow = settingsEl('setPwNow');
  if (pwNow) {
    pwNow.textContent = (!connected || !st.wifi) ? '' :
      st.password !== null ? 'loaded from the board (the settings window is open)' : 'set (not shown)';
  }

  const tarNow = settingsEl('setTarNow');
  if (!connected || st.wifi === null) tarNow.textContent = '-';
  else if (st.tars.length === 0) tarNow.textContent = 'none';
  else tarNow.textContent = 'set (' + st.tars.map(t => t.n).join(', ') + ')';

  const cfgNow = settingsEl('setCfgNow');
  if (!connected || st.wifi === null) cfgNow.textContent = '-';
  else if (!st.config) cfgNow.textContent = 'no ' + ASTERISM_CONFIG;
  else {
    const parts = ASTERISM_KEYS.filter(k => st.config[k]).map(k => k + ' = ' + st.config[k]);
    cfgNow.textContent = parts.length ? parts.join(', ') : 'empty';
  }

  document.querySelectorAll('[data-needs-settings]').forEach(el => {
    el.disabled = !open || settingsBusy;
  });
  const reload = settingsEl('setReload');
  if (reload) reload.disabled = !connected || settingsBusy;
}

// Fill the Asterism fields from what the board has (only on load, so what
// the user typed is not overwritten by the poll).
function settingsFillForm() {
  const wi = settingsState.wifiInfo;
  if (wi && wi.ssid) settingsEl('setSsid').value = wi.ssid;
  const c = settingsState.config || {};
  settingsEl('setRouter').value = c.router || '';
  settingsEl('setNode').value = c.node || '';
  settingsEl('setName').value = c.name || '';
}

async function settingsReload() {
  if (!rxChar || settingsBusy) return;
  settingsBusy = true;
  settingsRender();
  try {
    await settingsReadState();
    settingsFillForm();
  } catch (e) {
    log('settings: ' + e.message, 'err');
  }
  settingsBusy = false;
  settingsRender();
}

async function settingsPoll() {
  if (!rxChar || settingsBusy) { settingsRender(); return; }
  try {
    await settingsAskWindow();
    if (!settingsState.open) settingsForgetPassword();
    else if (settingsState.password === null) await settingsReadPassword();
  } catch (e) {
    // A missed poll is not worth a log line; the next one asks again.
  }
  settingsRender();
}

function settingsStartPolling() {
  settingsStopPolling();
  settingsPollTimer = setInterval(() => {
    // Ask the board every few seconds, count down locally in between.
    if (Date.now() - settingsState.leftAt >= SETTINGS_POLL_MS) settingsPoll();
    else settingsRender();
  }, 1000);
}

function settingsStopPolling() {
  if (settingsPollTimer !== null) clearInterval(settingsPollTimer);
  settingsPollTimer = null;
}

// Run one write; the board's refusal (window closed) comes back as an error
// and is shown as it is.
async function settingsWrite(msgId, fn) {
  if (settingsBusy) return;
  settingsBusy = true;
  settingsRender();
  settingsSetMsg(msgId, 'Writing...', '');
  try {
    const done = await fn();
    settingsSetMsg(msgId, done, 'ok');
    log('settings: ' + done, 'ok');
  } catch (e) {
    settingsSetMsg(msgId, 'Not written: ' + e.message, 'err');
    log('settings: ' + e.message, 'err');
  }
  try {
    await settingsReadState();
  } catch (e) {
    // The message above already says what happened to the write.
  }
  settingsBusy = false;
  settingsRender();
}

async function settingsEnsureDir() {
  const { meta } = await sendCommand(CMD_LS, { path: ASTERISM_DIR });
  if (meta.ok) return;
  const r = await sendCommand(CMD_MKDIR, { path: ASTERISM_DIR });
  if (!r.meta.ok) throw new Error('cannot make ' + ASTERISM_DIR + ': ' + (r.meta.err || 'refused'));
}

function settingsWriteWifi() {
  const ssid = settingsEl('setSsid').value;
  let password = settingsEl('setPassword').value;
  // Empty keeps the current password only when it was read from the board
  // (window open); otherwise there is nothing to keep it from.
  if (password === '' && settingsState.open && settingsState.password) password = settingsState.password;
  const bad = settingsCheckWifi(ssid, password);
  if (bad) { settingsSetMsg('setWifiMsg', bad, 'err'); return; }
  return settingsWrite('setWifiMsg', async () => {
    await deviceWriteFile(WIFI_HOME_PATH, new TextEncoder().encode(settingsWifiToml(ssid, password)));
    settingsState.password = password;
    return 'WiFi written to ' + WIFI_HOME_PATH + '. It is used from the next start: restart the board (menu > Reset).';
  });
}

function settingsWriteAsterism() {
  const c = {
    router: settingsEl('setRouter').value,
    node: settingsEl('setNode').value,
    name: settingsEl('setName').value,
  };
  const bad = settingsCheckAsterism(c);
  if (bad) { settingsSetMsg('setCfgMsg', bad, 'err'); return; }
  return settingsWrite('setCfgMsg', async () => {
    await settingsEnsureDir();
    await deviceWriteFile(ASTERISM_CONFIG, new TextEncoder().encode(settingsAsterismToml(c)));
    return 'Written to ' + ASTERISM_CONFIG + '. Applications read it when they start: ' +
           'restart the application that connects.';
  });
}

function settingsWriteTar() {
  const input = settingsEl('setTarFile');
  const file = input.files && input.files[0];
  if (!file) { settingsSetMsg('setTarMsg', 'choose the .tar the console handed out', 'err'); return; }
  const name = settingsTarName(file.name);
  if (!name) { settingsSetMsg('setTarMsg', 'the file has to be a .tar', 'err'); return; }
  if (file.size === 0 || file.size > TAR_MAX_SIZE) {
    settingsSetMsg('setTarMsg', 'the file is empty or too large for a credentials .tar', 'err');
    return;
  }
  const removeOthers = settingsEl('setTarReplace').checked;
  return settingsWrite('setTarMsg', async () => {
    await settingsEnsureDir();
    const data = new Uint8Array(await file.arrayBuffer());
    const path = ASTERISM_DIR + '/' + name;
    await deviceWriteFile(path, data, (n, total) => {
      settingsSetMsg('setTarMsg', 'Writing... ' + formatSize(n) + ' / ' + formatSize(total), '');
    });
    // With more than one .tar the board uses the first by name, which need
    // not be the one just written.
    const removed = [];
    if (removeOthers) {
      const dir = await settingsList(ASTERISM_DIR) || [];
      for (const e of dir) {
        if (e.t !== 'f' || !/\.tar$/i.test(e.n) || e.n === name) continue;
        const { meta } = await sendCommand(CMD_RM, { path: ASTERISM_DIR + '/' + e.n });
        if (!meta.ok) throw new Error('wrote ' + name + ' but could not remove ' + e.n + ': ' + (meta.err || 'refused'));
        removed.push(e.n);
      }
    }
    input.value = '';
    return 'Credentials written to ' + path + (removed.length ? ' (removed ' + removed.join(', ') + ')' : '') +
           '. Restart the application that connects.';
  });
}

// Called by app.js on connect and disconnect.
function settingsOnConnection(connected) {
  if (!connected) {
    settingsState = { supported: null, open: false, left: 0, leftAt: 0,
                      wifi: null, wifiInfo: null, password: null, tars: [], config: null };
    const pw = settingsEl('setPassword');
    if (pw) pw.value = '';
    settingsBusy = false;
    settingsStopPolling();
    settingsRender();
    return;
  }
  if (settingsEl('settingsPane').classList.contains('active')) {
    settingsReload();
    settingsStartPolling();
  }
}

registerTabChangeListener('settings', () => {
  settingsRender();
  if (rxChar) {
    settingsReload();
    settingsStartPolling();
  }
});
// Polling stops when another tab is shown (switchTab calls the listener of
// the tab it shows only), so check from the outside.
document.addEventListener('DOMContentLoaded', () => settingsRender());
setInterval(() => {
  const pane = settingsEl('settingsPane');
  if (pane && !pane.classList.contains('active')) settingsStopPolling();
}, 1000);
