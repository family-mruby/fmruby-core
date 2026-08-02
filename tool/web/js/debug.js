// Remote debugger panel: ps / kill / spawn over the BLE debug service.
//
// Rides the same GATT connection as the file console (app.js owns connect and
// disconnect; dbgAttach / dbgDetach are called from there). The debug service
// is a second primary service on the device, with its own RX/TX pair.
//
// Wire format, from main/drivers/debug/fmrb_debug_transport_ble.c:
//   COBS([u16 BE body length][msgpack body][CRC32 BE]) + 0x00
// where the CRC covers the length prefix and the body. That differs from the
// file service, whose body carries no length prefix, so the framing below is
// its own rather than a call into app.js's buildPacket.
//
// The message layer is the one tool/debug/fmrb_dbg_client.py speaks:
//   request  [0, seq, cmd, payload]
//   response [1, seq, err, payload]   err != 0 means the command failed
//   event    [2, _, name, payload]    unsolicited, no seq to match

const DBG_SERVICE_UUID = '4652414d-4252-5942-4c45-000000000005';
const DBG_RX_UUID      = '4652414d-4252-5942-4c45-000000000006';  // browser writes
const DBG_TX_UUID      = '4652414d-4252-5942-4c45-000000000007';  // device notifies

const DBG_MSG_REQUEST  = 0;
const DBG_MSG_RESPONSE = 1;
const DBG_MSG_EVENT    = 2;

const DBG_TIMEOUT_MS   = 5000;
const DBG_WRITE_CHUNK  = 180;  // same conservative ATT payload the file path uses

let dbgRxChar = null;
let dbgTxChar = null;
let dbgRxBuffer = new Uint8Array(0);
let dbgSeq = 0;
const dbgPending = new Map();  // seq -> {resolve, reject, timer}

// ============================================================
// Framing
// ============================================================
function dbgBuildFrame(body) {
  // [u16 BE len][body][CRC32 BE], then COBS + delimiter.
  const plain = new Uint8Array(2 + body.length + 4);
  plain[0] = (body.length >> 8) & 0xFF;
  plain[1] = body.length & 0xFF;
  plain.set(body, 2);
  const crcVal = crc32(plain.subarray(0, 2 + body.length));
  const o = 2 + body.length;
  plain[o]     = (crcVal >>> 24) & 0xFF;
  plain[o + 1] = (crcVal >>> 16) & 0xFF;
  plain[o + 2] = (crcVal >>> 8)  & 0xFF;
  plain[o + 3] = crcVal & 0xFF;

  const encoded = cobsEncode(plain);
  const frame = new Uint8Array(encoded.length + 1);
  frame.set(encoded);
  frame[encoded.length] = 0x00;
  return frame;
}

function dbgParseFrame(encoded) {
  const plain = cobsDecode(encoded);
  if (plain.length < 6) throw new Error('frame too short');

  const got = ((plain[plain.length - 4] << 24) | (plain[plain.length - 3] << 16) |
               (plain[plain.length - 2] << 8)  |  plain[plain.length - 1]) >>> 0;
  const calc = crc32(plain.subarray(0, plain.length - 4));
  if (got !== calc) throw new Error('CRC mismatch');

  const bodyLen = (plain[0] << 8) | plain[1];
  if (bodyLen !== plain.length - 6) throw new Error('length mismatch');
  return msgpack.decode(plain.subarray(2, 2 + bodyLen));
}

// ============================================================
// Transport
// ============================================================
function dbgOnNotification(event) {
  const value = new Uint8Array(event.target.value.buffer);
  const combined = new Uint8Array(dbgRxBuffer.length + value.length);
  combined.set(dbgRxBuffer);
  combined.set(value, dbgRxBuffer.length);
  dbgRxBuffer = combined;

  // A single notification may carry more than one frame.
  for (;;) {
    const delim = dbgRxBuffer.indexOf(0x00);
    if (delim < 0) break;
    const frame = dbgRxBuffer.slice(0, delim);
    dbgRxBuffer = dbgRxBuffer.slice(delim + 1);
    if (frame.length === 0) continue;
    try {
      dbgDispatch(dbgParseFrame(frame));
    } catch (e) {
      dbgLog('dropped a frame: ' + e.message, 'err');
    }
  }
}

