// ============================================================
// BLE UUIDs (must match ble_task.c)
// ============================================================
// UUID string is big-endian representation of BLE_UUID128_INIT (little-endian).
// BLE_UUID128_INIT(0x0Y, 0x00,..., 0x45, 0x4c, 0x42, 0x59, 0x52, 0x42, 0x4d, 0x41, 0x52, 0x46)
//   byte[15..12]=46,52,41,4d  byte[11..10]=42,52  byte[9..8]=59,42
//   byte[7..6]=4c,45  byte[5..0]=00,00,00,00,00,0Y
const SERVICE_UUID      = '4652414d-4252-5942-4c45-000000000001';
const DEVICE_INFO_UUID  = '4652414d-4252-5942-4c45-000000000002';
const FS_RX_UUID        = '4652414d-4252-5942-4c45-000000000003';
const FS_TX_UUID        = '4652414d-4252-5942-4c45-000000000004';

// Protocol constants
const CMD_CD     = 0x11;
const CMD_LS     = 0x12;
const CMD_RM     = 0x13;
const CMD_MKDIR  = 0x14;
const CMD_STATFS = 0x15;
const CMD_RENAME = 0x16;
const CMD_GET    = 0x21;
const CMD_PUT    = 0x22;
const CMD_LOG_SUBSCRIBE   = 0x31;
const CMD_LOG_UNSUBSCRIBE = 0x32;
const CMD_LOG_SET_LEVEL   = 0x33;

const WRITE_CHUNK_SIZE = 200; // Conservative BLE write chunk size
const FILE_CHUNK_SIZE  = 2048;
const EDITOR_MAX_SIZE  = 256 * 1024;
const TEXT_EXTENSIONS = new Set([
  'rb', 'txt', 'md', 'toml', 'json', 'yml', 'yaml',
  'csv', 'ini', 'conf', 'log', 'cfg', 'xml', 'html',
  'css', 'js', 'c', 'h', 'cpp', 'hpp', 'py', 'sh'
]);
const PRISM_LANG_BY_EXT = {
  rb: 'ruby',
  toml: 'toml',
  json: 'json',
  md: 'markdown',
  markdown: 'markdown'
};

// ============================================================
// State
// ============================================================
let bleDevice = null;
let rxChar = null;
let txChar = null;
let currentPath = '/';
let rxBuffer = new Uint8Array(0);
let pendingResolve = null;
let pendingReject = null;

// Log streaming state
let logSubscribed = false;       // server has accepted SUBSCRIBE, push is live
let logWantSubscribe = false;    // user intent (re-subscribe automatically on reconnect)
let logLastSeq = -1;             // last seen seq (for gap detection)
let logLineCap = 1000;           // max DOM lines kept in #deviceLogBox
let logFilterText = '';
const logTextDecoder = new TextDecoder();

// ============================================================
// CRC32
// ============================================================
const crc32Table = new Uint32Array(256);
(function() {
  for (let i = 0; i < 256; i++) {
    let c = i;
    for (let j = 0; j < 8; j++) {
      c = (c & 1) ? (0xEDB88320 ^ (c >>> 1)) : (c >>> 1);
    }
    crc32Table[i] = c >>> 0;
  }
})();

function crc32(data) {
  let crc = 0xFFFFFFFF;
  for (let i = 0; i < data.length; i++) {
    crc = (crc >>> 8) ^ crc32Table[(crc ^ data[i]) & 0xFF];
  }
  return (crc ^ 0xFFFFFFFF) >>> 0;
}

// ============================================================
// COBS encode/decode
// ============================================================
function cobsEncode(input) {
  const output = [];
  let codeIdx = 0;
  output.push(0); // placeholder
  let code = 1;

  for (let i = 0; i < input.length; i++) {
    if (input[i] === 0) {
      output[codeIdx] = code;
      code = 1;
      codeIdx = output.length;
      output.push(0);
    } else {
      output.push(input[i]);
      code++;
      if (code === 0xFF) {
        output[codeIdx] = code;
        code = 1;
        codeIdx = output.length;
        output.push(0);
      }
    }
  }
  output[codeIdx] = code;
  return new Uint8Array(output);
}

function cobsDecode(input) {
  const output = [];
  let i = 0;
  while (i < input.length) {
    const code = input[i++];
    if (code === 0) break;
    for (let j = 1; j < code && i < input.length; j++) {
      output.push(input[i++]);
    }
    if (code < 0xFF && i < input.length) {
      output.push(0);
    }
  }
  return new Uint8Array(output);
}

// ============================================================
// Protocol: build packet and parse response
// ============================================================
function buildPacket(cmd, jsonStr, binaryData) {
  const jsonBytes = new TextEncoder().encode(jsonStr);
  const jsonLen = jsonBytes.length;
  const binLen = binaryData ? binaryData.length : 0;

  // [cmd(1)][json_len(2 BE)][json][binary]
  const body = new Uint8Array(1 + 2 + jsonLen + binLen);
  body[0] = cmd;
  body[1] = (jsonLen >> 8) & 0xFF;
  body[2] = jsonLen & 0xFF;
  body.set(jsonBytes, 3);
  if (binaryData) {
    body.set(binaryData, 3 + jsonLen);
  }

  // Append CRC32 (big-endian)
  const crcVal = crc32(body);
  const packet = new Uint8Array(body.length + 4);
  packet.set(body);
  packet[body.length]     = (crcVal >> 24) & 0xFF;
  packet[body.length + 1] = (crcVal >> 16) & 0xFF;
  packet[body.length + 2] = (crcVal >> 8)  & 0xFF;
  packet[body.length + 3] = crcVal & 0xFF;

  // COBS encode + delimiter
  const encoded = cobsEncode(packet);
  const frame = new Uint8Array(encoded.length + 1);
  frame.set(encoded);
  frame[encoded.length] = 0x00; // delimiter
  return frame;
}

