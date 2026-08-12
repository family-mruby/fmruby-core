# Family mruby Console (tool/web)

Web Bluetooth でつながるブラウザ側の道具。ファイル管理、ログ、スプライト /
マップエディタ、debugd、そしてテキストエディタの**型支援**。

## 開くとき

**http で配信して開く**。

```
python3 -m http.server 8080 --directory tool/web   # か好きな静的サーバ
```

`file://` でも従来の機能は動くが、**型支援だけは動かない**。ブラウザが
`file://` から ES モジュール (js/ti.js) を読めないため。その場合はエディタの
下端にその旨が出る。

## エディタの型支援 (P7)

実機のエディタと同じ推論エンジン (picoruby-ti) を WebAssembly にしたもの。
ブラウザの中だけで完結し、**デバイスにつながっていなくても効く**。

- **Ctrl+Space** — 補完。上下で選び、Enter/Tab で確定、Esc で閉じる。
  選んだ候補の説明が一覧の下に出る。
- **マウスを重ねる** — その語の型かシグネチャ。
- **入力が止まると** — 引数の型が合わない等の指摘に赤い下線。件数は下端。
- **F1** — カーソル位置の名前の詳しい説明 (sig/ の長文ヘルプ)。
- **Docs (日本語/English)** — 説明文の言語。初期値はブラウザの言語。

元は 1 か所、リポジトリの `sig/*.rbs`。ファームウェアの型 db も F1 ヘルプも
同じ場所から作られる。

### 用意する

```
source ~/emsdk/emsdk_env.sh   # emscripten
rake ti:wasm                  # tool/web/wasm/ti.js + ti.wasm + help.json
```

生成物はコミットしない (`.gitignore`)。sig/ を直したら `rake ti:wasm` を
やり直す。

### 検証 (ブラウザ操作なしで通る)

```
node tool/web/wasm/test_wasm.mjs   # WASM モジュールの動作 (Node)
node tool/web/test/ui.mjs          # ページの操作 (Playwright headless)
```

`ui.mjs` は静的サーバを立てて headless Chromium で打鍵し、スクリーンショットを
`tool/web/test/shots/` に残す。`npm i` で playwright が要る。
