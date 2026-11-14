// ============================================================
// Sprite editor — tile sheet of 16x16 RGB332 tiles, stored as 8-bit BMP
// ============================================================

const SPR_TILE       = 16;   // pixel size of each tile (locked, per device convention)
const SPR_SHEET_ZOOM = 2;    // pixels per source pixel in overview canvas
const SPR_TILE_ZOOM  = 20;   // pixels per source pixel in editor canvas
const SPR_PAL_SWATCH = 18;   // pixels per palette swatch (16x16 grid)
const SPR_DEFAULT_DIR = '/sprites';

// Hand-picked defaults that match the on-device sprite_editor.app.rb 16-swatch
// quick palette ([sprite_editor.app.rb:10-13]) so a freshly-opened editor
// shows the same useful starter colors.
const SPR_DEFAULT_COLOR = 0xE0;  // bright red

const sprite = {
  tileCols: 8,
  tileRows: 8,
  pixels: null,                       // Uint8Array, sheetWidth * sheetHeight
  selectedTile: { col: 0, row: 0 },
  selectedColor: SPR_DEFAULT_COLOR,
  path: null,
  dirty: false,
  painting: 0,                        // 0 = idle, 1 = paint
};

// Diagonal-stripe hatch used to mark RGB332 index 0 (transparent chroma key)
// distinctly from solid black throughout the sprite editor canvases.
let sprTransparencyTexture = null;
function sprGetTransparencyTexture() {
  if (sprTransparencyTexture) return sprTransparencyTexture;
  const t = document.createElement('canvas');
  t.width = 4; t.height = 4;
  const c = t.getContext('2d');
  c.fillStyle = '#1a1a1a';
  c.fillRect(0, 0, 4, 4);
  c.strokeStyle = '#d82455';
  c.lineWidth = 1;
  c.lineCap = 'square';
  c.beginPath();
  c.moveTo(0, 4); c.lineTo(4, 0);
  c.stroke();
  sprTransparencyTexture = t;
  return t;
}

function sprSheetWidth()  { return sprite.tileCols * SPR_TILE; }
function sprSheetHeight() { return sprite.tileRows * SPR_TILE; }

function sprCreateEmpty(cols, rows) {
  sprite.tileCols = cols;
  sprite.tileRows = rows;
  sprite.pixels = new Uint8Array(cols * SPR_TILE * rows * SPR_TILE);  // index 0 = transparent black
  sprite.selectedTile = { col: 0, row: 0 };
  sprite.path = null;
  sprite.dirty = false;
  sprResizeCanvases();
  sprRedrawAll();
  sprUpdateToolbar();
}

function sprResizeCanvases() {
  const sheet = document.getElementById('sprSheetCanvas');
  sheet.width  = sprSheetWidth()  * SPR_SHEET_ZOOM;
  sheet.height = sprSheetHeight() * SPR_SHEET_ZOOM;

  const tile = document.getElementById('sprTileCanvas');
  tile.width  = SPR_TILE * SPR_TILE_ZOOM;
  tile.height = SPR_TILE * SPR_TILE_ZOOM;

  const pal = document.getElementById('sprPaletteCanvas');
  pal.width  = 16 * SPR_PAL_SWATCH;
  pal.height = 16 * SPR_PAL_SWATCH;
}

function sprMarkDirty() {
  sprite.dirty = true;
  sprUpdateToolbar();
}

function sprUpdateToolbar() {
  document.getElementById('sprSheetInfo').textContent =
    sprite.tileCols + ' x ' + sprite.tileRows + ' tiles (' +
    sprSheetWidth() + 'x' + sprSheetHeight() + 'px)';
  document.getElementById('sprTileInfo').textContent =
    '(' + sprite.selectedTile.col + ',' + sprite.selectedTile.row + ')';
  document.getElementById('sprPathInfo').textContent =
    (sprite.path || '(unsaved)') + (sprite.dirty ? ' *' : '');
  document.getElementById('sprColorInfo').textContent =
    '#' + sprite.selectedColor.toString(16).padStart(2, '0').toUpperCase() +
    ' (RGB332)';
}

// -------- drawing --------

function sprRedrawAll() {
  sprRedrawSheet();
  sprRedrawTile();
  sprRedrawPalette();
}

