// ============================================================
// Map editor — 2D tile map referencing a sprite BMP, with per-tile events.
// Output is JSON consumed by picoruby on device.
// ============================================================

const MAP_DEFAULT_DIR  = '/maps';
const MAP_FORMAT_NAME  = 'fmrb_map';
const MAP_FORMAT_VER   = 1;
const MAP_PAL_ZOOM     = 2;     // tile palette pixel scale
const MAP_CANVAS_ZOOM  = 1.5;   // map canvas pixel scale
const MAP_EMPTY        = -1;    // empty cell sentinel in layer data

const mapEd = {
  width: 32, height: 24,
  tileSize: 16,
  tilesheetPath: null,
  tilesheetCols: 0,
  tilesheetRows: 0,
  tilesheetPixels: null,
  tilesheetCanvas: null,
  layers: [{ name: 'ground', data: [] }],
  events: [],
  path: null,
  dirty: false,
  selectedTile: 0,
  mode: 'tile',
  selectedEvent: null,
  painting: 0,                  // 0=idle, 1=paint, 2=erase
};

function mapAllocLayer(w, h, fill) {
  const rows = new Array(h);
  for (let y = 0; y < h; y++) {
    const row = new Array(w);
    for (let x = 0; x < w; x++) row[x] = fill;
    rows[y] = row;
  }
  return rows;
}

function mapTileCount() { return mapEd.tilesheetCols * mapEd.tilesheetRows; }

function mapNew() {
  if (mapEd.dirty && !confirm('Discard unsaved map changes?')) return;
  const w = Math.max(1, Math.min(256, parseInt(document.getElementById('mapWidth').value, 10)  || 32));
  const h = Math.max(1, Math.min(256, parseInt(document.getElementById('mapHeight').value, 10) || 24));
  mapEd.width  = w;
  mapEd.height = h;
  mapEd.layers = [{ name: 'ground', data: mapAllocLayer(w, h, MAP_EMPTY) }];
  mapEd.events = [];
  mapEd.path = null;
  mapEd.dirty = false;
  mapEd.selectedEvent = null;
  mapResizeCanvas();
  mapRedraw();
  mapHideEventInspector();
  mapUpdateToolbar();
}

function mapApplySize() {
  const w = Math.max(1, Math.min(256, parseInt(document.getElementById('mapWidth').value, 10)  || mapEd.width));
  const h = Math.max(1, Math.min(256, parseInt(document.getElementById('mapHeight').value, 10) || mapEd.height));
  if (w === mapEd.width && h === mapEd.height) return;
  for (const layer of mapEd.layers) {
    const old = layer.data;
    const next = mapAllocLayer(w, h, MAP_EMPTY);
    for (let y = 0; y < Math.min(h, old.length); y++) {
      for (let x = 0; x < Math.min(w, old[y].length); x++) {
        next[y][x] = old[y][x];
      }
    }
    layer.data = next;
  }
  // Drop events outside the new bounds
  mapEd.events = mapEd.events.filter(e => e.x < w && e.y < h);
  mapEd.width = w; mapEd.height = h;
  mapMarkDirty();
  mapResizeCanvas();
  mapRedraw();
  mapHideEventInspector();
  mapUpdateToolbar();
}

function mapMarkDirty() {
  mapEd.dirty = true;
  mapUpdateToolbar();
}

function mapSetMode(mode) {
  mapEd.mode = mode;
  if (mode !== 'event') mapHideEventInspector();
}

function mapUpdateToolbar() {
  document.getElementById('mapSheetInfo').textContent =
    mapEd.tilesheetPath
      ? (mapEd.tilesheetPath + '  (' + mapEd.tilesheetCols + 'x' + mapEd.tilesheetRows + ' tiles)')
      : '(none)';
  document.getElementById('mapSizeInfo').textContent =
    mapEd.width + ' x ' + mapEd.height + ' tiles';
  document.getElementById('mapPathInfo').textContent =
    (mapEd.path || '(unsaved)') + (mapEd.dirty ? ' *' : '');
  document.getElementById('mapWidth').value  = mapEd.width;
  document.getElementById('mapHeight').value = mapEd.height;
}

