// Type support in the console's text editor: completion, signature help,
// hover, diagnostics and F1 help, all answered in the browser by the engine
// the device editor uses (picoruby-ti, compiled to WebAssembly by
// `rake ti:wasm`). Nothing here talks to a device.
//
// js/ti.js is the module that owns the engine; this file is the UI around it,
// kept out of app.js the way sprite-editor.js and map-editor.js are. app.js
// calls tiEditorOpened / tiEditorTextChanged / tiEditorClosed at the three
// moments that matter and needs to know nothing else.
//
// Where things are on the screen is measured, never computed: the editor wraps
// long lines (white-space: pre-wrap), so a column times a character width would
// be wrong as soon as a line wraps. The highlight overlay behind the textarea
// holds exactly the same text at exactly the same place, so a DOM Range over it
// gives the true rectangle of any character -- that is how the completion list
// finds the caret and how a diagnostic finds the code it is about.

const TI_COMPLETION_MAX_ROWS = 10;
const TI_DIAGNOSE_DELAY_MS = 600;    // after typing stops
const TI_HOVER_DELAY_MS = 250;       // after the pointer stops
const TI_SIGNATURE_DELAY_MS = 150;   // after the caret moves

const tiState = {
  api: null,          // the engine, once loaded
  status: 'idle',     // idle | loading | ready | unavailable
  reason: '',
  open: false,        // completion list showing
  items: [],
  index: 0,
  prefix: 0,          // characters before the caret the choice replaces
  diagnostics: [],
  timers: { diagnose: null, hover: null, signature: null },
  helpOpen: false,
  enabled: false,     // type support is Ruby-only; off for .bas/.lua/etc.
};

// ---------------------------------------------------------------- loading

// The engine is fetched when the editor first opens, not when the page loads:
// most visits to the console never open a file, and the module is 400KB.
async function tiEnsureEngine() {
  if (tiState.status === 'ready' || tiState.status === 'loading') return tiState.api;
  if (!window.FmrbTi) {
    // Module scripts do not load from file:// -- the page has to be served.
    tiState.status = 'unavailable';
    tiState.reason = 'type help needs the console to be served over http (file:// cannot load modules)';
    tiSetStatus();
    return null;
  }
  tiState.status = 'loading';
  tiSetStatus();
  try {
    tiState.api = await window.FmrbTi.createTi({ lang: tiLanguage() });
    tiState.status = 'ready';
  } catch (e) {
    tiState.status = 'unavailable';
    tiState.reason = 'type help failed to load: ' + e.message + ' (run `rake ti:wasm`)';
  }
  tiSetStatus();
  return tiState.api;
}

function tiLanguage() {
  const saved = localStorage.getItem('fmrbTiLang');
  if (saved === 'ja' || saved === 'en') return saved;
  return (navigator.language || 'en').toLowerCase().startsWith('ja') ? 'ja' : 'en';
}

function tiSetLanguage(lang) {
  localStorage.setItem('fmrbTiLang', lang);
  if (tiState.api) tiState.api.lang = lang;
  const select = document.getElementById('tiLangSelect');
  if (select) select.value = lang;
  if (tiState.open) tiRenderCompletion();
  if (tiState.helpOpen) tiShowHelp();
}

function tiSetStatus(message) {
  const el = document.getElementById('tiStatus');
  if (!el) return;
  if (message !== undefined) {
    el.textContent = message;
    return;
  }
  const text = {
    idle: '',
    loading: 'type help: loading...',
    ready: 'type help: ready (Ctrl+Space, F1)',
    unavailable: tiState.reason,
  }[tiState.status];
  el.textContent = text || '';
  el.className = tiState.status === 'unavailable' ? 'ti-status err' : 'ti-status';
}

// ------------------------------------------------------------- geometry

function tiTextarea() { return document.getElementById('editorTextarea'); }
function tiHighlight() { return document.getElementById('editorHighlight'); }