function dbgDispatch(msg) {
  if (!Array.isArray(msg) || msg.length < 3) return;
  if (msg[0] === DBG_MSG_RESPONSE) {
    const seq = msg[1];
    const err = msg[2];
    const payload = msg.length > 3 ? msg[3] : null;
    const waiter = dbgPending.get(seq);
    if (!waiter) return;  // late answer to something we already gave up on
    dbgPending.delete(seq);
    clearTimeout(waiter.timer);
    if (err !== 0) waiter.reject(new Error('device returned err=' + err));
    else waiter.resolve(payload);
  } else if (msg[0] === DBG_MSG_EVENT) {
    dbgLog('event ' + msg[2] + ': ' + JSON.stringify(msg.length > 3 ? msg[3] : {}));
  }
}

// The transport is swappable so the framing and request logic can be exercised
// without a device; see the fake used during development (not committed).
let dbgSendRaw = async function (frame) {
  if (!dbgRxChar) throw new Error('debug service not connected');
  for (let off = 0; off < frame.length; off += DBG_WRITE_CHUNK) {
    await dbgRxChar.writeValueWithoutResponse(
      frame.slice(off, Math.min(off + DBG_WRITE_CHUNK, frame.length)));
  }
};

async function dbgRequest(cmd, payload) {
  dbgSeq = (dbgSeq + 1) & 0xFFFF;
  const seq = dbgSeq;
  const body = msgpack.encode([DBG_MSG_REQUEST, seq, cmd, payload === undefined ? null : payload]);
  const frame = dbgBuildFrame(body);

  const answer = new Promise((resolve, reject) => {
    const timer = setTimeout(() => {
      dbgPending.delete(seq);
      reject(new Error("no response to '" + cmd + "' within " + DBG_TIMEOUT_MS + 'ms'));
    }, DBG_TIMEOUT_MS);
    dbgPending.set(seq, { resolve, reject, timer });
  });

  try {
    await dbgSendRaw(frame);
  } catch (e) {
    const waiter = dbgPending.get(seq);
    if (waiter) { clearTimeout(waiter.timer); dbgPending.delete(seq); }
    throw e;
  }
  return answer;
}

// ============================================================
// Connection hooks, called by app.js on the shared GATT connection
// ============================================================
async function dbgAttach(server) {
  try {
    const svc = await server.getPrimaryService(DBG_SERVICE_UUID);
    dbgRxChar = await svc.getCharacteristic(DBG_RX_UUID);
    dbgTxChar = await svc.getCharacteristic(DBG_TX_UUID);
    await dbgTxChar.startNotifications();
    dbgTxChar.addEventListener('characteristicvaluechanged', dbgOnNotification);
    dbgSetStatus('connected');
    dbgLog('debug service ready', 'ok');
  } catch (e) {
    // A firmware without the debug service should not break the file console.
    dbgRxChar = null;
    dbgTxChar = null;
    dbgSetStatus('unavailable');
    dbgLog('debug service unavailable: ' + e.message, 'err');
  }
  dbgUpdateButtons();
}

function dbgDetach() {
  dbgRxChar = null;
  dbgTxChar = null;
  dbgRxBuffer = new Uint8Array(0);
  for (const [, w] of dbgPending) {
    clearTimeout(w.timer);
    w.reject(new Error('disconnected'));
  }
  dbgPending.clear();
  dbgSetStatus('disconnected');
  dbgUpdateButtons();
}

// ============================================================
// UI
// ============================================================
function dbgSetStatus(text) {
  const el = document.getElementById('dbgStatus');
  if (el) el.textContent = text;
}

function dbgUpdateButtons() {
  const on = !!(dbgRxChar && dbgTxChar);
  document.querySelectorAll('[data-needs-debug]').forEach(el => { el.disabled = !on; });
}