// -------- tilesheet --------

function mapLoadTilesheetFromBuffer(arrayBuffer, originPath) {
  const decoded = decodeBmp332(arrayBuffer);
  if (decoded.width  % mapEd.tileSize !== 0)
    throw new Error('Tilesheet width not a multiple of ' + mapEd.tileSize + ': ' + decoded.width);
  if (decoded.height % mapEd.tileSize !== 0)
    throw new Error('Tilesheet height not a multiple of ' + mapEd.tileSize + ': ' + decoded.height);

  mapEd.tilesheetPath   = originPath || mapEd.tilesheetPath;
  mapEd.tilesheetCols   = decoded.width  / mapEd.tileSize;
  mapEd.tilesheetRows   = decoded.height / mapEd.tileSize;
  mapEd.tilesheetPixels = decoded.pixels;

  // Pre-render the sheet to an offscreen 1:1 canvas so map drawing can
  // slice from it with drawImage().
  const off = document.createElement('canvas');
  off.width  = decoded.width;
  off.height = decoded.height;
  const offCtx = off.getContext('2d');
  const img = offCtx.createImageData(decoded.width, decoded.height);
  for (let i = 0; i < decoded.pixels.length; i++) {
    const c = decoded.pixels[i];
    img.data[i * 4 + 0] = RGB332_RGB[c * 3 + 0];
    img.data[i * 4 + 1] = RGB332_RGB[c * 3 + 1];
    img.data[i * 4 + 2] = RGB332_RGB[c * 3 + 2];
    img.data[i * 4 + 3] = 255;
  }
  offCtx.putImageData(img, 0, 0);
  mapEd.tilesheetCanvas = off;

  // Clamp selected tile to new sheet range
  if (mapEd.selectedTile >= mapTileCount()) mapEd.selectedTile = 0;

  mapResizeCanvas();
  mapRedraw();
  mapUpdateToolbar();
}

async function mapLoadTilesheetDevice() {
  const def = mapEd.tilesheetPath || '/usr/share/sprites/';
  const path = prompt('Load tilesheet BMP from device path:', def);
  if (!path) return;
  try {
    showProgress(-1, 'Loading ' + path + '...');
    const data = await deviceReadFile(path, n => showProgress(-1, 'Loading ' + path + '... ' + formatSize(n)));
    mapLoadTilesheetFromBuffer(data.buffer.slice(data.byteOffset, data.byteOffset + data.byteLength), path);
    log('Map: tilesheet loaded ' + path, 'ok');
    toast('Tilesheet loaded ' + path, 'ok');
  } catch (e) {
    log('Map tilesheet load failed: ' + e.message, 'err');
    toast('Tilesheet load failed: ' + e.message, 'err');
  }
  hideProgress();
}

function mapOnLocalTilesheet(fileList) {
  if (!fileList || !fileList.length) return;
  const file = fileList[0];
  file.arrayBuffer().then(buf => {
    try {
      // Local sheet has no device path — keep existing tilesheetPath if any
      mapLoadTilesheetFromBuffer(buf, mapEd.tilesheetPath);
      log('Map: tilesheet loaded from local ' + file.name, 'ok');
      toast('Tilesheet loaded: ' + file.name, 'ok');
    } catch (e) {
      log('Map tilesheet load failed: ' + e.message, 'err');
      toast('Tilesheet load failed: ' + e.message, 'err');
    }
  }).catch(e => {
    log('Map tilesheet read error: ' + e.message, 'err');
    toast('Tilesheet read error: ' + e.message, 'err');
  });
  document.getElementById('mapSheetInput').value = '';
}

// -------- canvas sizing & drawing --------