function sprRedrawSheet() {
  const canvas = document.getElementById('sprSheetCanvas');
  if (!canvas.width) return;
  const ctx = canvas.getContext('2d');
  ctx.imageSmoothingEnabled = false;
  const w = sprSheetWidth();
  const h = sprSheetHeight();

  // Pixel-by-pixel fill using ImageData. Index 0 is left transparent so the
  // hatch background below shines through and marks the chroma-key cells.
  const img = ctx.createImageData(w, h);
  for (let i = 0; i < w * h; i++) {
    const c = sprite.pixels[i];
    if (c === 0) {
      img.data[i * 4 + 3] = 0;
      continue;
    }
    img.data[i * 4 + 0] = RGB332_RGB[c * 3 + 0];
    img.data[i * 4 + 1] = RGB332_RGB[c * 3 + 1];
    img.data[i * 4 + 2] = RGB332_RGB[c * 3 + 2];
    img.data[i * 4 + 3] = 255;
  }
  // Render into an offscreen 1:1 canvas then drawImage scaled for crisp upscale.
  const off = document.createElement('canvas');
  off.width = w; off.height = h;
  off.getContext('2d').putImageData(img, 0, 0);
  ctx.fillStyle = ctx.createPattern(sprGetTransparencyTexture(), 'repeat');
  ctx.fillRect(0, 0, canvas.width, canvas.height);
  ctx.drawImage(off, 0, 0, canvas.width, canvas.height);

  // Tile grid
  ctx.strokeStyle = 'rgba(108, 0, 0, 0.6)';
  ctx.lineWidth = 1;
  for (let cx = 0; cx <= sprite.tileCols; cx++) {
    const x = cx * SPR_TILE * SPR_SHEET_ZOOM + 0.5;
    ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, canvas.height); ctx.stroke();
  }
  for (let cy = 0; cy <= sprite.tileRows; cy++) {
    const y = cy * SPR_TILE * SPR_SHEET_ZOOM + 0.5;
    ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(canvas.width, y); ctx.stroke();
  }

  // Selection highlight
  ctx.strokeStyle = '#d82455';
  ctx.lineWidth = 2;
  const sx = sprite.selectedTile.col * SPR_TILE * SPR_SHEET_ZOOM + 1;
  const sy = sprite.selectedTile.row * SPR_TILE * SPR_SHEET_ZOOM + 1;
  ctx.strokeRect(sx, sy, SPR_TILE * SPR_SHEET_ZOOM - 2, SPR_TILE * SPR_SHEET_ZOOM - 2);
}

function sprRedrawTile() {
  const canvas = document.getElementById('sprTileCanvas');
  if (!canvas.width) return;
  const ctx = canvas.getContext('2d');
  ctx.imageSmoothingEnabled = false;
  ctx.clearRect(0, 0, canvas.width, canvas.height);

  const tx = sprite.selectedTile.col * SPR_TILE;
  const ty = sprite.selectedTile.row * SPR_TILE;
  const sw = sprSheetWidth();

  const hatch = ctx.createPattern(sprGetTransparencyTexture(), 'repeat');
  for (let y = 0; y < SPR_TILE; y++) {
    for (let x = 0; x < SPR_TILE; x++) {
      const c = sprite.pixels[(ty + y) * sw + (tx + x)];
      ctx.fillStyle = (c === 0) ? hatch : rgb332ToCss(c);
      ctx.fillRect(x * SPR_TILE_ZOOM, y * SPR_TILE_ZOOM,
                   SPR_TILE_ZOOM, SPR_TILE_ZOOM);
    }
  }

  // Pixel grid
  ctx.strokeStyle = 'rgba(255,255,255,0.15)';
  ctx.lineWidth = 1;
  for (let i = 0; i <= SPR_TILE; i++) {
    const p = i * SPR_TILE_ZOOM + 0.5;
    ctx.beginPath(); ctx.moveTo(p, 0); ctx.lineTo(p, canvas.height); ctx.stroke();
    ctx.beginPath(); ctx.moveTo(0, p); ctx.lineTo(canvas.width, p); ctx.stroke();
  }
}

function sprRedrawPalette() {
  const canvas = document.getElementById('sprPaletteCanvas');
  if (!canvas.width) return;
  const ctx = canvas.getContext('2d');
  ctx.clearRect(0, 0, canvas.width, canvas.height);

  const hatch = ctx.createPattern(sprGetTransparencyTexture(), 'repeat');
  for (let i = 0; i < 256; i++) {
    const px = (i & 0x0f) * SPR_PAL_SWATCH;
    const py = (i >> 4)   * SPR_PAL_SWATCH;
    ctx.fillStyle = (i === 0) ? hatch : rgb332ToCss(i);
    ctx.fillRect(px, py, SPR_PAL_SWATCH, SPR_PAL_SWATCH);
  }

  // Selection highlight
  const sx = (sprite.selectedColor & 0x0f) * SPR_PAL_SWATCH;
  const sy = (sprite.selectedColor >> 4)   * SPR_PAL_SWATCH;
  ctx.strokeStyle = '#fff';
  ctx.lineWidth = 2;
  ctx.strokeRect(sx + 1, sy + 1, SPR_PAL_SWATCH - 2, SPR_PAL_SWATCH - 2);
  ctx.strokeStyle = '#000';
  ctx.lineWidth = 1;
  ctx.strokeRect(sx + 2, sy + 2, SPR_PAL_SWATCH - 4, SPR_PAL_SWATCH - 4);
}