function parseResponse(data) {
  // data is COBS-encoded (without delimiter)
  const raw = cobsDecode(data);
  if (raw.length < 7) throw new Error('Response too short');

  // Verify CRC32
  const body = raw.slice(0, raw.length - 4);
  const rcvCrc = ((raw[raw.length-4] << 24) | (raw[raw.length-3] << 16) |
                  (raw[raw.length-2] << 8) | raw[raw.length-1]) >>> 0;
  const calcCrc = crc32(body);
  if (rcvCrc !== calcCrc) throw new Error('CRC mismatch');

  const jsonLen = (body[1] << 8) | body[2];
  const jsonStr = new TextDecoder().decode(body.slice(3, 3 + jsonLen));
  const meta = JSON.parse(jsonStr);

  let binData = null;
  if (meta.bin && meta.bin > 0) {
    binData = body.slice(3 + jsonLen, 3 + jsonLen + meta.bin);
  }

  return { meta, binData };
}

// ============================================================
// BLE communication
// ============================================================
function onTxNotification(event) {
  const value = new Uint8Array(event.target.value.buffer);

  // Append to rxBuffer
  const combined = new Uint8Array(rxBuffer.length + value.length);
  combined.set(rxBuffer);
  combined.set(value, rxBuffer.length);
  rxBuffer = combined;

  // Check for delimiter (0x00)
  const delimIdx = rxBuffer.indexOf(0x00);
  if (delimIdx >= 0) {
    const frameData = rxBuffer.slice(0, delimIdx);
    rxBuffer = rxBuffer.slice(delimIdx + 1);

    let result;
    try {
      result = parseResponse(frameData);
    } catch (e) {
      if (pendingReject) {
        pendingReject(e);
        pendingResolve = null;
        pendingReject = null;
      }
      return;
    }

    // Unsolicited "log" event: route to log handler instead of resolving
    // the pending request promise (which expects a 1:1 response).
    if (result.meta && result.meta.evt === 'log') {
      handleLogEvent(result.meta, result.binData);
      return;
    }

    if (pendingResolve) {
      pendingResolve(result);
      pendingResolve = null;
      pendingReject = null;
    }
  }
}

async function sendCommand(cmd, params, binaryData) {
  if (!rxChar || !txChar) throw new Error('Not connected');

  const frame = buildPacket(cmd, JSON.stringify(params), binaryData);

  // Fragment write into BLE-safe chunks
  for (let off = 0; off < frame.length; off += WRITE_CHUNK_SIZE) {
    const chunk = frame.slice(off, Math.min(off + WRITE_CHUNK_SIZE, frame.length));
    await rxChar.writeValueWithoutResponse(chunk);
  }

  // Wait for response
  return new Promise((resolve, reject) => {
    pendingResolve = resolve;
    pendingReject = reject;
    setTimeout(() => {
      if (pendingReject) {
        pendingReject(new Error('Response timeout'));
        pendingResolve = null;
        pendingReject = null;
      }
    }, 10000);
  });
}

// ============================================================
// Connection management
// ============================================================
async function toggleConnection() {
  if (bleDevice && bleDevice.gatt.connected) {
    bleDevice.gatt.disconnect();
    return;
  }
  await connect();
}

async function connect() {
  try {
    log('Requesting BLE device...');
    bleDevice = await navigator.bluetooth.requestDevice({
      filters: [{ namePrefix: 'Family-mruby-' }],
      optionalServices: [SERVICE_UUID]
    });

    bleDevice.addEventListener('gattserverdisconnected', onDisconnected);

    log('Connecting to GATT server...');
    const server = await bleDevice.gatt.connect();

    log('Getting service...');
    const service = await server.getPrimaryService(SERVICE_UUID);

    rxChar = await service.getCharacteristic(FS_RX_UUID);
    txChar = await service.getCharacteristic(FS_TX_UUID);

    await txChar.startNotifications();
    txChar.addEventListener('characteristicvaluechanged', onTxNotification);
    log('Notifications enabled', 'ok');

    setConnected(true);
    log('Connected', 'ok');

    await refreshDir();
    await refreshStatfs();

    // If user had a log subscription before disconnecting, restore it.
    if (logWantSubscribe) {
      try {
        await subscribeLogs();
      } catch (e) {
        log('Auto re-subscribe failed: ' + e.message, 'err');
      }
    }
  } catch (e) {
    log('Connection failed: ' + e.message, 'err');
    setConnected(false);
  }
}

