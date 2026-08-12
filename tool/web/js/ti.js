/*
 * Type support in the browser: the same engine the editor runs on the device
 * (picoruby-ti), compiled to WebAssembly by `rake ti:wasm`.
 *
 * This module is the JavaScript half of tool/web/wasm/ti_wasm.c. It owns the
 * conversions the C side cannot do: text to UTF-8 bytes (the engine counts in
 * bytes, JavaScript strings count in UTF-16 units), and a doc comment to the
 * language the reader asked for.
 *
 * The calls are synchronous once the module is loaded. Loading is the only
 * asynchronous part, so it is done once through createTi(); after that a
 * completion is an ordinary function call, which is what a keystroke needs.
 *
 * Used from the page through window.FmrbTi (see the bottom of this file) and
 * from Node by tool/web/wasm/test_wasm.mjs.
 */

/* One marker carries both languages in a doc comment (sig/README.md). The
   database and the help pages keep it as ordinary text, so this is the only
   place in the browser that decides which half a reader sees -- the same
   arrangement as ti_ui.rb on the device. */
export const TI_LANG_MARK = "<<en>>";

/* The half of a doc comment written in `lang`. A comment without the marker is
   shown as it is, in either language: that is what keeps the Japanese-only
   signatures working while they are being translated. */
export function pickLang(text, lang) {
  const s = String(text ?? "");
  const i = s.indexOf(TI_LANG_MARK);
  if (i < 0) return s;
  return lang === "en"
    ? s.slice(i + TI_LANG_MARK.length).trim()
    : s.slice(0, i).trim();
}

/* Long help pages are split by a line holding nothing but the marker. */
export function pickLangBlock(text, lang) {
  const s = String(text ?? "");
  const lines = s.split("\n");
  const at = lines.findIndex((line) => line.trim() === TI_LANG_MARK);
  const half = at < 0
    ? lines
    : (lang === "en" ? lines.slice(at + 1) : lines.slice(0, at));
  /* The summary line inside a page is inline, like every other summary. */
  return half.map((line) => (line.includes(TI_LANG_MARK) ? pickLang(line, lang) : line))
             .join("\n")
             .trim();
}

const FIELD_LABEL = 0;
const FIELD_DETAIL = 1;
const FIELD_DOC = 2;

const HOVER_NAME = 0;
const HOVER_TYPE = 1;
const HOVER_SIGNATURE = 2;
const HOVER_DOC = 3;

const DIAG_START_Y = 0;
const DIAG_START_X = 1;
const DIAG_END_Y = 2;
const DIAG_END_X = 3;

const CALL_SIGNATURE = 0;
const CALL_ARG_NAME = 1;
const CALL_ARG_TYPE = 2;

/* The document is above what the engine will look at (tw_suggest returns it). */
export const TI_TOO_LARGE = -4;

const encoder = new TextEncoder();

/* Byte offset of a UTF-16 index, which is what the engine wants for a cursor. */
export function byteOffsetOf(text, index) {
  return encoder.encode(text.slice(0, index)).length;
}