function mapResizeCanvas() {
  const ts = mapEd.tileSize;
  const map = document.getElementById('mapCanvas');
  map.width  = Math.round(mapEd.width  * ts * MAP_CANVAS_ZOOM);
  map.height = Math.round(mapEd.height * ts * MAP_CANVAS_ZOOM);

  const pal = document.getElementById('mapPaletteCanvas');
  if (mapEd.tilesheetCanvas) {
    pal.width  = mapEd.tilesheetCols * ts * MAP_PAL_ZOOM;
    pal.height = mapEd.tilesheetRows * ts * MAP_PAL_ZOOM;
  } else {
    pal.width = 64; pal.height = 64;
  }
}

function mapRedraw() {
  mapDrawPalette();
  mapDrawMap();
}

function mapDrawPalette() {
  const canvas = document.getElementById('mapPaletteCanvas');
  const ctx = canvas.getContext('2d');
  ctx.imageSmoothingEnabled = false;
  ctx.fillStyle = '#000';
  ctx.fillRect(0, 0, canvas.width, canvas.height);

  if (!mapEd.tilesheetCanvas) {
    ctx.fillStyle = '#888';
    ctx.font = '11px sans-serif';
    ctx.fillText('No tilesheet', 8, 32);
    return;
  }
  ctx.drawImage(mapEd.tilesheetCanvas, 0, 0, canvas.width, canvas.height);

  // Tile grid
  const ts = mapEd.tileSize * MAP_PAL_ZOOM;
  ctx.strokeStyle = 'rgba(108, 0, 0, 0.5)';
  ctx.lineWidth = 1;
  for (let c = 0; c <= mapEd.tilesheetCols; c++) {
    const x = c * ts + 0.5;
    ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, canvas.height); ctx.stroke();
  }
  for (let r = 0; r <= mapEd.tilesheetRows; r++) {
    const y = r * ts + 0.5;
    ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(canvas.width, y); ctx.stroke();
  }

  // Selection highlight
  const sCol = mapEd.selectedTile % mapEd.tilesheetCols;
  const sRow = (mapEd.selectedTile / mapEd.tilesheetCols) | 0;
  ctx.strokeStyle = '#fcb4aa';
  ctx.lineWidth = 2;
  ctx.strokeRect(sCol * ts + 1, sRow * ts + 1, ts - 2, ts - 2);
}