function onDisconnected() {
  log('Disconnected', 'err');
  setConnected(false);
  rxChar = null;
  txChar = null;
  rxBuffer = new Uint8Array(0);
  pendingResolve = null;
  pendingReject = null;
  // Server clears its subscription on disconnect; we keep `logWantSubscribe`
  // so reconnect can restore the stream.
  logSubscribed = false;
  logLastSeq = -1;
  setLogStatus(logWantSubscribe ? 'disconnected (will resubscribe)' : 'idle');
  updateLogSubscribeButton();
  document.getElementById('storageInfo').textContent = '';
}

function setConnected(connected) {
  const badge = document.getElementById('statusBadge');
  const btn = document.getElementById('btnConnect');
  badge.textContent = connected ? 'Connected' : 'Disconnected';
  badge.className = 'status' + (connected ? ' connected' : '');
  btn.textContent = connected ? 'Disconnect' : 'Connect';
  document.getElementById('btnRefresh').disabled = !connected;
  document.getElementById('btnMkdir').disabled = !connected;
  document.getElementById('btnNewFile').disabled = !connected;
  document.querySelectorAll('[data-needs-device]').forEach(el => { el.disabled = !connected; });
  updateLogSubscribeButton();
}

// ============================================================
// File operations
// ============================================================
async function refreshDir() {
  try {
    const { meta } = await sendCommand(CMD_LS, { path: currentPath });
    if (!meta.ok) {
      log('ls failed: ' + (meta.err || 'unknown'), 'err');
      return;
    }
    renderBreadcrumb();
    renderFileList(meta.entries || []);
  } catch (e) {
    log('ls error: ' + e.message, 'err');
  }
}

async function navigateTo(path) {
  currentPath = path;
  await refreshDir();
}

async function downloadFile(name) {
  const filePath = currentPath === '/' ? '/' + name : currentPath + '/' + name;
  log('Downloading: ' + filePath);
  showProgress(0, 'Downloading...');

  try {
    const chunks = [];
    let offset = 0;
    let totalRead = 0;

    while (true) {
      const { meta, binData } = await sendCommand(CMD_GET, { path: filePath, off: offset });
      if (!meta.ok) {
        log('Download failed: ' + (meta.err || 'unknown'), 'err');
        hideProgress();
        return;
      }

      if (binData && binData.length > 0) {
        chunks.push(binData);
        totalRead += binData.length;
        offset += binData.length;
        showProgress(-1, 'Downloaded ' + formatSize(totalRead));
      }

      if (meta.eof) break;
    }

    // Combine chunks and trigger browser download
    const totalSize = chunks.reduce((s, c) => s + c.length, 0);
    const combined = new Uint8Array(totalSize);
    let pos = 0;
    for (const chunk of chunks) {
      combined.set(chunk, pos);
      pos += chunk.length;
    }

    const blob = new Blob([combined]);
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = name;
    a.click();
    URL.revokeObjectURL(url);

    log('Downloaded: ' + name + ' (' + formatSize(totalSize) + ')', 'ok');
  } catch (e) {
    log('Download error: ' + e.message, 'err');
  }
  hideProgress();
}

async function uploadFiles(fileList) {
  if (!rxChar) return;

  for (const file of fileList) {
    const remotePath = currentPath === '/' ? '/' + file.name : currentPath + '/' + file.name;
    log('Uploading: ' + file.name + ' (' + formatSize(file.size) + ')');
    showProgress(0, 'Uploading ' + file.name + '...');

    try {
      const data = new Uint8Array(await file.arrayBuffer());
      let offset = 0;

      while (offset <= data.length) {
        const chunk = data.slice(offset, offset + FILE_CHUNK_SIZE);
        const { meta } = await sendCommand(CMD_PUT, { path: remotePath, off: offset }, chunk);
        if (!meta.ok) {
          log('Upload failed: ' + (meta.err || 'unknown'), 'err');
          break;
        }
        offset += chunk.length;
        if (file.size > 0) {
          showProgress(Math.min(100, Math.round(offset / file.size * 100)),
                       'Uploading ' + file.name + '... ' + formatSize(offset) + ' / ' + formatSize(file.size));
        }
        if (chunk.length === 0) break;
      }

      log('Uploaded: ' + file.name, 'ok');
    } catch (e) {
      log('Upload error: ' + e.message, 'err');
    }
  }

  hideProgress();
  document.getElementById('fileInput').value = '';
  await refreshDir();
  await refreshStatfs();
}

// ============================================================
// Generic device file I/O (reusable for binary asset transfer)
// ============================================================
async function deviceReadFile(path, progressCb) {
  if (!rxChar || !txChar) throw new Error('Not connected');
  const chunks = [];
  let offset = 0;
  let totalRead = 0;
  while (true) {
    const { meta, binData } = await sendCommand(CMD_GET, { path, off: offset });
    if (!meta.ok) throw new Error(meta.err || 'read failed');
    if (binData && binData.length > 0) {
      chunks.push(binData);
      totalRead += binData.length;
      offset += binData.length;
      if (progressCb) progressCb(totalRead);
    }
    if (meta.eof) break;
  }
  const out = new Uint8Array(totalRead);
  let p = 0;
  for (const c of chunks) { out.set(c, p); p += c.length; }
  return out;
}