// -------- interaction --------

function sprSheetMouseDown(e) {
  const r = e.currentTarget.getBoundingClientRect();
  const x = ((e.clientX - r.left) / r.width)  * sprSheetWidth();
  const y = ((e.clientY - r.top)  / r.height) * sprSheetHeight();
  const col = Math.floor(x / SPR_TILE);
  const row = Math.floor(y / SPR_TILE);
  if (col < 0 || row < 0 || col >= sprite.tileCols || row >= sprite.tileRows) return;
  sprite.selectedTile = { col, row };
  sprRedrawSheet();
  sprRedrawTile();
  sprUpdateToolbar();
}

function sprTileEventToPixel(e) {
  const r = e.currentTarget.getBoundingClientRect();
  const x = ((e.clientX - r.left) / r.width)  * SPR_TILE;
  const y = ((e.clientY - r.top)  / r.height) * SPR_TILE;
  const px = Math.floor(x);
  const py = Math.floor(y);
  if (px < 0 || py < 0 || px >= SPR_TILE || py >= SPR_TILE) return null;
  return { px, py };
}

function sprTilePaintAt(px, py, mode) {
  const tx = sprite.selectedTile.col * SPR_TILE + px;
  const ty = sprite.selectedTile.row * SPR_TILE + py;
  const idx = ty * sprSheetWidth() + tx;
  if (mode === 1) sprite.pixels[idx] = sprite.selectedColor;
  else if (mode === 3) {
    // eyedropper
    sprite.selectedColor = sprite.pixels[idx];
    sprRedrawPalette();
    sprUpdateToolbar();
    return;
  }
  sprMarkDirty();
  sprRedrawTile();
  sprRedrawSheet();
}

function sprTileMouseDown(e) {
  e.preventDefault();
  const p = sprTileEventToPixel(e);
  if (!p) return;
  // Right-click and middle-click both act as one-shot eyedropper.
  // To erase, pick palette index 0 (transparent) and paint with left-click.
  if (e.button === 2 || e.button === 1) { sprTilePaintAt(p.px, p.py, 3); return; }
  sprite.painting = 1;
  sprTilePaintAt(p.px, p.py, sprite.painting);
}

function sprTileMouseMove(e) {
  if (!sprite.painting) return;
  const p = sprTileEventToPixel(e);
  if (!p) return;
  sprTilePaintAt(p.px, p.py, sprite.painting);
}

function sprTileMouseUp() { sprite.painting = 0; }

function sprPaletteMouseDown(e) {
  const r = e.currentTarget.getBoundingClientRect();
  const x = ((e.clientX - r.left) / r.width)  * 16;
  const y = ((e.clientY - r.top)  / r.height) * 16;
  const col = Math.floor(x);
  const row = Math.floor(y);
  if (col < 0 || row < 0 || col >= 16 || row >= 16) return;
  sprite.selectedColor = (row << 4) | col;
  sprRedrawPalette();
  sprUpdateToolbar();
}

// -------- new sheet modal --------

function spriteNew() {
  if (sprite.dirty && !confirm('Discard unsaved changes?')) return;
  document.getElementById('sprNewModal').classList.add('open');
}

function spriteNewCancel() {
  document.getElementById('sprNewModal').classList.remove('open');
}

function spriteNewConfirm() {
  const cols = Math.max(1, Math.min(16, parseInt(document.getElementById('sprNewCols').value, 10) || 8));
  const rows = Math.max(1, Math.min(16, parseInt(document.getElementById('sprNewRows').value, 10) || 8));
  document.getElementById('sprNewModal').classList.remove('open');
  sprCreateEmpty(cols, rows);
}

// -------- BMP I/O --------

function sprLoadBmpFromBuffer(arrayBuffer, originPath) {
  const decoded = decodeBmp332(arrayBuffer);
  if (decoded.width  % SPR_TILE !== 0) throw new Error('Width not a multiple of 16: ' + decoded.width);
  if (decoded.height % SPR_TILE !== 0) throw new Error('Height not a multiple of 16: ' + decoded.height);
  sprite.tileCols = decoded.width  / SPR_TILE;
  sprite.tileRows = decoded.height / SPR_TILE;
  sprite.pixels   = decoded.pixels;
  sprite.selectedTile = { col: 0, row: 0 };
  sprite.path = originPath || null;
  sprite.dirty = false;
  sprResizeCanvases();
  sprRedrawAll();
  sprUpdateToolbar();
  if (decoded.remapped) log('Sprite: remapped non-RGB332 palette to nearest device colors');
}