function mapDrawMap() {
  const canvas = document.getElementById('mapCanvas');
  const ctx = canvas.getContext('2d');
  ctx.imageSmoothingEnabled = false;
  ctx.fillStyle = '#0d0508';
  ctx.fillRect(0, 0, canvas.width, canvas.height);

  const ts  = mapEd.tileSize;
  const dts = ts * MAP_CANVAS_ZOOM;

  if (mapEd.tilesheetCanvas) {
    for (const layer of mapEd.layers) {
      for (let y = 0; y < mapEd.height; y++) {
        const row = layer.data[y];
        if (!row) continue;
        for (let x = 0; x < mapEd.width; x++) {
          const idx = row[x];
          if (idx === undefined || idx === null || idx < 0) continue;
          if (idx >= mapTileCount()) continue;
          const sx = (idx % mapEd.tilesheetCols) * ts;
          const sy = ((idx / mapEd.tilesheetCols) | 0) * ts;
          ctx.drawImage(mapEd.tilesheetCanvas,
                        sx, sy, ts, ts,
                        x * dts, y * dts, dts, dts);
        }
      }
    }
  } else {
    // Show cells that contain a non-empty tile index as a placeholder square
    ctx.fillStyle = '#3a0e1a';
    for (const layer of mapEd.layers) {
      for (let y = 0; y < mapEd.height; y++) {
        const row = layer.data[y];
        if (!row) continue;
        for (let x = 0; x < mapEd.width; x++) {
          if (row[x] >= 0) ctx.fillRect(x * dts, y * dts, dts, dts);
        }
      }
    }
  }

  // Map grid
  ctx.strokeStyle = 'rgba(108, 0, 0, 0.3)';
  ctx.lineWidth = 1;
  for (let x = 0; x <= mapEd.width; x++) {
    const px = x * dts + 0.5;
    ctx.beginPath(); ctx.moveTo(px, 0); ctx.lineTo(px, canvas.height); ctx.stroke();
  }
  for (let y = 0; y <= mapEd.height; y++) {
    const py = y * dts + 0.5;
    ctx.beginPath(); ctx.moveTo(0, py); ctx.lineTo(canvas.width, py); ctx.stroke();
  }

  // Event markers
  ctx.lineWidth = 2;
  for (let i = 0; i < mapEd.events.length; i++) {
    const ev = mapEd.events[i];
    const cx = ev.x * dts + dts / 2;
    const cy = ev.y * dts + dts / 2;
    const r  = Math.max(4, dts / 3);
    ctx.fillStyle = i === mapEd.selectedEvent ? '#fff' : 'rgba(216, 36, 85, 0.9)';
    ctx.strokeStyle = '#000';
    ctx.beginPath();
    ctx.moveTo(cx, cy - r);
    ctx.lineTo(cx + r, cy);
    ctx.lineTo(cx, cy + r);
    ctx.lineTo(cx - r, cy);
    ctx.closePath();
    ctx.fill();
    ctx.stroke();
    ctx.fillStyle = '#000';
    ctx.font = 'bold ' + Math.max(9, Math.floor(dts / 2.5)) + 'px sans-serif';
    ctx.textAlign = 'center'; ctx.textBaseline = 'middle';
    ctx.fillText(String(ev.id), cx, cy + 1);
  }
}

// -------- interaction --------

function mapPaletteMouseDown(e) {
  if (!mapEd.tilesheetCanvas) return;
  const r = e.currentTarget.getBoundingClientRect();
  const ts = mapEd.tileSize * MAP_PAL_ZOOM;
  const c  = Math.floor((e.clientX - r.left) / r.width  * (mapEd.tilesheetCols * ts) / ts);
  const ro = Math.floor((e.clientY - r.top)  / r.height * (mapEd.tilesheetRows * ts) / ts);
  if (c < 0 || ro < 0 || c >= mapEd.tilesheetCols || ro >= mapEd.tilesheetRows) return;
  mapEd.selectedTile = ro * mapEd.tilesheetCols + c;
  mapDrawPalette();
}

function mapEventToCell(e) {
  const r = e.currentTarget.getBoundingClientRect();
  const dts = mapEd.tileSize * MAP_CANVAS_ZOOM;
  const x = Math.floor((e.clientX - r.left) / r.width  * mapEd.width);
  const y = Math.floor((e.clientY - r.top)  / r.height * mapEd.height);
  if (x < 0 || y < 0 || x >= mapEd.width || y >= mapEd.height) return null;
  return { x, y };
}

function mapPaintCell(cell, mode) {
  const layer = mapEd.layers[0];
  if (mode === 1) {
    layer.data[cell.y][cell.x] = mapEd.selectedTile;
  } else if (mode === 2) {
    layer.data[cell.y][cell.x] = MAP_EMPTY;
  }
  mapMarkDirty();
  mapDrawMap();
}

function mapFindEventAt(x, y) {
  return mapEd.events.findIndex(ev => ev.x === x && ev.y === y);
}

function mapNextEventId() {
  let m = 0;
  for (const ev of mapEd.events) if (ev.id > m) m = ev.id;
  return m + 1;
}