async function deviceWriteFile(path, data, progressCb) {
  if (!rxChar || !txChar) throw new Error('Not connected');
  const arr = data instanceof Uint8Array ? data : new Uint8Array(data);
  let offset = 0;
  while (offset <= arr.length) {
    const chunk = arr.slice(offset, offset + FILE_CHUNK_SIZE);
    const { meta } = await sendCommand(CMD_PUT, { path, off: offset }, chunk);
    if (!meta.ok) throw new Error(meta.err || 'write failed');
    offset += chunk.length;
    if (progressCb) progressCb(offset, arr.length);
    if (chunk.length === 0) break;
  }
}

// ============================================================
// Text editor
// ============================================================
let editorState = { name: null, original: '', dirty: false };

async function openEditor(name) {
  const filePath = currentPath === '/' ? '/' + name : currentPath + '/' + name;
  log('Opening: ' + filePath);
  showProgress(0, 'Loading...');

  try {
    const chunks = [];
    let offset = 0;
    while (true) {
      const { meta, binData } = await sendCommand(CMD_GET, { path: filePath, off: offset });
      if (!meta.ok) {
        log('Open failed: ' + (meta.err || 'unknown'), 'err');
        hideProgress();
        return;
      }
      if (binData && binData.length > 0) {
        chunks.push(binData);
        offset += binData.length;
      }
      if (meta.eof) break;
    }

    const totalSize = chunks.reduce((s, c) => s + c.length, 0);
    const combined = new Uint8Array(totalSize);
    let pos = 0;
    for (const chunk of chunks) {
      combined.set(chunk, pos);
      pos += chunk.length;
    }

    let text;
    try {
      text = new TextDecoder('utf-8', { fatal: true }).decode(combined);
    } catch (e) {
      hideProgress();
      if (confirm(name + ' may be a binary file (not valid UTF-8). Download instead?')) {
        return downloadFile(name);
      }
      return;
    }

    editorState = { name, original: text, dirty: false };
    document.getElementById('editorPath').textContent = filePath;
    document.getElementById('editorDirty').textContent = '';
    document.getElementById('editorMessage').textContent = '';
    const ta = document.getElementById('editorTextarea');
    const code = document.getElementById('editorHighlight');
    const lang = prismLangForName(name);
    code.className = 'language-' + lang;
    code.dataset.lang = lang;
    ta.value = text;
    highlightEditorContent();
    syncEditorScroll();
    updateEditorStats();
    document.getElementById('editorModal').classList.add('open');
    ta.scrollTop = 0;
    ta.scrollLeft = 0;
    ta.setSelectionRange(0, 0);
    setTimeout(() => { ta.focus(); ta.setSelectionRange(0, 0); syncEditorScroll(); }, 0);
    log('Loaded: ' + name + ' (' + formatSize(totalSize) + ')', 'ok');
  } catch (e) {
    log('Open error: ' + e.message, 'err');
  }
  hideProgress();
}

async function saveEditor() {
  if (!editorState.name) return;
  const ta = document.getElementById('editorTextarea');
  const text = ta.value;
  const filePath = currentPath === '/' ? '/' + editorState.name : currentPath + '/' + editorState.name;

  const msgEl = document.getElementById('editorMessage');
  msgEl.textContent = 'Saving...';
  msgEl.className = '';

  try {
    const data = new TextEncoder().encode(text);
    await putFile(filePath, data);
    editorState.original = text;
    editorState.dirty = false;
    document.getElementById('editorDirty').textContent = '';
    msgEl.textContent = 'Saved (' + formatSize(data.length) + ')';
    log('Saved: ' + editorState.name, 'ok');
    await refreshDir();
    await refreshStatfs();
  } catch (e) {
    msgEl.textContent = 'Save failed: ' + e.message;
    msgEl.className = 'err';
    log('Save error: ' + e.message, 'err');
  }
}

function closeEditor() {
  if (editorState.dirty) {
    if (!confirm('Discard unsaved changes?')) return;
  }
  document.getElementById('editorModal').classList.remove('open');
  document.getElementById('editorTextarea').value = '';
  const code = document.getElementById('editorHighlight');
  code.textContent = '';
  code.className = 'language-none';
  code.dataset.lang = 'none';
  editorState = { name: null, original: '', dirty: false };
}

function updateEditorStats() {
  const ta = document.getElementById('editorTextarea');
  const text = ta.value;
  const lines = text.split('\n').length;
  const bytes = new TextEncoder().encode(text).length;
  document.getElementById('editorStats').textContent =
    lines + ' lines, ' + text.length + ' chars, ' + formatSize(bytes);
}