// Every text node of the highlight overlay with the offset it starts at, so a
// character index in the document can be turned into a DOM position. Rebuilt on
// demand: Prism replaces the whole tree on each keystroke.
function tiTextNodes() {
  const code = tiHighlight();
  const nodes = [];
  let total = 0;
  if (!code) return { nodes, total };
  const walker = document.createTreeWalker(code, NodeFilter.SHOW_TEXT);
  while (walker.nextNode()) {
    const node = walker.currentNode;
    nodes.push({ node, start: total });
    total += node.nodeValue.length;
  }
  return { nodes, total };
}

function tiRangeFor(map, from, to) {
  if (!map.nodes.length) return null;
  const place = (index) => {
    let lo = 0, hi = map.nodes.length - 1;
    while (lo < hi) {
      const mid = (lo + hi + 1) >> 1;
      if (map.nodes[mid].start <= index) lo = mid; else hi = mid - 1;
    }
    const entry = map.nodes[lo];
    const within = Math.min(Math.max(index - entry.start, 0), entry.node.nodeValue.length);
    return [entry.node, within];
  };
  const range = document.createRange();
  const [startNode, startOffset] = place(from);
  const [endNode, endOffset] = place(to);
  range.setStart(startNode, startOffset);
  range.setEnd(endNode, endOffset);
  return range;
}

// The rectangle of the character at `index`, in viewport coordinates.
function tiRectAt(map, index) {
  const range = tiRangeFor(map, index, Math.min(index + 1, map.total));
  if (!range) return null;
  const rect = range.getBoundingClientRect();
  return rect.width === 0 && rect.height === 0 ? null : rect;
}

// Which character a point is over. Characters come out of the layout in reading
// order, so their rectangles are ordered too and a binary search finds the one
// under the pointer without measuring the rest.
function tiOffsetFromPoint(x, y) {
  const map = tiTextNodes();
  if (!map.total) return -1;
  let lo = 0, hi = map.total - 1, best = -1;
  while (lo <= hi) {
    const mid = (lo + hi) >> 1;
    const rect = tiRectAt(map, mid);
    if (!rect) { lo = mid + 1; continue; }
    if (y < rect.top) { hi = mid - 1; continue; }
    if (y > rect.bottom) { lo = mid + 1; continue; }
    if (x < rect.left) { hi = mid - 1; continue; }
    if (x > rect.right) { lo = mid + 1; continue; }
    best = mid;
    break;
  }
  return best;
}

// Character index of a (line, character column) pair, which is how the engine
// reports a diagnostic.
function tiIndexOfPosition(text, line, column) {
  let index = 0;
  for (let i = 0; i < line; i++) {
    const next = text.indexOf('\n', index);
    if (next < 0) return text.length;
    index = next + 1;
  }
  return Math.min(index + column, text.length);
}

// --------------------------------------------------------- completion UI

function tiIsNameChar(ch) {
  return /[A-Za-z0-9_]/.test(ch);
}

// What the caret is standing after: a name, the dot of a call, or the colon of
// a constant path. Anywhere else there is nothing to complete.
function tiCanComplete(text, caret) {
  if (caret <= 0) return false;
  const ch = text[caret - 1];
  return ch === '.' || ch === ':' || ch === '?' || ch === '!' || tiIsNameChar(ch);
}

function tiPrefixLength(text, caret) {
  let n = 0;
  while (caret - n > 0 && tiIsNameChar(text[caret - n - 1])) n++;
  return n;
}

// Object-common methods (every receiver has them, flattened in from Object and
// Kernel) that the completion list sinks below the receiver's own methods, so
// the specific, useful names lead. Operators are sunk too, by the shape of the
// name. The device editor applies the same rule (editor/ti_ui.rb) -- keep the
// two lists in step.
const TI_COMMON_METHODS = new Set([
  'attr_accessor', 'attr_reader', 'block_given?', 'class', 'dup', 'exit',
  'extend', 'include', 'inspect', 'is_a?', 'lambda', 'loop', 'nil?', 'p',
  'print', 'private', 'proc', 'public', 'puts', 'raise', 'relinquish', 'self',
  'sleep', 'sleep_ms', 'sprintf', 'to_s', 'yield',
]);

function tiIsDemoted(label) {
  if (TI_COMMON_METHODS.has(label)) return true;
  return !/^[A-Za-z_]/.test(label);   // operator methods start otherwise
}