function mapCanvasMouseDown(e) {
  e.preventDefault();
  const cell = mapEventToCell(e);
  if (!cell) return;

  if (mapEd.mode === 'event') {
    if (e.button === 2) {
      // Right-click deletes an event at this cell
      const i = mapFindEventAt(cell.x, cell.y);
      if (i >= 0) {
        mapEd.events.splice(i, 1);
        if (mapEd.selectedEvent === i) mapHideEventInspector();
        else if (mapEd.selectedEvent !== null && mapEd.selectedEvent > i) mapEd.selectedEvent--;
        mapMarkDirty();
        mapDrawMap();
      }
      return;
    }
    let i = mapFindEventAt(cell.x, cell.y);
    if (i < 0) {
      mapEd.events.push({ x: cell.x, y: cell.y, id: mapNextEventId(), data: {} });
      i = mapEd.events.length - 1;
      mapMarkDirty();
    }
    mapShowEventInspector(i);
    mapDrawMap();
    return;
  }

  if (mapEd.mode === 'erase') {
    mapEd.painting = 2;
  } else if (e.button === 2) {
    mapEd.painting = 2;
  } else {
    mapEd.painting = 1;
  }
  mapPaintCell(cell, mapEd.painting);
}

function mapCanvasMouseMove(e) {
  if (!mapEd.painting) return;
  if (mapEd.mode === 'event') return;
  const cell = mapEventToCell(e);
  if (!cell) return;
  mapPaintCell(cell, mapEd.painting);
}

function mapCanvasMouseUp() { mapEd.painting = 0; }

// -------- event inspector --------

function mapShowEventInspector(i) {
  mapEd.selectedEvent = i;
  const ev = mapEd.events[i];
  document.getElementById('mapEventInspector').style.display = 'block';
  document.getElementById('mapEvId').value = ev.id;
  document.getElementById('mapEvPos').textContent = '(' + ev.x + ', ' + ev.y + ')';
  document.getElementById('mapEvData').value = JSON.stringify(ev.data || {}, null, 2);
  document.getElementById('mapEvMsg').textContent = '';
}

function mapHideEventInspector() {
  mapEd.selectedEvent = null;
  document.getElementById('mapEventInspector').style.display = 'none';
}

function mapEventApply() {
  if (mapEd.selectedEvent === null) return;
  const ev = mapEd.events[mapEd.selectedEvent];
  const idEl = document.getElementById('mapEvId');
  const msg  = document.getElementById('mapEvMsg');
  const newId = parseInt(idEl.value, 10);
  if (!Number.isFinite(newId) || newId < 0) {
    msg.textContent = 'ID must be a non-negative integer';
    return;
  }
  let parsed;
  try {
    parsed = JSON.parse(document.getElementById('mapEvData').value || '{}');
  } catch (e) {
    msg.textContent = 'JSON parse error: ' + e.message;
    return;
  }
  if (typeof parsed !== 'object' || parsed === null || Array.isArray(parsed)) {
    msg.textContent = 'Data must be a JSON object';
    return;
  }
  ev.id = newId;
  ev.data = parsed;
  mapMarkDirty();
  mapDrawMap();
  msg.textContent = 'Applied';
}

function mapEventDelete() {
  if (mapEd.selectedEvent === null) return;
  mapEd.events.splice(mapEd.selectedEvent, 1);
  mapHideEventInspector();
  mapMarkDirty();
  mapDrawMap();
}

// -------- JSON I/O --------

function mapSerialize() {
  const layers = mapEd.layers.map(l => ({
    name: l.name,
    data: l.data.map(row => row.slice()),
  }));
  return {
    format: MAP_FORMAT_NAME,
    version: MAP_FORMAT_VER,
    tilesheet: mapEd.tilesheetPath || '',
    tilesheet_cols: mapEd.tilesheetCols,
    tile_size: mapEd.tileSize,
    width:  mapEd.width,
    height: mapEd.height,
    layers,
    events: mapEd.events.map(ev => ({ x: ev.x, y: ev.y, id: ev.id, data: ev.data || {} })),
  };
}