document.addEventListener('DOMContentLoaded', () => {
  const ta = document.getElementById('editorTextarea');
  let refreshRaf = null;
  ta.addEventListener('input', () => {
    // Synchronous: flip the dirty badge so it reacts on the very keystroke.
    if (!editorState.dirty) {
      editorState.dirty = true;
      document.getElementById('editorDirty').textContent = 'modified';
    }
    // Coalesce: one Prism highlight per animation frame at most.
    // The overlay <code> keeps its previous highlighted HTML until the
    // frame callback runs (~16ms), so we never flash plain text.
    if (refreshRaf !== null) return;
    refreshRaf = requestAnimationFrame(() => {
      refreshRaf = null;
      const stillDirty = ta.value !== editorState.original;
      if (stillDirty !== editorState.dirty) {
        editorState.dirty = stillDirty;
        document.getElementById('editorDirty').textContent = stillDirty ? 'modified' : '';
      }
      updateEditorStats();
      highlightEditorContent();
      syncEditorScroll();
    });
  });

  ta.addEventListener('scroll', syncEditorScroll, { passive: true });

  window.addEventListener('resize', () => {
    if (document.getElementById('editorModal').classList.contains('open')) {
      syncEditorScroll();
    }
  });

  document.addEventListener('keydown', (e) => {
    const modalOpen = document.getElementById('editorModal').classList.contains('open');
    if (!modalOpen) return;
    if ((e.ctrlKey || e.metaKey) && e.key === 's') {
      e.preventDefault();
      saveEditor();
    } else if (e.key === 'Escape') {
      e.preventDefault();
      closeEditor();
    }
  });
});

async function deleteFile(name, isDir) {
  const filePath = currentPath === '/' ? '/' + name : currentPath + '/' + name;
  if (!confirm('Delete ' + (isDir ? 'directory' : 'file') + ': ' + name + '?')) return;

  try {
    const { meta } = await sendCommand(CMD_RM, { path: filePath });
    if (!meta.ok) {
      log('Delete failed: ' + (meta.err || 'unknown'), 'err');
      return;
    }
    log('Deleted: ' + name, 'ok');
    await refreshDir();
    await refreshStatfs();
  } catch (e) {
    log('Delete error: ' + e.message, 'err');
  }
}

async function createDir() {
  const name = prompt('New folder name:');
  if (!name) return;

  const dirPath = currentPath === '/' ? '/' + name : currentPath + '/' + name;

  try {
    const { meta } = await sendCommand(CMD_MKDIR, { path: dirPath });
    if (!meta.ok) {
      log('mkdir failed: ' + (meta.err || 'unknown'), 'err');
      return;
    }
    log('Created folder: ' + name, 'ok');
    await refreshDir();
  } catch (e) {
    log('mkdir error: ' + e.message, 'err');
  }
}

async function createFile() {
  const name = prompt('New file name:');
  if (!name) return;

  const filePath = currentPath === '/' ? '/' + name : currentPath + '/' + name;

  try {
    await putFile(filePath, new Uint8Array(0));
    log('Created file: ' + name, 'ok');
    await refreshDir();
    await refreshStatfs();
  } catch (e) {
    log('create file error: ' + e.message, 'err');
  }
}

async function renameEntry(name, isDir) {
  const newName = prompt('New name:', name);
  if (newName === null) return;
  const trimmed = newName.trim();
  if (trimmed === '' || trimmed === name) return;
  if (trimmed.indexOf('/') !== -1) {
    log('Rename: name must not contain "/"', 'err');
    return;
  }

  const from = currentPath === '/' ? '/' + name : currentPath + '/' + name;
  const to = currentPath === '/' ? '/' + trimmed : currentPath + '/' + trimmed;

  try {
    const { meta } = await sendCommand(CMD_RENAME, { from: from, to: to });
    if (!meta.ok) {
      log('Rename failed: ' + (meta.err || 'unknown'), 'err');
      return;
    }
    log('Renamed: ' + name + ' -> ' + trimmed, 'ok');
    await refreshDir();
  } catch (e) {
    log('Rename error: ' + e.message, 'err');
  }
}

async function refreshStatfs() {
  try {
    const { meta } = await sendCommand(CMD_STATFS, { path: '/' });
    if (!meta.ok) return;
    const free = formatSize(meta.free);
    const total = formatSize(meta.total);
    document.getElementById('storageInfo').textContent = 'Free: ' + free + ' / ' + total;
  } catch (e) {
    // STATFS is informational; never break the UI on failure
    log('statfs error: ' + e.message, 'err');
  }
}

async function putFile(remotePath, data) {
  let offset = 0;
  while (offset <= data.length) {
    const chunk = data.slice(offset, offset + FILE_CHUNK_SIZE);
    const { meta } = await sendCommand(CMD_PUT, { path: remotePath, off: offset }, chunk);
    if (!meta.ok) {
      throw new Error(meta.err || 'put failed');
    }
    offset += chunk.length;
    if (chunk.length === 0) break;
  }
}

// ============================================================
// UI rendering
// ============================================================
function renderBreadcrumb() {
  const el = document.getElementById('breadcrumb');
  const parts = currentPath.split('/').filter(p => p);

  let html = '<span onclick="navigateTo(\'/\')">/</span>';
  let path = '';
  for (let i = 0; i < parts.length; i++) {
    const part = parts[i];
    path += '/' + part;
    const p = path; // closure capture
    // Skip separator between root '/' and first part (the root span already
    // ends with '/'). Add a separator between subsequent parts.
    if (i > 0) {
      html += '<span class="sep">/</span>';
    }
    html += '<span onclick="navigateTo(\'' + p + '\')">' + part + '</span>';
  }
  el.innerHTML = html;
}