// Two tiers, alphabetical within each (the engine already returns them sorted,
// and the index tie-break keeps that order): the receiver's own methods first,
// then object-common methods and operators.
function tiSortSuggestions(items) {
  return items
    .map((item, index) => [item, index])
    .sort((a, b) => {
      const demotedA = tiIsDemoted(a[0].label) ? 1 : 0;
      const demotedB = tiIsDemoted(b[0].label) ? 1 : 0;
      return demotedA - demotedB || a[1] - b[1];
    })
    .map((pair) => pair[0]);
}

async function tiOpenCompletion() {
  if (!tiState.enabled) return;
  const api = await tiEnsureEngine();
  if (!api) return;
  const ta = tiTextarea();
  const text = ta.value;
  const caret = ta.selectionStart;

  const started = performance.now();
  const items = api.suggest(text, api.byteOffsetOf(text, caret));
  const ms = Math.round(performance.now() - started);

  if (items.tooLarge) {
    tiSetStatus('type help: the file is too large for the engine');
    return;
  }
  if (!items.length) {
    tiSetStatus('type help: nothing to suggest here (' + ms + ' ms)');
    tiCloseCompletion();
    return;
  }
  tiSetStatus('type help: ' + items.length + ' candidates (' + ms + ' ms)');
  tiState.items = tiSortSuggestions(items);
  tiState.index = 0;
  tiState.prefix = tiPrefixLength(text, caret);
  tiState.open = true;
  tiRenderCompletion();
}

function tiCloseCompletion() {
  tiState.open = false;
  tiState.items = [];
  const box = document.getElementById('tiComplete');
  if (box) box.classList.remove('open');
}

function tiRenderCompletion() {
  const box = document.getElementById('tiComplete');
  const list = document.getElementById('tiCompleteList');
  const doc = document.getElementById('tiCompleteDoc');
  if (!box || !list) return;

  list.innerHTML = '';
  tiState.items.forEach((item, i) => {
    const row = document.createElement('div');
    row.className = 'ti-item' + (i === tiState.index ? ' selected' : '');
    row.dataset.index = String(i);
    const label = document.createElement('span');
    label.className = 'ti-label';
    label.textContent = item.label;
    const detail = document.createElement('span');
    detail.className = 'ti-detail';
    // The signature starts with the name the label already shows ("abs: () ->
    // Integer"), so only what follows it is worth a second column.
    detail.textContent = item.detail.startsWith(item.label)
      ? item.detail.slice(item.label.length)
      : item.detail;
    row.append(label, detail);
    row.addEventListener('mousedown', (e) => {
      e.preventDefault();          // keep the caret in the textarea
      tiState.index = i;
      tiAcceptCompletion();
    });
    list.appendChild(row);
  });

  const chosen = tiState.items[tiState.index];
  if (doc && chosen) {
    // The doc comment carries both languages; show the reader's half. The
    // signature is the same in every language, so it stands in when there is
    // no comment at all.
    const about = tiState.api ? tiState.api.pickLang(chosen.doc) : chosen.doc;
    doc.textContent = about || chosen.detail || '';
  }

  box.classList.add('open');
  tiPlaceCompletion();
  const selected = list.querySelector('.ti-item.selected');
  if (selected) selected.scrollIntoView({ block: 'nearest' });
}

function tiPlaceCompletion() {
  const box = document.getElementById('tiComplete');
  const ta = tiTextarea();
  if (!box || !ta) return;
  const map = tiTextNodes();
  const caret = Math.max(ta.selectionStart - 1, 0);
  const rect = tiRectAt(map, caret) || ta.getBoundingClientRect();
  const width = box.offsetWidth || 320;
  const height = box.offsetHeight || 200;
  let left = rect.left;
  let top = rect.bottom + 2;
  if (left + width > window.innerWidth - 8) left = window.innerWidth - width - 8;
  if (top + height > window.innerHeight - 8) top = Math.max(rect.top - height - 2, 8);
  box.style.left = Math.max(left, 8) + 'px';
  box.style.top = top + 'px';
}

