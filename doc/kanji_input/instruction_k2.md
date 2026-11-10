# K2 指示書: 辞書を引く部品 (C)

> 状態: 実装中 | 更新: 2026-11-11 | 正は plan.md・dict_format.md (版 1)・report/k1.md

## K2a (今やる): C の部品とホストの試験

- 新しい component (例: `components/fmrb_kanji/`) に、辞書を引く C の部品を書く。ESP32・ネイティブ版・sim で同じコード。
  - 開く: 頭 (64 バイト) を読んで印・版・crc を確かめ、字の表と塊の索引 (合わせて約 16 KB) を PSRAM に読む
    (`fmrb_sys_malloc` 系。内蔵 RAM を使わない)。ファイルは fmrb_hal のファイルの口で読む。
  - 引く: 読み (UTF-8 のひらがな) から、塊の索引を二分探索し、塊を 1 つだけ読み、候補を UTF-8 で返す。読みの先頭からの
    最長一致の口も (一致した長さを返す)。塊の読み込みの場所は呼び手が渡す (4 KB)。
  - 素の malloc を使わない (fmrb_mem.h)、esp_* を直接 include しない (CLAUDE.md の決まり)。
- ホストの試験: `tools/kanji/lookup.rb` を基準に、生成した辞書 (tools/kanji/work/out/、無ければ `rake kanji:dict`) で
  数千の読み (辞書の読み全部でもよい) の答えが一致すること、最長一致、壊れたファイル・版違いを断ること。
  `rake test` から呼べるようにし、辞書が無い環境 (CI) では理由を出して飛ばす。
- まだファームには組み込まない (ビルドを使うほかのサブがいるため。ホストの gcc だけで試す)。

## K2b (後で): ファームに組み込み、端末で測る

- 優先度の低い別の C のタスクで引く (host task では引かない)。置き場所の設定 (既定 /sd/dict/、sim は別の場所)。
- 端末での速さの測定の手順 (実機が要る)。K3 と一緒にやる。

## 触ってよい範囲 (K2a)

components/fmrb_kanji/ (新)、tools/kanji/、rakelib の kanji と test の task、doc/kanji_input/。ファームのビルド・sim・
ネイティブ版・ブラウザ版は動かさない。コミットはパスを明示して develop へ。push しない。

## report

`doc/kanji_input/report/k2.md` (K2a の節)。