function renderFileList(entries) {
  const el = document.getElementById('fileList');

  if (entries.length === 0) {
    el.innerHTML = '<div class="empty-msg">Empty directory</div>';
    return;
  }

  // Sort: directories first, then files, alphabetical
  entries.sort((a, b) => {
    if (a.t !== b.t) return a.t === 'd' ? -1 : 1;
    return a.n.localeCompare(b.n);
  });

  let html = '';
  for (const entry of entries) {
    const isDir = entry.t === 'd';
    const editable = !isDir && isTextFile(entry.n, entry.s);
    const ext = isDir ? '' : fileExtension(entry.n);
    const isBmp  = ext === 'bmp';
    const isJson = ext === 'json';
    const icon = isDir ? '&#x1F4C1;' : (editable ? '&#x1F4DD;' : '&#x1F4C4;');
    const size = isDir ? '' : formatSize(entry.s);
    const name = escapeHtml(entry.n);
    const clickAction = isDir
      ? 'navigateTo(\'' + escapeAttr(currentPath === '/' ? '/' + entry.n : currentPath + '/' + entry.n) + '\')'
      : (editable
          ? 'openEditor(\'' + escapeAttr(entry.n) + '\')'
          : 'downloadFile(\'' + escapeAttr(entry.n) + '\')');

    html += '<div class="file-row">';
    html += '<div class="icon">' + icon + '</div>';
    html += '<div class="name" onclick="' + clickAction + '">' + name + '</div>';
    html += '<div class="size">' + size + '</div>';
    html += '<div class="actions">';
    if (!isDir) {
      if (editable) {
        html += '<button onclick="openEditor(\'' + escapeAttr(entry.n) + '\')">Edit</button>';
      }
      if (isBmp) {
        html += '<button onclick="openInSpriteEditor(\'' + escapeAttr(entry.n) + '\')">Spr</button>';
      }
      if (isJson) {
        html += '<button onclick="openInMapEditor(\'' + escapeAttr(entry.n) + '\')">Map</button>';
      }
      html += '<button onclick="downloadFile(\'' + escapeAttr(entry.n) + '\')">DL</button>';
    }
    html += '<button onclick="renameEntry(\'' + escapeAttr(entry.n) + '\',' + isDir + ')">Rn</button>';
    html += '<button class="danger" onclick="deleteFile(\'' + escapeAttr(entry.n) + '\',' + isDir + ')">Del</button>';
    html += '</div>';
    html += '</div>';
  }
  el.innerHTML = html;
}

// ============================================================
// File Manager -> editor jumps (BMP -> Sprite Editor, JSON -> Map Editor)
// Loads the chosen device file directly into the target editor and switches
// to its tab. The target load path itself is responsible for validating the
// file shape — we just surface the error via toast/log if it rejects.
// ============================================================
async function openInSpriteEditor(name) {
  const filePath = currentPath === '/' ? '/' + name : currentPath + '/' + name;
  if (typeof sprite !== 'undefined' && sprite.dirty &&
      !confirm('Discard unsaved sprite changes to open ' + name + '?')) return;
  switchTab('sprite');
  try {
    showProgress(-1, 'Loading ' + filePath + '...');
    const data = await deviceReadFile(filePath,
      n => showProgress(-1, 'Loading ' + filePath + '... ' + formatSize(n)));
    sprLoadBmpFromBuffer(
      data.buffer.slice(data.byteOffset, data.byteOffset + data.byteLength),
      filePath);
    log('Sprite: opened ' + filePath, 'ok');
    toast('Sprite opened: ' + filePath, 'ok');
  } catch (e) {
    log('Sprite open failed: ' + e.message, 'err');
    toast('Not a valid sprite BMP: ' + e.message, 'err');
  }
  hideProgress();
}

async function openInMapEditor(name) {
  const filePath = currentPath === '/' ? '/' + name : currentPath + '/' + name;
  if (typeof mapEd !== 'undefined' && mapEd.dirty &&
      !confirm('Discard unsaved map changes to open ' + name + '?')) return;
  switchTab('map');
  try {
    showProgress(-1, 'Loading ' + filePath + '...');
    const data = await deviceReadFile(filePath);
    let obj;
    try {
      obj = JSON.parse(new TextDecoder().decode(data));
    } catch (e) {
      throw new Error('JSON parse error: ' + e.message);
    }
    mapDeserialize(obj, filePath);
    log('Map: opened ' + filePath, 'ok');
    toast('Map opened: ' + filePath, 'ok');
    // Auto-fetch the referenced tilesheet so the map renders with art.
    if (mapEd.tilesheetPath) {
      try {
        showProgress(-1, 'Loading tilesheet ' + mapEd.tilesheetPath + '...');
        const sheetData = await deviceReadFile(mapEd.tilesheetPath);
        mapLoadTilesheetFromBuffer(
          sheetData.buffer.slice(sheetData.byteOffset, sheetData.byteOffset + sheetData.byteLength),
          mapEd.tilesheetPath);
        log('Map: tilesheet auto-loaded ' + mapEd.tilesheetPath, 'ok');
      } catch (e) {
        log('Map: tilesheet auto-load failed (' + e.message +
            '). Use "Load Tilesheet from Device..." manually.', 'err');
        toast('Tilesheet auto-load failed: ' + e.message, 'err');
      }
    }
  } catch (e) {
    log('Map open failed: ' + e.message, 'err');
    toast('Not a valid map JSON: ' + e.message, 'err');
  }
  hideProgress();
}