function tiMoveCompletion(delta) {
  if (!tiState.open) return;
  const n = tiState.items.length;
  tiState.index = (tiState.index + delta + n) % n;
  tiRenderCompletion();
}

function tiAcceptCompletion() {
  if (!tiState.open) return;
  const chosen = tiState.items[tiState.index];
  const ta = tiTextarea();
  const caret = ta.selectionStart;
  const from = caret - tiState.prefix;
  tiCloseCompletion();
  if (!chosen) return;

  ta.setRangeText(chosen.label, from, caret, 'end');
  ta.focus();
  // setRangeText does not raise input, and the highlight, the dirty flag and
  // the diagnostics all hang off that event.
  ta.dispatchEvent(new Event('input', { bubbles: true }));
}

// ------------------------------------------------------- signature help

function tiScheduleSignature() {
  if (!tiState.enabled) return;
  clearTimeout(tiState.timers.signature);
  tiState.timers.signature = setTimeout(tiShowSignature, TI_SIGNATURE_DELAY_MS);
}

function tiShowSignature() {
  if (tiState.status !== 'ready' || tiState.open) return;
  const ta = tiTextarea();
  if (!ta || document.activeElement !== ta) return;
  const text = ta.value;
  const call = tiState.api.callContext(text, tiState.api.byteOffsetOf(text, ta.selectionStart));
  const el = document.getElementById('tiSignature');
  if (!el) return;
  if (!call.found) { el.textContent = ''; return; }
  const argument = call.argumentIndex >= 0
    ? '  <- ' + (call.argumentType ? call.argumentType + ' ' : '') +
      (call.argumentName || ('argument ' + (call.argumentIndex + 1)))
    : '';
  el.textContent = call.signature + argument;
}

// ---------------------------------------------------------------- hover

function tiScheduleHover(event) {
  if (!tiState.enabled) return;
  clearTimeout(tiState.timers.hover);
  const x = event.clientX, y = event.clientY;
  tiState.timers.hover = setTimeout(() => tiShowHover(x, y), TI_HOVER_DELAY_MS);
}

function tiShowHover(x, y) {
  const tip = document.getElementById('tiHover');
  if (!tip || tiState.status !== 'ready') return;
  const ta = tiTextarea();
  const index = tiOffsetFromPoint(x, y);
  if (index < 0) { tip.classList.remove('open'); return; }

  const text = ta.value;
  if (!tiIsNameChar(text[index] || '')) { tip.classList.remove('open'); return; }
  // Aim at the inside of the word: the engine looks up the token the offset
  // falls in, and the first character of a method is also the end of the dot
  // before it.
  let start = index;
  while (start > 0 && tiIsNameChar(text[start - 1])) start--;

  const info = tiState.api.hover(text, tiState.api.byteOffsetOf(text, start + 1));
  if (!info.found) { tip.classList.remove('open'); return; }

  const head = info.isMethod
    ? info.signature
    : (info.name + ': ' + (info.type || 'untyped'));
  const doc = tiState.api.pickLang(info.doc);
  tip.innerHTML = '';
  const line = document.createElement('div');
  line.className = 'ti-hover-head';
  line.textContent = head;
  tip.appendChild(line);
  if (doc) {
    const body = document.createElement('div');
    body.className = 'ti-hover-doc';
    body.textContent = doc;
    tip.appendChild(body);
  }
  tip.classList.add('open');
  const width = tip.offsetWidth;
  tip.style.left = Math.min(x + 12, window.innerWidth - width - 8) + 'px';
  tip.style.top = (y + 18) + 'px';
}

function tiHideHover() {
  clearTimeout(tiState.timers.hover);
  const tip = document.getElementById('tiHover');
  if (tip) tip.classList.remove('open');
}

// ---------------------------------------------------------- diagnostics

function tiScheduleDiagnose() {
  if (!tiState.enabled) return;
  clearTimeout(tiState.timers.diagnose);
  tiState.timers.diagnose = setTimeout(tiDiagnose, TI_DIAGNOSE_DELAY_MS);
}

