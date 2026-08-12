/*
 * The console's type support, driven in a real browser with nobody at the
 * keyboard.
 *
 * This is the third of the three steps P7 asks for: rake ti:test covers the
 * inference, tool/web/wasm/test_wasm.mjs covers the module, and this covers
 * the page -- the completion list appearing under the caret, the doc line
 * following the language switch, the diagnostics chip counting, the help panel
 * opening on F1. It serves tool/web over http (module scripts and WebAssembly
 * both need a real origin), opens it in headless Chromium, types into the
 * editor and reads the DOM back. Screenshots land in tool/web/test/shots/.
 *
 * usage: node tool/web/test/ui.mjs   (after rake ti:wasm; npm i playwright)
 */
import { createServer } from "node:http";
import { readFile } from "node:fs/promises";
import { existsSync, mkdirSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { dirname, join, normalize, extname } from "node:path";
import { chromium } from "playwright";

const here = dirname(fileURLToPath(import.meta.url));
const webRoot = join(here, "..");
const shots = join(here, "shots");

if (!existsSync(join(webRoot, "wasm/ti.wasm"))) {
  console.error("tool/web/wasm/ti.wasm is missing -- run `rake ti:wasm` first");
  process.exit(1);
}
mkdirSync(shots, { recursive: true });

const TYPES = {
  ".html": "text/html; charset=utf-8",
  ".js": "text/javascript; charset=utf-8",
  ".mjs": "text/javascript; charset=utf-8",
  ".css": "text/css; charset=utf-8",
  ".json": "application/json; charset=utf-8",
  ".wasm": "application/wasm",
};

const server = createServer(async (request, response) => {
  const path = decodeURIComponent(new URL(request.url, "http://localhost").pathname);
  const file = join(webRoot, normalize(path === "/" ? "/index.html" : path));
  if (!file.startsWith(webRoot)) { response.writeHead(403).end(); return; }
  try {
    const body = await readFile(file);
    response.writeHead(200, { "content-type": TYPES[extname(file)] || "application/octet-stream" });
    response.end(body);
  } catch {
    response.writeHead(404).end("not found");
  }
});

await new Promise((resolve) => server.listen(0, "127.0.0.1", resolve));
const origin = `http://127.0.0.1:${server.address().port}`;

let failures = 0;
function check(name, ok, detail) {
  if (ok) {
    console.log(`ok   ${name}`);
  } else {
    failures++;
    console.log(`FAIL ${name}${detail ? ` -- ${detail}` : ""}`);
  }
}

const browser = await chromium.launch();
const page = await browser.newPage({ viewport: { width: 1100, height: 800 } });
const pageErrors = [];
page.on("pageerror", (error) => pageErrors.push(error.message));

await page.goto(origin, { waitUntil: "load" });
await page.waitForFunction(() => !!window.FmrbTi);

/* Open the editor without a device: showEditor is what openEditor calls once
   the bytes have arrived. */
async function openWith(text) {
  await page.evaluate((source) => window.showEditor("demo.rb", "/demo.rb", source), text);
  await page.waitForFunction(() => document.getElementById("tiStatus").textContent.includes("ready"),
                             null, { timeout: 30000 });
  await page.click("#editorTextarea");
}

/* --- completion ------------------------------------------------------- */

await openWith("");
await page.type("#editorTextarea", "s = 1\ns.");
await page.keyboard.press("Control+Space");
await page.waitForSelector(".ti-complete.open .ti-item", { timeout: 15000 });

const labels = await page.$$eval(".ti-complete .ti-item .ti-label", (nodes) => nodes.map((n) => n.textContent));
check("the completion list offers Integer methods after `s = 1; s.`",
      labels.includes("abs") && labels.includes("times"), labels.slice(0, 10).join(", "));

const placed = await page.evaluate(() => {
  const box = document.getElementById("tiComplete").getBoundingClientRect();
  const area = document.getElementById("editorTextarea").getBoundingClientRect();
  return { boxTop: box.top, boxLeft: box.left, areaTop: area.top, areaLeft: area.left };
});
check("the list is under the caret, not off in a corner",
      placed.boxTop > placed.areaTop && placed.boxLeft >= placed.areaLeft - 1,
      JSON.stringify(placed));

await page.screenshot({ path: join(shots, "completion.png") });

/* Accepting one puts it in the text. Walk down to `abs` rather than taking
   whatever is first: the list starts with the operators. */
for (let i = 0; i < labels.indexOf("abs"); i++) await page.keyboard.press("ArrowDown");
await page.keyboard.press("Enter");
const afterAccept = await page.inputValue("#editorTextarea");
check("choosing a candidate writes it into the document",
      afterAccept === "s = 1\ns.abs", JSON.stringify(afterAccept));

/* --- the language switch ---------------------------------------------- */

const APP = [
  "class MyApp < FmrbApp",
  "  def on_draw",
  "    @gfx.draw_text",
  "  end",
  "end",
].join("\n");

async function docLineFor(lang) {
  await page.selectOption("#tiLangSelect", lang);
  await openWith(APP);
  await page.evaluate(() => {
    const ta = document.getElementById("editorTextarea");
    const at = ta.value.indexOf("draw_text") + "draw_text".length;
    ta.setSelectionRange(at, at);
  });
  await page.keyboard.press("Control+Space");
  await page.waitForSelector(".ti-complete.open .ti-item", { timeout: 15000 });
  return page.textContent("#tiCompleteDoc");
}

const ja = await docLineFor("ja");
check("the doc line is Japanese with Docs=日本語", ja.includes("文字列を描く") && !ja.includes("<<en>>"), ja);
await page.screenshot({ path: join(shots, "doc_ja.png") });

const en = await docLineFor("en");
check("the same doc line is English with Docs=English", en === "draw a string", en);
await page.screenshot({ path: join(shots, "doc_en.png") });

/* --- diagnostics ------------------------------------------------------- */

await openWith([
  "class MyApp < FmrbApp",
  "  def on_draw",
  "    @gfx.draw_text(\"x\", 1, \"hello\", 255)",
  "  end",
  "end",
].join("\n"));
await page.waitForFunction(() => document.getElementById("tiProblems").textContent.length > 0,
                           null, { timeout: 15000 });
const problems = await page.textContent("#tiProblems");
check("a wrong argument type is counted", problems === "1 problem", problems);
const marks = await page.$$eval(".ti-mark", (nodes) => nodes.length);
check("and underlined in the text", marks > 0, String(marks));
await page.screenshot({ path: join(shots, "diagnostics.png") });

/* --- hover ------------------------------------------------------------- */

await openWith("s = \"abc\"\ns.upcase\n");
const target = await page.evaluate(() => {
  const walker = document.createTreeWalker(document.getElementById("editorHighlight"), NodeFilter.SHOW_TEXT);
  while (walker.nextNode()) {
    const node = walker.currentNode;
    const at = node.nodeValue.indexOf("upcase");
    if (at < 0) continue;
    const range = document.createRange();
    range.setStart(node, at + 1);
    range.setEnd(node, at + 2);
    const rect = range.getBoundingClientRect();
    return { x: rect.left + rect.width / 2, y: rect.top + rect.height / 2 };
  }
  return null;
});
check("the highlight overlay holds the text to point at", !!target,
      "no `upcase` in the overlay");
if (target) {
  await page.mouse.move(target.x, target.y);
  await page.waitForSelector(".ti-hover.open", { timeout: 15000 });
  const tip = await page.textContent(".ti-hover");
  check("hovering a method shows its signature", tip.includes("String"), tip);
  await page.screenshot({ path: join(shots, "hover.png") });
}

/* --- F1 help ----------------------------------------------------------- */

await openWith(APP);
await page.evaluate(() => {
  const ta = document.getElementById("editorTextarea");
  const at = ta.value.indexOf("draw_text") + 2;
  ta.setSelectionRange(at, at);
});
await page.keyboard.press("F1");
await page.waitForSelector(".ti-help.open", { timeout: 15000 });
/* The panel opens first and fills in when help.json has arrived. */
await page.waitForFunction(() => {
  const text = document.getElementById("tiHelpBody").textContent;
  return text.length > 0 && text !== "loading...";
}, null, { timeout: 15000 });
const help = await page.textContent("#tiHelpBody");
check("F1 opens the long help for the name under the caret",
      help.length > 40 && !help.includes("<<en>>"), help.slice(0, 80));
await page.screenshot({ path: join(shots, "help.png") });

check("no uncaught errors on the page", pageErrors.length === 0, pageErrors.join(" | "));

await browser.close();
server.close();

console.log(failures === 0 ? `\nall checks passed (screenshots in ${shots})` : `\n${failures} check(s) failed`);
process.exit(failures === 0 ? 0 : 1);