// ============================================================
// Drag & drop
// ============================================================
const uploadArea = document.getElementById('uploadArea');
uploadArea.addEventListener('dragover', (e) => { e.preventDefault(); uploadArea.classList.add('drag'); });
uploadArea.addEventListener('dragleave', () => uploadArea.classList.remove('drag'));
uploadArea.addEventListener('drop', (e) => {
  e.preventDefault();
  uploadArea.classList.remove('drag');
  if (e.dataTransfer.files.length > 0) {
    uploadFiles(e.dataTransfer.files);
  }
});

// ============================================================
// Device log streaming (BLE LOG_SUBSCRIBE/UNSUBSCRIBE/SET_LEVEL)
// ============================================================
const TAB_IDS = ['files', 'logs', 'sprite', 'map'];
const tabChangeListeners = {};

function registerTabChangeListener(name, cb) {
  tabChangeListeners[name] = cb;
}

function switchTab(name) {
  TAB_IDS.forEach(id => {
    const tabBtn = document.getElementById('tab' + id.charAt(0).toUpperCase() + id.slice(1));
    const pane   = document.getElementById(id + 'Pane');
    if (tabBtn) tabBtn.classList.toggle('active', id === name);
    if (pane)   pane.classList.toggle('active', id === name);
  });
  const cb = tabChangeListeners[name];
  if (cb) cb();
}

function setLogStatus(text) {
  const el = document.getElementById('logStatus');
  if (el) el.textContent = text;
}

function updateLogSubscribeButton() {
  const btn = document.getElementById('btnLogSubscribe');
  if (!btn) return;
  const connected = !!(rxChar && txChar);
  btn.disabled = !connected;
  btn.textContent = logSubscribed ? 'Unsubscribe' : 'Subscribe';
  btn.className = logSubscribed ? '' : 'primary';
}

async function toggleLogSubscribe() {
  if (logSubscribed) {
    logWantSubscribe = false;
    await unsubscribeLogs();
  } else {
    logWantSubscribe = true;
    await subscribeLogs();
  }
}

async function subscribeLogs() {
  const level = document.getElementById('logLevelSelect').value || 'I';
  setLogStatus('subscribing...');
  try {
    const { meta } = await sendCommand(CMD_LOG_SUBSCRIBE, {
      period: 200, max_lines: 20, level: level
    });
    if (!meta.ok) {
      setLogStatus('subscribe failed: ' + (meta.err || 'unknown'));
      return;
    }
    logSubscribed = true;
    logLastSeq = -1;
    setLogStatus('subscribed (level=' + (meta.level || level) +
                 ', period=' + (meta.period || 200) + 'ms)');
    updateLogSubscribeButton();
  } catch (e) {
    setLogStatus('subscribe error: ' + e.message);
  }
}

async function unsubscribeLogs() {
  setLogStatus('unsubscribing...');
  try {
    await sendCommand(CMD_LOG_UNSUBSCRIBE, {});
  } catch (e) {
    // Ignore — server clears state on disconnect anyway
  }
  logSubscribed = false;
  setLogStatus('idle');
  updateLogSubscribeButton();
}

async function onLogLevelChange() {
  const level = document.getElementById('logLevelSelect').value || 'I';
  if (!rxChar || !txChar) return;        // not connected yet
  if (!logSubscribed) return;            // change applies on next subscribe
  try {
    const { meta } = await sendCommand(CMD_LOG_SET_LEVEL, { level: level });
    if (meta.ok) {
      setLogStatus('subscribed (level=' + (meta.level || level) + ')');
    } else {
      setLogStatus('set_level failed: ' + (meta.err || 'unknown'));
    }
  } catch (e) {
    setLogStatus('set_level error: ' + e.message);
  }
}

function clearDeviceLog() {
  const box = document.getElementById('deviceLogBox');
  while (box.firstChild) box.removeChild(box.firstChild);
}

function applyLogFilter() {
  logFilterText = (document.getElementById('logFilterInput').value || '').toLowerCase();
  const box = document.getElementById('deviceLogBox');
  for (const child of box.children) {
    const txt = child.dataset.raw || child.textContent;
    child.style.display = (logFilterText === '' || txt.toLowerCase().indexOf(logFilterText) !== -1)
      ? '' : 'none';
  }
}

// Append log lines from a server push event. `meta.evt === 'log'`,
// `meta.bin` = number of bytes in `binData`. Lines are '\n' separated.
function handleLogEvent(meta, binData) {
  // Gap detection
  if (logLastSeq >= 0 && meta.seq !== (logLastSeq + 1) >>> 0) {
    appendDeviceLogLine('-- gap: seq jump ' + logLastSeq + ' -> ' + meta.seq + ' --',
                        'dropped', null);
  }
  logLastSeq = meta.seq >>> 0;

  if (meta.dropped) {
    appendDeviceLogLine('-- log buffer overrun: some lines dropped --',
                        'dropped', null);
  }

  if (!binData || binData.length === 0) return;

  const text = logTextDecoder.decode(binData);
  // Lines are '\n'-terminated; the trailing newline produces an empty
  // last element which we discard.
  const lines = text.split('\n');
  if (lines.length > 0 && lines[lines.length - 1] === '') lines.pop();

  const box = document.getElementById('deviceLogBox');
  const wasAtBottom = isLogAtBottom(box);
  for (const line of lines) {
    appendDeviceLogLine(line, parseLogLevelClass(line), box);
  }
  capDeviceLogLines(box);
  if (document.getElementById('logAutoScroll').checked && wasAtBottom) {
    box.scrollTop = box.scrollHeight;
  }
}