function tiDiagnose() {
  if (tiState.status !== 'ready') return;
  const ta = tiTextarea();
  if (!ta) return;
  const found = tiState.api.diagnose(ta.value);
  tiState.diagnostics = found.tooLarge ? [] : found;
  tiDrawDiagnostics();

  const chip = document.getElementById('tiProblems');
  if (!chip) return;
  if (found.tooLarge) {
    chip.textContent = 'file too large to check';
    chip.className = 'ti-problems';
    return;
  }
  chip.textContent = tiState.diagnostics.length
    ? tiState.diagnostics.length + ' problem' + (tiState.diagnostics.length > 1 ? 's' : '')
    : 'no problems';
  chip.className = 'ti-problems' + (tiState.diagnostics.length ? ' err' : '');
  chip.title = tiState.diagnostics
    .map((d) => 'line ' + (d.startY + 1) + ': ' + d.message)
    .join('\n');
}

// Underline what each diagnostic is about. One box per rectangle, because a
// wrapped line hands back one rectangle per visual row.
function tiDrawDiagnostics() {
  const marks = document.getElementById('tiMarks');
  const ta = tiTextarea();
  if (!marks || !ta) return;
  marks.innerHTML = '';
  if (!tiState.diagnostics.length) return;

  const text = ta.value;
  const map = tiTextNodes();
  const stack = marks.parentElement.getBoundingClientRect();

  tiState.diagnostics.forEach((d) => {
    const from = tiIndexOfPosition(text, d.startY, d.startX);
    let to = tiIndexOfPosition(text, d.endY, d.endX);
    if (to <= from) to = Math.min(from + 1, text.length);
    const range = tiRangeFor(map, from, to);
    if (!range) return;
    for (const rect of range.getClientRects()) {
      if (!rect.width) continue;
      const box = document.createElement('div');
      box.className = 'ti-mark';
      box.style.left = (rect.left - stack.left) + 'px';
      box.style.top = (rect.top - stack.top) + 'px';
      box.style.width = rect.width + 'px';
      box.style.height = rect.height + 'px';
      box.title = d.message;
      marks.appendChild(box);
    }
  });
}

// ------------------------------------------------------------- F1 help

async function tiShowHelp() {
  if (!tiState.enabled) return;
  const api = await tiEnsureEngine();
  if (!api) return;
  const panel = document.getElementById('tiHelp');
  const body = document.getElementById('tiHelpBody');
  const title = document.getElementById('tiHelpTitle');
  if (!panel || !body) return;

  const ta = tiTextarea();
  const text = ta.value;
  let name = '';
  if (tiState.open && tiState.items[tiState.index]) {
    name = tiState.items[tiState.index].label;
  } else {
    let start = ta.selectionStart;
    while (start > 0 && tiIsNameChar(text[start - 1])) start--;
    let end = ta.selectionStart;
    while (end < text.length && tiIsNameChar(text[end])) end++;
    name = text.slice(start, end);
  }

  tiState.helpOpen = true;
  panel.classList.add('open');
  title.textContent = name || 'Help';
  if (!name) {
    body.textContent = 'Put the caret on a name and press F1.';
    return;
  }
  body.textContent = 'loading...';
  let page = null;
  try {
    page = await api.helpFor(name);
  } catch (e) {
    body.textContent = 'help.json could not be read (' + e.message + '). Run `rake ti:wasm`.';
    return;
  }
  if (!page) {
    body.textContent = 'No long help for "' + name + '" yet.';
    return;
  }
  body.innerHTML = tiRenderMarkdown(page.text);
}

function tiCloseHelp() {
  tiState.helpOpen = false;
  const panel = document.getElementById('tiHelp');
  if (panel) panel.classList.remove('open');
}