function spriteOnLocalFile(fileList) {
  if (!fileList || !fileList.length) return;
  const file = fileList[0];
  file.arrayBuffer().then(buf => {
    try {
      sprLoadBmpFromBuffer(buf, null);
      log('Sprite: loaded local ' + file.name, 'ok');
      toast('Sprite loaded: ' + file.name, 'ok');
    } catch (e) {
      log('Sprite load failed: ' + e.message, 'err');
      toast('Sprite load failed: ' + e.message, 'err');
    }
  }).catch(e => {
    log('Sprite read error: ' + e.message, 'err');
    toast('Sprite read error: ' + e.message, 'err');
  });
  document.getElementById('sprFileInput').value = '';
}

function spriteSaveLocal() {
  if (!sprite.pixels) {
    log('Sprite: nothing to save', 'err');
    toast('Nothing to save', 'err');
    return;
  }
  const data = encodeBmp332(sprSheetWidth(), sprSheetHeight(), sprite.pixels);
  const blob = new Blob([data], { type: 'image/bmp' });
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  const baseName = sprite.path ? sprite.path.split('/').pop() : 'sprite.bmp';
  a.href = url; a.download = baseName.endsWith('.bmp') ? baseName : baseName + '.bmp';
  a.click();
  URL.revokeObjectURL(url);
  log('Sprite: downloaded ' + a.download, 'ok');
  toast('Downloaded ' + a.download, 'ok');
}

async function spriteLoadDevice() {
  const def = sprite.path || (SPR_DEFAULT_DIR + '/');
  const path = prompt('Load sprite BMP from device path:', def);
  if (!path) return;
  try {
    showProgress(-1, 'Loading ' + path + '...');
    const data = await deviceReadFile(path, n => showProgress(-1, 'Loading ' + path + '... ' + formatSize(n)));
    sprLoadBmpFromBuffer(data.buffer.slice(data.byteOffset, data.byteOffset + data.byteLength), path);
    log('Sprite: loaded ' + path + ' (' + formatSize(data.length) + ')', 'ok');
    toast('Loaded ' + path + ' (' + formatSize(data.length) + ')', 'ok');
  } catch (e) {
    log('Sprite load failed: ' + e.message, 'err');
    toast('Load failed: ' + e.message, 'err');
  }
  hideProgress();
}

async function spriteSaveDevice() {
  if (!sprite.pixels) {
    log('Sprite: nothing to save', 'err');
    toast('Nothing to save', 'err');
    return;
  }
  const def = sprite.path || (SPR_DEFAULT_DIR + '/sprite.bmp');
  const path = prompt('Save sprite BMP to device path:', def);
  if (!path) return;
  try {
    const buf = encodeBmp332(sprSheetWidth(), sprSheetHeight(), sprite.pixels);
    const data = new Uint8Array(buf);
    showProgress(0, 'Saving ' + path + '...');
    await deviceWriteFile(path, data, (sent, total) => {
      showProgress(Math.min(100, Math.round(sent / total * 100)),
                   'Saving ' + path + '... ' + formatSize(sent) + ' / ' + formatSize(total));
    });
    sprite.path = path;
    sprite.dirty = false;
    sprUpdateToolbar();
    log('Sprite: saved ' + path + ' (' + formatSize(data.length) + ')', 'ok');
    toast('Saved ' + path + ' (' + formatSize(data.length) + ')', 'ok');
    if (typeof refreshDir === 'function' && currentPath && path.startsWith(currentPath)) {
      refreshDir().catch(() => {});
    }
  } catch (e) {
    log('Sprite save failed: ' + e.message, 'err');
    toast('Save failed: ' + e.message, 'err');
  }
  hideProgress();
}

// -------- bootstrap --------

(function sprInit() {
  // Defer until DOM is parsed (this script runs at end of <body> so it's safe,
  // but registerTabChangeListener must exist — it's defined in app.js loaded
  // above).
  const sheet   = document.getElementById('sprSheetCanvas');
  const tile    = document.getElementById('sprTileCanvas');
  const palette = document.getElementById('sprPaletteCanvas');
  if (!sheet || !tile || !palette) return;

  sheet.addEventListener('mousedown', sprSheetMouseDown);

  tile.addEventListener('contextmenu', e => e.preventDefault());
  tile.addEventListener('mousedown',  sprTileMouseDown);
  tile.addEventListener('mousemove',  sprTileMouseMove);
  window.addEventListener('mouseup', sprTileMouseUp);

  palette.addEventListener('mousedown', sprPaletteMouseDown);
  palette.addEventListener('contextmenu', e => e.preventDefault());

  sprCreateEmpty(8, 8);
  registerTabChangeListener('sprite', () => {
    // Re-render in case the canvases were resized while in display:none.
    sprResizeCanvases();
    sprRedrawAll();
  });
})();