function parseLogLevelClass(line) {
  // fmrb_log_buffer_printf prefixes lines with '[<level>][<tag>] ...'
  if (line.length >= 3 && line[0] === '[') {
    const lv = line[1];
    if (lv === 'E' || lv === 'W' || lv === 'I' || lv === 'D') return 'lv-' + lv;
  }
  return 'lv-I';
}

function appendDeviceLogLine(text, cls, boxArg) {
  const box = boxArg || document.getElementById('deviceLogBox');
  const div = document.createElement('div');
  div.className = 'devlog-line' + (cls ? ' ' + cls : '');
  div.dataset.raw = text;
  div.textContent = text;
  if (logFilterText && text.toLowerCase().indexOf(logFilterText) === -1) {
    div.style.display = 'none';
  }
  box.appendChild(div);
}

function capDeviceLogLines(box) {
  const over = box.children.length - logLineCap;
  for (let i = 0; i < over; i++) {
    box.removeChild(box.firstChild);
  }
}

function isLogAtBottom(box) {
  return (box.scrollHeight - box.scrollTop - box.clientHeight) < 24;
}

// ============================================================
// Helpers
// ============================================================
function formatSize(bytes) {
  if (bytes < 1024) return bytes + ' B';
  if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(1) + ' KB';
  return (bytes / (1024 * 1024)).toFixed(1) + ' MB';
}

function escapeHtml(s) {
  return s.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');
}

function escapeAttr(s) {
  return s.replace(/\\/g, '\\\\').replace(/'/g, "\\'");
}

function fileExtension(name) {
  const idx = name.lastIndexOf('.');
  return idx > 0 ? name.slice(idx + 1).toLowerCase() : '';
}

function isTextFile(name, size) {
  if (size > EDITOR_MAX_SIZE) return false;
  return TEXT_EXTENSIONS.has(fileExtension(name));
}

function prismLangForName(name) {
  return PRISM_LANG_BY_EXT[fileExtension(name)] || 'none';
}

function highlightEditorContent() {
  const ta = document.getElementById('editorTextarea');
  const code = document.getElementById('editorHighlight');
  if (!ta || !code) return;
  const lang = code.dataset.lang || 'none';
  const text = ta.value;
  // Pad trailing newline so the last line stays aligned with the textarea.
  const padded = text.endsWith('\n') ? text + ' ' : text;
  const grammar = (window.Prism && Prism.languages) ? Prism.languages[lang] : null;
  if (lang === 'none' || !grammar) {
    code.textContent = padded;
    return;
  }
  code.innerHTML = Prism.highlight(padded, grammar, lang);
}

function syncEditorScroll() {
  const ta = document.getElementById('editorTextarea');
  const pre = document.querySelector('.editor-highlight');
  if (!ta || !pre) return;
  pre.scrollTop  = ta.scrollTop;
  pre.scrollLeft = ta.scrollLeft;
}

function log(msg, cls) {
  const box = document.getElementById('logBox');
  const line = document.createElement('div');
  line.className = 'log-line' + (cls ? ' ' + cls : '');
  const time = new Date().toLocaleTimeString();
  line.textContent = '[' + time + '] ' + msg;
  box.appendChild(line);
  box.scrollTop = box.scrollHeight;
}

// Transient on-screen status (the log box lives inside a collapsed <details>
// so save/load results would otherwise be invisible). `kind` is 'ok' | 'err' |
// 'info' (default); errors stay visible longer.
function toast(msg, kind) {
  let container = document.getElementById('toastContainer');
  if (!container) {
    container = document.createElement('div');
    container.id = 'toastContainer';
    container.className = 'toast-container';
    document.body.appendChild(container);
  }
  const el = document.createElement('div');
  el.className = 'toast ' + (kind || 'info');
  el.textContent = msg;
  const dismiss = () => {
    if (!el.parentNode) return;
    el.classList.remove('show');
    setTimeout(() => el.remove(), 300);
  };
  el.addEventListener('click', dismiss);
  container.appendChild(el);
  requestAnimationFrame(() => el.classList.add('show'));
  setTimeout(dismiss, kind === 'err' ? 5000 : 3000);
}

function showProgress(pct, text) {
  const el = document.getElementById('progress');
  el.style.display = 'block';
  if (pct >= 0) {
    document.getElementById('progressFill').style.width = pct + '%';
  }
  document.getElementById('progressText').textContent = text || '';
}

function hideProgress() {
  document.getElementById('progress').style.display = 'none';
  document.getElementById('progressFill').style.width = '0%';
}

// Check Web Bluetooth availability
if (!navigator.bluetooth) {
  log('Web Bluetooth is not available in this browser. Use Chrome/Edge on a supported platform.', 'err');
  document.getElementById('btnConnect').disabled = true;
}