// Enough markdown for what gen_help writes: headings, fenced code and
// paragraphs. Text is escaped first, so a page can never inject markup.
function tiRenderMarkdown(source) {
  const escape = (s) => s.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
  const out = [];
  const lines = source.split('\n');
  let code = null;
  for (const line of lines) {
    const fence = line.trim().startsWith('```');
    if (fence) {
      if (code === null) { code = []; } else { out.push('<pre><code>' + escape(code.join('\n')) + '</code></pre>'); code = null; }
      continue;
    }
    if (code !== null) { code.push(line); continue; }
    const heading = /^(#{1,6})\s+(.*)$/.exec(line);
    if (heading) {
      const level = Math.min(heading[1].length + 2, 6);
      out.push('<h' + level + '>' + escape(heading[2]) + '</h' + level + '>');
    } else if (line.trim() === '') {
      out.push('');
    } else {
      out.push('<p>' + escape(line) + '</p>');
    }
  }
  if (code !== null) out.push('<pre><code>' + escape(code.join('\n')) + '</code></pre>');
  return out.join('\n');
}

// --------------------------------------------------------------- wiring

// app.js calls these; everything else in this file hangs off them.
function tiEditorOpened(name) {
  // The engine infers Ruby, so type support is Ruby-only. Other editable files
  // (.bas, .lua, .txt, ...) open in the editor but get no completion, hover,
  // diagnostics or help -- the guards in the trigger functions key off this.
  tiState.enabled = typeof name === 'string' && name.endsWith('.rb');
  tiCloseHelp();
  tiCloseCompletion();
  tiHideHover();
  tiState.diagnostics = [];
  tiDrawDiagnostics();
  const chip = document.getElementById('tiProblems');
  if (chip) { chip.textContent = ''; chip.className = 'ti-problems'; }
  if (!tiState.enabled) {
    tiSetStatus('type help: off (Ruby files only)');
    return;
  }
  tiSetStatus();
  tiEnsureEngine().then((api) => {
    if (api) tiScheduleDiagnose();
  });
}

function tiEditorTextChanged() {
  tiScheduleDiagnose();
  tiDrawDiagnostics();       // keep the marks under the text they belong to
  if (tiState.open) {
    // Typing narrows the list: ask again from the new caret.
    tiOpenCompletion();
  }
}

function tiEditorClosed() {
  tiCloseCompletion();
  tiCloseHelp();
  tiHideHover();
  clearTimeout(tiState.timers.diagnose);
  tiState.diagnostics = [];
  tiDrawDiagnostics();
}

document.addEventListener('DOMContentLoaded', () => {
  const ta = tiTextarea();
  if (!ta) return;

  const select = document.getElementById('tiLangSelect');
  if (select) {
    select.value = tiLanguage();
    select.addEventListener('change', () => tiSetLanguage(select.value));
  }
  const closeHelp = document.getElementById('tiHelpClose');
  if (closeHelp) closeHelp.addEventListener('click', tiCloseHelp);

  // Capture: the completion list has first claim on Enter, Tab, the arrows and
  // Escape, and app.js closes the whole editor on Escape.
  ta.addEventListener('keydown', (e) => {
    if ((e.ctrlKey || e.metaKey) && e.code === 'Space') {
      e.preventDefault();
      tiOpenCompletion();
      return;
    }
    if (e.key === 'F1') {
      e.preventDefault();
      tiShowHelp();
      return;
    }
    if (!tiState.open) return;
    if (e.key === 'ArrowDown') { e.preventDefault(); tiMoveCompletion(1); }
    else if (e.key === 'ArrowUp') { e.preventDefault(); tiMoveCompletion(-1); }
    else if (e.key === 'Enter' || e.key === 'Tab') { e.preventDefault(); tiAcceptCompletion(); }
    else if (e.key === 'Escape') { e.preventDefault(); e.stopPropagation(); tiCloseCompletion(); }
  }, true);

  ta.addEventListener('blur', () => { tiCloseCompletion(); tiHideHover(); });
  ta.addEventListener('mousemove', tiScheduleHover);
  ta.addEventListener('mouseleave', tiHideHover);
  ta.addEventListener('keyup', (e) => {
    if (e.key === 'Escape') return;
    tiScheduleSignature();
    if (tiState.open) tiPlaceCompletion();
  });
  ta.addEventListener('click', tiScheduleSignature);
  ta.addEventListener('scroll', () => {
    tiHideHover();
    tiDrawDiagnostics();
    if (tiState.open) tiPlaceCompletion();
  }, { passive: true });

  window.addEventListener('resize', () => {
    tiDrawDiagnostics();
    if (tiState.open) tiPlaceCompletion();
  });
});