function mapDeserialize(obj, originPath) {
  if (!obj || typeof obj !== 'object') throw new Error('Map JSON not an object');
  if (obj.format !== MAP_FORMAT_NAME) throw new Error('Unexpected format: ' + obj.format);
  if (obj.version !== MAP_FORMAT_VER) throw new Error('Unsupported version: ' + obj.version);
  const w = obj.width | 0, h = obj.height | 0;
  if (w <= 0 || h <= 0) throw new Error('Invalid map size');

  mapEd.width  = w;
  mapEd.height = h;
  mapEd.tileSize = obj.tile_size | 0 || 16;
  mapEd.tilesheetPath = obj.tilesheet || null;
  mapEd.tilesheetCols = obj.tilesheet_cols | 0 || 0;

  const layers = Array.isArray(obj.layers) && obj.layers.length
    ? obj.layers
    : [{ name: 'ground', data: mapAllocLayer(w, h, MAP_EMPTY) }];
  mapEd.layers = layers.map(l => {
    const data = mapAllocLayer(w, h, MAP_EMPTY);
    if (Array.isArray(l.data)) {
      for (let y = 0; y < Math.min(h, l.data.length); y++) {
        const row = l.data[y];
        if (!Array.isArray(row)) continue;
        for (let x = 0; x < Math.min(w, row.length); x++) {
          const v = row[x];
          data[y][x] = (v === null || v === undefined) ? MAP_EMPTY : (v | 0);
        }
      }
    }
    return { name: l.name || 'layer', data };
  });

  mapEd.events = Array.isArray(obj.events) ? obj.events.map(ev => ({
    x: ev.x | 0, y: ev.y | 0, id: ev.id | 0,
    data: (ev.data && typeof ev.data === 'object' && !Array.isArray(ev.data)) ? ev.data : {},
  })) : [];

  mapEd.path = originPath || null;
  mapEd.dirty = false;
  mapEd.selectedEvent = null;

  // Tilesheet pixels are not in the JSON — caller should load the tilesheet
  // separately. If the embedded path is reachable on the device the user can
  // hit "Load Tilesheet from Device" afterwards.
  mapEd.tilesheetPixels = null;
  mapEd.tilesheetCanvas = null;
  mapEd.tilesheetRows = 0;

  mapResizeCanvas();
  mapRedraw();
  mapHideEventInspector();
  mapUpdateToolbar();
}

async function mapLoadDevice() {
  const def = mapEd.path || (MAP_DEFAULT_DIR + '/');
  const path = prompt('Load map JSON from device path:', def);
  if (!path) return;
  try {
    showProgress(-1, 'Loading ' + path + '...');
    const data = await deviceReadFile(path);
    const text = new TextDecoder().decode(data);
    const obj  = JSON.parse(text);
    mapDeserialize(obj, path);
    log('Map: loaded ' + path + ' (' + formatSize(data.length) + ')', 'ok');
    toast('Loaded ' + path + ' (' + formatSize(data.length) + ')', 'ok');
    // Auto-fetch the referenced tilesheet so the user sees a rendered map.
    if (mapEd.tilesheetPath) {
      const jsonCols = mapEd.tilesheetCols;
      try {
        showProgress(-1, 'Loading tilesheet ' + mapEd.tilesheetPath + '...');
        const sheetData = await deviceReadFile(mapEd.tilesheetPath);
        mapLoadTilesheetFromBuffer(
          sheetData.buffer.slice(sheetData.byteOffset, sheetData.byteOffset + sheetData.byteLength),
          mapEd.tilesheetPath);
        log('Map: tilesheet auto-loaded ' + mapEd.tilesheetPath, 'ok');
        toast('Tilesheet auto-loaded ' + mapEd.tilesheetPath, 'ok');
        if (jsonCols && jsonCols !== mapEd.tilesheetCols) {
          log('Map: warning - JSON tilesheet_cols=' + jsonCols +
              ' but sheet actually has ' + mapEd.tilesheetCols + ' cols; tile indices may be off', 'err');
          toast('Warning: tilesheet_cols mismatch (' + jsonCols +
                ' vs ' + mapEd.tilesheetCols + ')', 'err');
        }
      } catch (e) {
        log('Map: tilesheet auto-load failed (' + e.message +
            '). Use "Load Tilesheet from Device..." to load it manually.', 'err');
        toast('Tilesheet auto-load failed: ' + e.message, 'err');
      }
    }
  } catch (e) {
    log('Map load failed: ' + e.message, 'err');
    toast('Map load failed: ' + e.message, 'err');
  }
  hideProgress();
}