export async function createTi(options = {}) {
  const moduleUrl = options.moduleUrl ?? new URL("../wasm/ti.js", import.meta.url).href;
  const helpUrl = options.helpUrl ?? new URL("../wasm/help.json", import.meta.url).href;

  const factory = (await import(/* @vite-ignore */ moduleUrl)).default;
  const wasm = await factory();

  let lang = options.lang ?? "ja";
  let help = null;

  /* Copy the source into the module's own buffer and answer where it is. The
     module owns the memory, so nothing here has to free it; a request that is
     too big gets no buffer at all. */
  function place(text) {
    const bytes = encoder.encode(text);
    const ptr = wasm._tw_source_buffer(bytes.length);
    if (!ptr) return null;
    wasm.HEAPU8.set(bytes, ptr);
    wasm.HEAPU8[ptr + bytes.length] = 0;
    return { ptr, length: bytes.length };
  }

  const str = (ptr) => wasm.UTF8ToString(ptr);

  const api = {
    get lang() { return lang; },
    set lang(value) { lang = value === "en" ? "en" : "ja"; },

    /* The doc half in the current language, unless one is named. */
    pickLang: (text, forced) => pickLang(text, forced ?? lang),
    pickLangBlock: (text, forced) => pickLangBlock(text, forced ?? lang),
    byteOffsetOf,

    maxSourceBytes: () => wasm._tw_max_source_bytes(),

    /* What can follow the cursor, as {label, detail, doc}. `cursor` is a byte
       offset into `text` (byteOffsetOf converts one). An empty array means the
       engine had nothing to say; tooLarge tells the caller why. */
    suggest(text, cursor) {
      const placed = place(text);
      if (!placed) return [];
      const count = wasm._tw_suggest(placed.ptr, placed.length, cursor);
      if (count <= 0) {
        return Object.assign([], { tooLarge: count === TI_TOO_LARGE });
      }
      const items = [];
      for (let i = 0; i < count; i++) {
        items.push({
          label: str(wasm._tw_suggestion(i, FIELD_LABEL)),
          detail: str(wasm._tw_suggestion(i, FIELD_DETAIL)),
          doc: str(wasm._tw_suggestion(i, FIELD_DOC)),
        });
      }
      return items;
    },

    /* What is under the cursor: a variable and its type, or a method and its
       signature. */
    hover(text, cursor) {
      const placed = place(text);
      if (!placed) return { found: false };
      const found = wasm._tw_hover(placed.ptr, placed.length, cursor);
      if (!found) return { found: false };
      return {
        found: true,
        name: str(wasm._tw_hover_field(HOVER_NAME)),
        type: str(wasm._tw_hover_field(HOVER_TYPE)),
        signature: str(wasm._tw_hover_field(HOVER_SIGNATURE)),
        doc: str(wasm._tw_hover_field(HOVER_DOC)),
        isMethod: !!wasm._tw_hover_is_method(),
      };
    },

    /* Which call the cursor is inside, and which argument it has reached. */
    callContext(text, cursor) {
      const placed = place(text);
      if (!placed) return { found: false, argumentIndex: -1 };
      const found = wasm._tw_call_context(placed.ptr, placed.length, cursor);
      if (!found) return { found: false, argumentIndex: -1 };
      return {
        found: true,
        signature: str(wasm._tw_call_field(CALL_SIGNATURE)),
        argumentName: str(wasm._tw_call_field(CALL_ARG_NAME)),
        argumentType: str(wasm._tw_call_field(CALL_ARG_TYPE)),
        argumentIndex: wasm._tw_call_argument_index(),
      };
    },

    /* Everything wrong with the text, in lines and CHARACTER columns -- the
       coordinates an editor can point at, converted in C while the bytes the
       engine measured were still around. */
    diagnose(text) {
      const placed = place(text);
      if (!placed) return [];
      const count = wasm._tw_diagnose(placed.ptr, placed.length);
      if (count <= 0) {
        return Object.assign([], { tooLarge: count === TI_TOO_LARGE });
      }
      const items = [];
      for (let i = 0; i < count; i++) {
        items.push({
          startY: wasm._tw_diagnostic_pos(i, DIAG_START_Y),
          startX: wasm._tw_diagnostic_pos(i, DIAG_START_X),
          endY: wasm._tw_diagnostic_pos(i, DIAG_END_Y),
          endX: wasm._tw_diagnostic_pos(i, DIAG_END_X),
          message: str(wasm._tw_diagnostic_message(i)),
        });
      }
      return items;
    },

    /* The long help (F1). One file for every page, fetched the first time it
       is asked for -- the page starts without it. */
    async loadHelp() {
      if (help) return help;
      const response = await fetch(helpUrl);
      if (!response.ok) throw new Error(`help.json: ${response.status}`);
      help = await response.json();
      return help;
    },

    /* The page for a name ("draw_text", or "FmrbGfx" for a class), in the
       current language. Null when nothing was written for it. */
    async helpFor(name) {
      const bundle = await api.loadHelp();
      const hit = bundle.index.find((entry) => entry[0] === name);
      if (!hit) return null;
      return { path: hit[1], text: pickLangBlock(bundle.pages[hit[1]], lang) };
    },

    async helpNames() {
      const bundle = await api.loadHelp();
      return bundle.index.map((entry) => entry[0]);
    },
  };

  return api;
}

/* The console's scripts are plain <script> files, so hand them the module
   through the window. A page served from file:// cannot load modules at all,
   which is why app.js treats this as optional and says so in the editor. */
if (typeof window !== "undefined") {
  window.FmrbTi = { createTi, pickLang, pickLangBlock, byteOffsetOf, TI_LANG_MARK };
  window.dispatchEvent(new Event("fmrb-ti-ready"));
}