function dbgLog(text, cls) {
  const out = document.getElementById('dbgOutput');
  if (!out) return;
  const line = document.createElement('div');
  line.className = 'dbg-line' + (cls ? ' ' + cls : '');
  const t = new Date().toTimeString().slice(0, 8);
  line.textContent = '[' + t + '] ' + text;
  out.appendChild(line);
  out.scrollTop = out.scrollHeight;
}

// Process state names, from fmrb_proc_state_t (fmrb_app.h).
const DBG_STATES = ['FREE', 'INIT', 'RUNNING', 'SUSPENDED', 'STOPPING'];
// VM types, from fmrb_vm_type_t (fmrb_app.h). "native" is index 3, and a
// Spinel kernel or desktop reports as native because it is compiled C.
const DBG_VMS = ['mruby', 'lua', 'basic', 'native', 'micropython'];

function dbgStateName(v) { return DBG_STATES[v] !== undefined ? DBG_STATES[v] : String(v); }
function dbgVmName(v)    { return DBG_VMS[v]    !== undefined ? DBG_VMS[v]    : String(v); }

async function dbgPs() {
  const tbody = document.getElementById('dbgPsBody');
  try {
    const res = await dbgRequest('ps');
    const apps = (res && res.apps) || [];
    tbody.innerHTML = '';
    for (const a of apps) {
      const tr = document.createElement('tr');
      const cells = [a.pid, a.name, dbgVmName(a.vm), dbgStateName(a.state)];
      for (const c of cells) {
        const td = document.createElement('td');
        td.textContent = c;
        tr.appendChild(td);
      }
      const td = document.createElement('td');
      // pid 0 is the kernel and pid 2 the desktop; killing either takes the
      // system down, so the button is only offered for the rest.
      if (a.pid > 2) {
        const btn = document.createElement('button');
        btn.textContent = 'kill';
        btn.className = 'danger';
        btn.onclick = () => dbgKill(a.pid);
        td.appendChild(btn);
      }
      tr.appendChild(td);
      tbody.appendChild(tr);
    }
    dbgLog('ps: ' + apps.length + ' task(s)', 'ok');
  } catch (e) {
    dbgLog('ps failed: ' + e.message, 'err');
  }
}

async function dbgKill(pid) {
  try {
    await dbgRequest('kill', { pid: pid });
    dbgLog('kill pid=' + pid + ' ok', 'ok');
  } catch (e) {
    dbgLog('kill pid=' + pid + ' failed: ' + e.message, 'err');
  }
  await dbgPs();
}

async function dbgSpawn() {
  const input = document.getElementById('dbgSpawnPath');
  const path = input.value.trim();
  if (!path) { dbgLog('spawn: enter a path first', 'err'); return; }
  try {
    const res = await dbgRequest('spawn', { path: path });
    dbgLog('spawn ' + path + ' -> pid=' + (res && res.pid), 'ok');
  } catch (e) {
    dbgLog('spawn failed: ' + e.message, 'err');
  }
  await dbgPs();
}

// Raw escape hatch: any command debugd grows can be driven from here without
// waiting for a control to be added. Payload is JSON, empty means nil.
async function dbgRaw() {
  const cmd = document.getElementById('dbgRawCmd').value.trim();
  const rawPayload = document.getElementById('dbgRawPayload').value.trim();
  if (!cmd) { dbgLog('raw: enter a command name', 'err'); return; }
  let payload = null;
  if (rawPayload) {
    try {
      payload = JSON.parse(rawPayload);
    } catch (e) {
      dbgLog('raw: payload is not valid JSON: ' + e.message, 'err');
      return;
    }
  }
  try {
    const res = await dbgRequest(cmd, payload);
    dbgLog(cmd + ' -> ' + JSON.stringify(res), 'ok');
  } catch (e) {
    dbgLog(cmd + ' failed: ' + e.message, 'err');
  }
}

function dbgClear() {
  const out = document.getElementById('dbgOutput');
  if (out) out.innerHTML = '';
}
