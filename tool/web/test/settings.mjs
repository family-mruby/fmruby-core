/*
 * The console's Connection settings pane, driven in headless Chromium against
 * a stand-in board.
 *
 * Web Bluetooth cannot be reached from a test, so navigator.bluetooth is
 * replaced by a small fake that speaks the file service's framing (COBS +
 * CRC32, the same functions the page uses) over an in-memory file system,
 * with the board's rules: the .tar is never read, the WiFi file and the API
 * key file (secrets.toml) only while the settings window is open, settings are written only while it is open,
 * and CMD_SETTINGS carries a WiFi summary without the password. The rules themselves are
 * tested in C (rake secret:test); this covers the page -- what it shows,
 * what it writes, and that it does not retry a refusal.
 *
 * usage: node tool/web/test/settings.mjs   (npm i playwright)
 */
import { createServer } from "node:http";
import { readFile } from "node:fs/promises";
import { mkdirSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { dirname, join, normalize, extname } from "node:path";
import { chromium } from "playwright";

const here = dirname(fileURLToPath(import.meta.url));
const webRoot = join(here, "..");
const shots = join(here, "shots");
mkdirSync(shots, { recursive: true });

const TYPES = {
  ".html": "text/html; charset=utf-8",
  ".js": "text/javascript; charset=utf-8",
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
  if (ok) console.log(`ok   ${name}`);
  else { failures++; console.log(`FAIL ${name}${detail ? ` -- ${detail}` : ""}`); }
}

// The stand-in board. Runs in the page, before the page's own scripts.
function fakeBoard() {
  const enc = new TextEncoder();
  const files = new Map();   // path -> Uint8Array
  const dirs = new Set(["/", "/home", "/etc", "/app", "/home/asterism"]);
  files.set("/etc/wifi.toml", enc.encode('[wifi]\nenable = true\nssid = "old"\npassword = "secret-old"\n'));
  files.set("/home/asterism/config.toml",
            enc.encode('# This machine\'s Asterism settings (doc/ruby_asterism)\nrouter = "tls/203.0.113.5:7447"\nname = "nano4"\n'));
  files.set("/home/asterism/old-board.tar", new Uint8Array([1, 2, 3]));
  files.set("/home/notes.txt", enc.encode("hello"));
  // An obvious fake: no real key is ever written into a test.
  files.set("/etc/secrets.toml", enc.encode('# Written by rake\n[openai]\napi_key = "sk-test-etc-0000000000000000"\n'));

  const board = { files, dirs, windowUntil: 0, setup: false, rebooted: false, commands: [], sent: [] };
  window.__board = board;

  // The setup mode holds the window open (reported as a long time left).
  const left = () => board.setup ? 86399 : Math.max(0, Math.ceil((board.windowUntil - Date.now()) / 1000));
  const settingsPath = (p) => {
    const s = p.split("/").filter(x => x);
    return s[0] === "home" && s[1] === "asterism";
  };
  const writable = (p) => (!isSecretPath(p) && !settingsPath(p)) || left() > 0;
  const isWifi = (p) => /^\/(home|etc)\/(wifi|secrets)\.toml/.test(p);
  const denyClosed = { ok: false, denied: true, closed: true,
                       err: "The board is not accepting settings: open BLE Setup in its menu" };

  function handle(cmd, params, bin) {
    board.commands.push({ cmd, path: params.path });
    switch (cmd) {
    case 0x12: {  // LS
      const dir = params.path === "/" ? "" : params.path;
      if (!dirs.has(params.path)) return [{ ok: false, err: "Cannot open directory" }];
      const entries = [];
      for (const d of dirs) {
        if (d !== "/" && d.startsWith(dir + "/") && !d.slice(dir.length + 1).includes("/")) {
          entries.push({ n: d.slice(dir.length + 1), t: "d", s: 0 });
        }
      }
      for (const [p, data] of files) {
        if (p.startsWith(dir + "/") && !p.slice(dir.length + 1).includes("/")) {
          entries.push({ n: p.slice(dir.length + 1), t: "f", s: data.length });
        }
      }
      return [{ ok: true, entries }];
    }
    case 0x15: return [{ ok: true, total: 1 << 20, free: 1 << 19 }];
    case 0x14: dirs.add(params.path); return [{ ok: true }];
    case 0x17: {
      const src = files.has("/home/wifi.toml") ? "/home/wifi.toml" : files.has("/etc/wifi.toml") ? "/etc/wifi.toml" : "";
      const text = src ? new TextDecoder().decode(files.get(src)) : "";
      const ssid = src ? (settingsTomlValue(text, "ssid") || "") : "";
      const meta = { ok: true, open: left() > 0, left: left() };
      if (board.setup) { meta.setup = true; meta.until_restart = true; }
      meta.wifi = { src, on: /enable\s*=\s*true/.test(text), ssid, conn: !board.setup, ip: board.setup ? "" : "192.0.2.7" };
      return [meta];
    }
    case 0x18: {  // REBOOT, setup mode only
      if (!board.setup) return [{ ok: false, denied: true, err: "The board restarts from the console only in the setup mode" }];
      board.rebooted = true;
      return [{ ok: true, restart_ms: 500 }];
    }
    case 0x21: {  // GET
      if (isWifi(params.path) && left() === 0) return [denyClosed];
      if (isSecretPath(params.path) && !isWifi(params.path)) {
        return [{ ok: false, denied: true, err: "This file holds a key and is not read over BLE" }];
      }
      const data = files.get(params.path);
      if (!data) return [{ ok: false, err: "Cannot open file" }];
      const chunk = data.slice(params.off || 0, (params.off || 0) + 2048);
      return [{ ok: true, eof: chunk.length < 2048, bin: chunk.length }, chunk];
    }
    case 0x22: {  // PUT
      if (!writable(params.path)) return [denyClosed];
      const old = params.off ? (files.get(params.path) || new Uint8Array(0)) : new Uint8Array(0);
      const out = new Uint8Array(Math.max(old.length, params.off + bin.length));
      out.set(old.slice(0, params.off));
      out.set(bin, params.off);
      files.set(params.path, out);
      return [{ ok: true, n: bin.length }];
    }
    case 0x13: {  // RM
      if (!writable(params.path)) return [denyClosed];
      files.delete(params.path);
      return [{ ok: true }];
    }
    default: return [{ ok: false, err: "Unknown command" }];
    }
  }

  function respond(tx, meta, bin) {
    board.sent.push({ open: left() > 0, text: JSON.stringify(meta) + (bin ? new TextDecoder().decode(bin) : "") });
    const json = enc.encode(JSON.stringify(meta));
    const binLen = bin ? bin.length : 0;
    const body = new Uint8Array(3 + json.length + binLen);
    body[0] = 0; body[1] = json.length >> 8; body[2] = json.length & 0xff;
    body.set(json, 3);
    if (bin) body.set(bin, 3 + json.length);
    const crc = crc32(body);
    const packet = new Uint8Array(body.length + 4);
    packet.set(body);
    packet[body.length] = crc >>> 24; packet[body.length + 1] = (crc >>> 16) & 0xff;
    packet[body.length + 2] = (crc >>> 8) & 0xff; packet[body.length + 3] = crc & 0xff;
    const encoded = cobsEncode(packet);
    const frame = new Uint8Array(encoded.length + 1);
    frame.set(encoded);
    setTimeout(() => {
      for (let off = 0; off < frame.length; off += 180) {
        const part = frame.slice(off, off + 180);
        tx.listeners.forEach(fn => fn({ target: { value: new DataView(part.buffer) } }));
      }
    }, 5);
  }

  const tx = {
    listeners: [],
    async startNotifications() {},
    addEventListener(_, fn) { this.listeners.push(fn); },
  };
  let rxBuf = [];
  const rx = {
    async writeValueWithoutResponse(chunk) {
      for (const b of chunk) {
        if (b !== 0) { rxBuf.push(b); continue; }
        const raw = cobsDecode(new Uint8Array(rxBuf));
        rxBuf = [];
        if (raw.length < 7) { respond(tx, { ok: false, err: "Frame too short" }); continue; }
        const jsonLen = (raw[1] << 8) | raw[2];
        const params = JSON.parse(new TextDecoder().decode(raw.slice(3, 3 + jsonLen)));
        const bin = raw.slice(3 + jsonLen, raw.length - 4);
        const [meta, out] = handle(raw[0], params, bin);
        respond(tx, meta, out);
      }
    },
  };
  const device = {
    gatt: {
      connected: true,
      async connect() {
        return {
          async getPrimaryService(uuid) {
            if (uuid !== SERVICE_UUID) throw new Error("no such service");
            return { async getCharacteristic(u) { return u === FS_RX_UUID ? rx : tx; } };
          },
        };
      },
      disconnect() {},
    },
    addEventListener() {},
  };
  Object.defineProperty(navigator, "bluetooth", {
    value: { async requestDevice() { return device; } },
  });
}

const browser = await chromium.launch();
const page = await browser.newPage({ viewport: { width: 900, height: 1100 } });
const pageErrors = [];
page.on("pageerror", (error) => pageErrors.push(error.message));
await page.addInitScript(fakeBoard);
await page.goto(origin, { waitUntil: "load" });

const text = (id) => page.evaluate((i) => document.getElementById(i).textContent, id);
const fileText = (p) => page.evaluate((path) => {
  const d = window.__board.files.get(path);
  return d ? new TextDecoder().decode(d) : null;
}, p);
const disabled = (sel) => page.evaluate((s) =>
  [...document.querySelectorAll(s)].every(b => b.disabled), sel);

// ---- connect, Files tab ----
await page.click("#btnConnect");
await page.waitForFunction(() => document.getElementById("statusBadge").textContent === "Connected");
await page.evaluate(() => navigateTo("/home"));
await page.waitForFunction(() => document.getElementById("fileList").textContent.includes("asterism"));
await page.evaluate(() => navigateTo("/etc"));
await page.waitForFunction(() => document.getElementById("fileList").textContent.includes("wifi.toml"));
const etcRow = await page.evaluate(() => document.getElementById("fileList").innerHTML);
check("files: /etc/wifi.toml is marked secret", etcRow.includes("secret-tag"), etcRow.slice(0, 200));
check("files: no download or edit button on a secret", !etcRow.includes(">DL<") && !etcRow.includes(">Edit<"));
check("files: /etc/secrets.toml is marked secret", await page.evaluate(() =>
  isSecretPath("/etc/secrets.toml") && isSecretPath("/home/secrets.toml") && isSecretPath("/flash/home/secrets.toml.part") &&
  !isSecretPath("/home/services.toml") && !isSecretPath("/home/sub/secrets.toml")));

// A GET of a secret is refused, and the refusal is not retried.
const getResult = await page.evaluate(async () => {
  const before = window.__board.commands.length;
  try { await deviceReadFile("/etc/wifi.toml"); return "read"; }
  catch (e) { return e.message + "|" + (window.__board.commands.length - before); }
});
check("GET of the WiFi file is refused once while closed", getResult.endsWith("|1") && getResult.includes("not accepting"), getResult);
const keyGet = await page.evaluate(async () => {
  try { await deviceReadFile("/etc/secrets.toml"); return "read"; } catch (e) { return e.message; }
});
check("GET of the key file is refused while closed", keyGet.includes("not accepting"), keyGet);

// ---- Settings tab, window closed ----
await page.click("#tabSettings");
await page.waitForFunction(() => document.getElementById("setCfgNow").textContent.includes("router"));
check("closed: says so", (await text("setWindow")).includes("not accepting settings"), await text("setWindow"));
check("closed: write buttons disabled", await disabled("[data-needs-settings]"));
check("shows WiFi from the firmware", (await text("setWifiNow")).includes("/etc/wifi.toml"), await text("setWifiNow"));
check("shows the SSID and the link", (await text("setWifiNow")).includes('SSID "old"') && (await text("setWifiNow")).includes("192.0.2.7"),
      await text("setWifiNow"));
check("fills the SSID field", (await page.inputValue("#setSsid")) === "old");
check("password field empty, said to be set", (await page.inputValue("#setPassword")) === "" &&
      (await text("setPwNow")) === "set (not shown)", await text("setPwNow"));
check("the password never sent while closed",
      await page.evaluate(() => window.__board.sent.filter(r => !r.open).every(r => !r.text.includes("secret-old"))));
check("closed: the API key is said to be set, from /etc", (await text("setKeyNow")).includes("set (the firmware's /etc/secrets.toml"),
      await text("setKeyNow"));
check("closed: the API key field is empty", (await page.inputValue("#setApiKey")) === "" &&
      (await text("setKeyLoaded")) === "set (not shown)", await text("setKeyLoaded"));
check("the API key never sent while closed",
      await page.evaluate(() => window.__board.sent.filter(r => !r.open).every(r => !r.text.includes("sk-test-etc"))));
check("shows the current .tar by name", (await text("setTarNow")).includes("old-board.tar"), await text("setTarNow"));
check("fills the Asterism form from config.toml",
      (await page.inputValue("#setRouter")) === "tls/203.0.113.5:7447" && (await page.inputValue("#setName")) === "nano4");
await page.screenshot({ path: join(shots, "settings_closed.png"), fullPage: true });

// The board refuses even when the page is made to write anyway.
await page.fill("#setPassword", "");
await page.evaluate(() => settingsWriteWifi());
check("closed: empty password refused in the page", (await text("setWifiMsg")) === "enter the password", await text("setWifiMsg"));
await page.fill("#setSsid", "Home Net");
await page.fill("#setPassword", "password1");
await page.evaluate(() => settingsWriteWifi());
await page.waitForFunction(() => document.getElementById("setWifiMsg").textContent.startsWith("Not written"));
check("closed: the board's refusal is shown", (await text("setWifiMsg")).includes("not accepting settings"), await text("setWifiMsg"));
check("closed: nothing written", (await fileText("/home/wifi.toml")) === null);

// ---- the window opens on the board ----
await page.fill("#setPassword", "");
await page.evaluate(() => { window.__board.windowUntil = Date.now() + 300 * 1000; });
await page.waitForFunction(() => document.getElementById("setWindow").textContent.includes("accepting settings:"),
                           null, { timeout: 8000 });
check("open: countdown shown", /[45]:\d\d left/.test(await text("setWindow")), await text("setWindow"));
check("open: write buttons enabled", !(await disabled("[data-needs-settings]")));
await page.waitForFunction(() => document.getElementById("setPassword").value !== "", null, { timeout: 8000 }).catch(() => {});
check("open: the current password is loaded", (await page.inputValue("#setPassword")) === "secret-old" &&
      (await text("setPwNow")).startsWith("loaded"), await text("setPwNow"));
const tarGet = await page.evaluate(async () => {
  try { await deviceReadFile("/home/asterism/old-board.tar"); return "read"; } catch (e) { return e.message; }
});
check("open: the .tar is still not read", tarGet.includes("holds a key"), tarGet);

await page.waitForFunction(() => document.getElementById("setApiKey").value !== "", null, { timeout: 8000 }).catch(() => {});
check("open: the current API key is loaded", (await page.inputValue("#setApiKey")) === "sk-test-etc-0000000000000000" &&
      (await text("setKeyLoaded")).startsWith("loaded"), await text("setKeyLoaded"));
check("open: the key field is hidden until show", (await page.getAttribute("#setApiKey", "type")) === "password");
await page.check("#setKeyShow");
check("open: show makes it visible", (await page.getAttribute("#setApiKey", "type")) === "text");
await page.uncheck("#setKeyShow");

// An empty key field keeps the loaded key: the /etc key is copied to /home.
await page.fill("#setApiKey", "");
await page.click("text=Write API key");
await page.waitForFunction(() => document.getElementById("setKeyMsg").textContent.startsWith("API key written"));
check("empty key keeps the loaded one", (await fileText("/home/secrets.toml")) ===
  "# This machine's secrets, written by the Family mruby Console.\n" +
  "# Kept like the WiFi password: never read over HTTP, over BLE only in BLE Setup.\n" +
  "[openai]\napi_key = \"sk-test-etc-0000000000000000\"\n", JSON.stringify(await fileText("/home/secrets.toml")));
await page.waitForFunction(() => document.getElementById("setKeyNow").textContent === "set (/home/secrets.toml)",
                           null, { timeout: 8000 }).catch(() => {});
check("now shows the key from /home", (await text("setKeyNow")) === "set (/home/secrets.toml)", await text("setKeyNow"));

// A new key replaces api_key and keeps the rest of the file.
await page.evaluate(() => {
  window.__board.files.set("/home/secrets.toml", new TextEncoder().encode(
    "# mine\n[other]\ntoken = \"t-0\"\n\n[openai]\n# old one below\napi_key = \"sk-test-old-2222\"\n"));
});
await page.fill("#setApiKey", "sk-test-new-1111111111111111");
await page.click("text=Write API key");
await page.waitForFunction(() => document.getElementById("setKeyMsg").textContent.startsWith("API key written"));
check("a new key keeps the other lines", (await fileText("/home/secrets.toml")) ===
  "# mine\n[other]\ntoken = \"t-0\"\n\n[openai]\n# old one below\napi_key = \"sk-test-new-1111111111111111\"\n",
  JSON.stringify(await fileText("/home/secrets.toml")));
await page.fill("#setApiKey", "sk-test with space");
await page.click("text=Write API key");
check("a key with a space is refused in the page", (await text("setKeyMsg")).includes("space"), await text("setKeyMsg"));
check("secrets.toml writer and reader", await page.evaluate(() => {
  const a = settingsSecretsToml("[other]\nx = \"1\"\n", "sk-test-a");
  const b = settingsSecretsToml("[openai]\nmodel = \"m\"\n[z]\n", "sk-test-b");
  return settingsSecretsValue(a, "openai", "api_key") === "sk-test-a" && a.startsWith("[other]\nx = \"1\"\n") &&
         b === "[openai]\napi_key = \"sk-test-b\"\nmodel = \"m\"\n[z]\n" &&
         settingsSecretsValue("api_key = \"no\"\n", "openai", "api_key") === null &&
         settingsCheckApiKey("") !== "" && settingsCheckApiKey("sk-\"x") !== "" && settingsCheckApiKey("sk-test-ok") === "";
}));

// An empty password keeps the one loaded from the board.
await page.fill("#setPassword", "");
await page.click("text=Write WiFi");
await page.waitForFunction(() => document.getElementById("setWifiMsg").textContent.startsWith("WiFi written"));
check("empty password keeps the loaded one", (await fileText("/home/wifi.toml")).includes('password = "secret-old"'),
      await fileText("/home/wifi.toml"));

// WiFi
await page.fill("#setSsid", "Home \"Net\"");
await page.fill("#setPassword", "pa\\ss word1");
await page.click("text=Write WiFi");
await page.waitForFunction(() => document.getElementById("setWifiMsg").textContent.startsWith("WiFi written"));
const wifi = await fileText("/home/wifi.toml");
check("wifi.toml content", wifi ===
  "# This machine's WiFi, written by the Family mruby Console\n[wifi]\nenable = true\n" +
  "ssid = \"Home \\\"Net\\\"\"\npassword = \"pa\\\\ss word1\"\n", JSON.stringify(wifi));

await page.waitForFunction(() => !window.settingsBusy, null, { timeout: 8000 }).catch(() => {});
await page.waitForFunction(() => document.getElementById("setWifiNow").textContent.includes('SSID "Home "Net"" from /home/wifi.toml'),
                           null, { timeout: 8000 }).catch(() => {});
check("now shows WiFi from /home", (await text("setWifiNow")).includes('SSID "Home "Net"" from /home/wifi.toml'), await text("setWifiNow"));
check("short password refused in the page", await page.evaluate(() => settingsCheckWifi("a", "short") !== ""));
check("toml value reader", await page.evaluate(() =>
  settingsTomlValue('password = "pa\\\\ss \\"x\\" \\u00e9"', "password") === 'pa\\ss "x" \u00e9' &&
  settingsTomlValue("password = 'lit'", "password") === "lit" && settingsTomlValue("x = 1", "password") === null));

// Asterism
await page.fill("#setName", "Nano \"4\"");
await page.fill("#setNode", "");
await page.click("text=Write Asterism settings");
await page.waitForFunction(() => document.getElementById("setCfgMsg").textContent.startsWith("Written to"));
const cfg = await fileText("/home/asterism/config.toml");
check("config.toml content (FmrbAsterism.config_text shape)", cfg ===
  "# This machine's Asterism settings (doc/ruby_asterism)\nrouter = \"tls/203.0.113.5:7447\"\nname = \"Nano \\\"4\\\"\"\n",
  JSON.stringify(cfg));
const parsed = await page.evaluate((t) => settingsParseConfig(t), cfg);
check("config.toml reads back", parsed.router === "tls/203.0.113.5:7447" && parsed.name === "Nano \"4\"" && !("node" in parsed),
      JSON.stringify(parsed));
check("bad router refused in the page", await page.evaluate(() => settingsCheckAsterism({ router: "203.0.113.5" }) !== ""));

// Credentials
const tarBytes = Buffer.alloc(5000, 7);
await page.setInputFiles("#setTarFile", { name: "fmruby-aaaaaa.tar", mimeType: "application/x-tar", buffer: tarBytes });
await page.click("text=Write credentials");
await page.waitForFunction(() => document.getElementById("setTarMsg").textContent.startsWith("Credentials written"),
                           null, { timeout: 15000 });
const tarOk = await page.evaluate(() => {
  const d = window.__board.files.get("/home/asterism/fmruby-aaaaaa.tar");
  return d && d.length === 5000 && d.every(b => b === 7);
});
check("tar written byte for byte", tarOk);
check("the other .tar removed", !(await page.evaluate(() => window.__board.files.has("/home/asterism/old-board.tar"))));
check("tar names", await page.evaluate(() =>
  settingsTarName("C:\\fakepath\\my board.tar") === "my_board.tar" && settingsTarName("x.zip") === null &&
  settingsTarName(".tar") === null));
await page.screenshot({ path: join(shots, "settings_open.png"), fullPage: true });

// ---- the window closes on the board ----
await page.evaluate(() => { window.__board.windowUntil = 0; });
await page.waitForFunction(() => document.getElementById("setWindow").textContent.includes("not accepting"),
                           null, { timeout: 8000 });
check("closed again: buttons disabled", await disabled("[data-needs-settings]"));
check("closed again: the password is forgotten", (await page.inputValue("#setPassword")) === "");
check("closed again: the API key is forgotten", (await page.inputValue("#setApiKey")) === "");

// ---- the setup mode (doc/ruby_asterism/report/cf3.md) ----
check("normal: no Reboot button", await page.evaluate(() => document.getElementById("setReboot").hidden));
const rebootRefused = await page.evaluate(async () => (await sendCommand(CMD_REBOOT, {})).meta);
check("normal: the board refuses a reboot", rebootRefused.ok === false && rebootRefused.denied === true, JSON.stringify(rebootRefused));
await page.evaluate(() => { window.__board.setup = true; });
await page.waitForFunction(() => document.getElementById("setWindow").textContent.includes("setup mode"),
                           null, { timeout: 8000 });
check("setup: says until it restarts", (await text("setWindow")).includes("until it restarts"), await text("setWindow"));
check("setup: no countdown", !/\d:\d\d left/.test(await text("setWindow")), await text("setWindow"));
check("setup: write buttons enabled", !(await disabled("[data-needs-settings]")));
check("setup: Reboot button shown", !(await page.evaluate(() => document.getElementById("setReboot").hidden)));
await page.screenshot({ path: join(shots, "settings_setup.png"), fullPage: true });
await page.click("#setReboot");
await page.waitForFunction(() => document.getElementById("setRebootMsg").textContent.includes("restarting"),
                           null, { timeout: 8000 });
check("setup: Reboot reaches the board", await page.evaluate(() => window.__board.rebooted));
await page.evaluate(() => { window.__board.setup = false; });
await page.waitForFunction(() => document.getElementById("setReboot").hidden, null, { timeout: 8000 });
check("after setup: Reboot button hidden again", await page.evaluate(() => document.getElementById("setReboot").hidden));

// The files the console's other tabs use are unaffected.
const plain = await page.evaluate(async () => new TextDecoder().decode(await deviceReadFile("/home/notes.txt")));
check("ordinary GET as before", plain === "hello", plain);

const realErrors = pageErrors.filter(m => !/ti\.|wasm|WebAssembly/i.test(m));
check("no page errors", realErrors.length === 0, realErrors.join("; "));

await browser.close();
server.close();
console.log(failures === 0 ? "settings: all passed" : `settings: ${failures} failed`);
process.exit(failures === 0 ? 0 : 1);
