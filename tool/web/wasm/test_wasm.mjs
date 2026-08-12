/*
 * Does the browser engine answer the way the device one does?
 *
 * This runs the WebAssembly module in Node, with no browser involved: it is
 * the middle step of the three the P7 instruction asks for (rake ti:test
 * covers the inference itself, Playwright covers the page). What it checks is
 * that the module built by `rake ti:wasm` is wired correctly -- exports
 * reachable, source buffer in the right place, strings coming back as UTF-8,
 * and the FMRB database inside it.
 *
 * usage: node tool/web/wasm/test_wasm.mjs   (after rake ti:wasm)
 */
import { existsSync } from "node:fs";
import { fileURLToPath, pathToFileURL } from "node:url";
import { dirname, join } from "node:path";

const here = dirname(fileURLToPath(import.meta.url));
const modulePath = join(here, "ti.js");

if (!existsSync(modulePath)) {
  console.error("ti.js is missing -- run `rake ti:wasm` first");
  process.exit(1);
}

const { createTi } = await import("../js/ti.js");
const ti = await createTi({ moduleUrl: pathToFileURL(modulePath).href });

let failures = 0;

function check(name, ok, detail) {
  if (ok) {
    console.log(`ok   ${name}`);
  } else {
    failures++;
    console.log(`FAIL ${name}${detail ? ` -- ${detail}` : ""}`);
  }
}

function labels(list) {
  return list.map((item) => item.label);
}

/* 1. A local with a literal type: the integer methods follow the dot. */
{
  const source = "n = 42\nn.";
  const items = ti.suggest(source, source.length);
  check("Integer methods after `n = 42; n.`",
        labels(items).includes("abs") && labels(items).includes("times"),
        labels(items).slice(0, 8).join(", "));

  const abs = items.find((item) => item.label === "abs");
  check("candidate carries its signature", !!abs && abs.detail.includes("Integer"),
        abs && abs.detail);
  /* The standard classes are bilingual too, not only the FMRB API. */
  check("Integer#abs has both languages",
        !!abs && ti.pickLang(abs.doc, "ja") === "絶対値を返す" &&
        ti.pickLang(abs.doc, "en") === "Get absolute value", abs && abs.doc);
}

/* 2. The FMRB API: @gfx inside an app class, declared in sig/fmrb_app.rbs. */
{
  const source = [
    "class MyApp < FmrbApp",
    "  def on_draw",
    "    @gfx.draw_",
    "  end",
    "end",
  ].join("\n");
  const cursor = source.indexOf("@gfx.draw_") + "@gfx.draw_".length;
  const items = ti.suggest(source, cursor);
  check("draw_* on @gfx inside an FmrbApp subclass",
        labels(items).includes("draw_text") && labels(items).includes("draw_rect"),
        labels(items).join(", "));

  const drawText = items.find((item) => item.label === "draw_text");
  check("doc comment comes through", !!drawText && drawText.doc.length > 0,
        drawText && drawText.doc);
  check("both languages are in the doc string",
        !!drawText && drawText.doc.includes("<<en>>"), drawText && drawText.doc);
}

/* 3. The two languages, split the way the editor splits them (P4.6). */
{
  check("ja side of the marker", ti.pickLang("文字列を描く <<en>> draw a string", "ja")
        === "文字列を描く");
  check("en side of the marker", ti.pickLang("文字列を描く <<en>> draw a string", "en")
        === "draw a string");
  check("no marker means one language for both",
        ti.pickLang("説明だけ", "en") === "説明だけ");
}

/* 4. Hover: the type of a variable, and the signature of a method. */
{
  const source = "s = \"abc\"\ns.upcase";
  const info = ti.hover(source, source.indexOf("upcase") + 2);
  check("hover finds the method", info.found && info.isMethod, JSON.stringify(info));
  check("hover reports the signature",
        info.found && info.signature.includes("String"), info.signature);
}

/* 5. Diagnostics: a wrong argument type is one finding, on the line it is on. */
{
  const source = [
    "class MyApp < FmrbApp",
    "  def on_draw",
    "    @gfx.draw_text(\"x\", 1, \"hello\", 255)",
    "  end",
    "end",
  ].join("\n");
  const found = ti.diagnose(source);
  check("one diagnostic for the wrong argument type", found.length === 1,
        JSON.stringify(found));
  check("it points at line 3 (0-based 2)", found.length > 0 && found[0].startY === 2,
        found.length > 0 && String(found[0].startY));

  const clean = source.replace("\"x\"", "1");
  check("no diagnostic once it is right", ti.diagnose(clean).length === 0,
        JSON.stringify(ti.diagnose(clean)));
}

/* 6. Signature help: which argument the cursor is on. */
{
  const source = [
    "class MyApp < FmrbApp",
    "  def on_draw",
    "    @gfx.draw_text(1, 2, ",
    "  end",
    "end",
  ].join("\n");
  const call = ti.callContext(source, source.indexOf("draw_text(1, 2, ") + "draw_text(1, 2, ".length);
  check("call context found", call.found, JSON.stringify(call));
  check("cursor is on the third argument", call.found && call.argumentIndex === 2,
        call.found && String(call.argumentIndex));
}

/* 7. Non-ASCII source: byte offsets and character columns stay apart. */
{
  const source = "# にほんご\nn = 1\nn.";
  const items = ti.suggest(source, new TextEncoder().encode(source).length);
  check("a multibyte comment does not move the cursor",
        labels(items).includes("abs"), labels(items).slice(0, 5).join(", "));
}

console.log(failures === 0 ? "\nall checks passed" : `\n${failures} check(s) failed`);
process.exit(failures === 0 ? 0 : 1);