async function mapSaveDevice() {
  const def = mapEd.path || (MAP_DEFAULT_DIR + '/map.json');
  const path = prompt('Save map JSON to device path:', def);
  if (!path) return;
  try {
    const text = JSON.stringify(mapSerialize());
    const data = new TextEncoder().encode(text);
    showProgress(0, 'Saving ' + path + '...');
    await deviceWriteFile(path, data, (sent, total) => {
      showProgress(Math.min(100, Math.round(sent / total * 100)),
                   'Saving ' + path + '... ' + formatSize(sent) + ' / ' + formatSize(total));
    });
    mapEd.path = path;
    mapEd.dirty = false;
    mapUpdateToolbar();
    log('Map: saved ' + path + ' (' + formatSize(data.length) + ')', 'ok');
    toast('Saved ' + path + ' (' + formatSize(data.length) + ')', 'ok');
    if (typeof refreshDir === 'function' && currentPath && path.startsWith(currentPath)) {
      refreshDir().catch(() => {});
    }
  } catch (e) {
    log('Map save failed: ' + e.message, 'err');
    toast('Map save failed: ' + e.message, 'err');
  }
  hideProgress();
}

function mapOnLocalJson(fileList) {
  if (!fileList || !fileList.length) return;
  const file = fileList[0];
  file.text().then(text => {
    try {
      const obj = JSON.parse(text);
      mapDeserialize(obj, null);
      log('Map: loaded local ' + file.name, 'ok');
      toast('Map loaded: ' + file.name, 'ok');
      if (mapEd.tilesheetPath) {
        log('Map: hint — tilesheet referenced is ' + mapEd.tilesheetPath);
      }
    } catch (e) {
      log('Map load failed: ' + e.message, 'err');
      toast('Map load failed: ' + e.message, 'err');
    }
  }).catch(e => {
    log('Map read error: ' + e.message, 'err');
    toast('Map read error: ' + e.message, 'err');
  });
  document.getElementById('mapJsonInput').value = '';
}

function mapSaveLocal() {
  const text = JSON.stringify(mapSerialize());
  const blob = new Blob([text], { type: 'application/json' });
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  const baseName = mapEd.path ? mapEd.path.split('/').pop() : 'map.json';
  a.href = url; a.download = baseName.endsWith('.json') ? baseName : baseName + '.json';
  a.click();
  URL.revokeObjectURL(url);
  log('Map: downloaded ' + a.download, 'ok');
  toast('Downloaded ' + a.download, 'ok');
}

// -------- bootstrap --------

(function mapInit() {
  const map = document.getElementById('mapCanvas');
  const pal = document.getElementById('mapPaletteCanvas');
  if (!map || !pal) return;

  pal.addEventListener('mousedown', mapPaletteMouseDown);
  map.addEventListener('contextmenu', e => e.preventDefault());
  map.addEventListener('mousedown', mapCanvasMouseDown);
  map.addEventListener('mousemove', mapCanvasMouseMove);
  window.addEventListener('mouseup', mapCanvasMouseUp);

  mapEd.layers[0].data = mapAllocLayer(mapEd.width, mapEd.height, MAP_EMPTY);
  mapResizeCanvas();
  mapRedraw();
  mapUpdateToolbar();

  registerTabChangeListener('map', () => {
    mapResizeCanvas();
    mapRedraw();
  });
})();
