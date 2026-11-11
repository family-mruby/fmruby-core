# K6 指示書: カタカナの候補、小書きのかな、送り途中の切り替え

> 状態: 実装中 | 更新: 2026-11-11 | 正は plan.md の「ユーザの見直し (2026-11-11)」

## やること

1. **小書きのかな・外来語の音をローマ字で打てるようにする** (`main/drivers/usb/fmrb_kana.c`、ESP32・sim・ネイティブ版の
   かな入力で共通。BASIC の表など同じ表を使う所があれば合わせる)。
   - 一般の IME (MS-IME・Google 日本語入力) と同じ組み合わせ: 小書きの単独 (`xa xi xu xe xo`・`la li lu le lo` → ぁぃぅぇぉ、
     `xtu ltu xtsu ltsu` → っ、`xya xyu xyo lya lyu lyo` → ゃゅょ、`xwa lwa` → ゎ、`xka xke` → ヵヶ は要れば)、
     外来語の音 (`thi thu dhi dhu` → てぃ てゅ でぃ でゅ、`twu dwu` → とぅ どぅ、`fa fi fe fo fyu` → ふぁ…、
     `va vi vu ve vo` → ゔぁ…ゔ、`she je che` → しぇ じぇ ちぇ、`tsa tsi tse tso` → つぁ…、`wi we` → うぃ うぇ、
     `kwa gwa qa qi qe qo` など)。採る表は report に一覧で書く。
   - **今打てる組み合わせの意味は 1 つも変えない** (既存の打鍵の試験と、全組み合わせを前後で比べる試験で示す)。
     衝突するもの (今は別の意味になる組み合わせ) が見つかったら、変えずに止まって返す。
   - カタカナモードでも同じ (小書きのカタカナ)。
2. **辞書を作り直す** (`tools/kanji/`): カタカナの読みだけの語 (読みをカタカナにしただけの表記) を外す。1 で打てるようになった
   読みの語 (K1 で外した約 700) を戻す (打てるかの判定を 1 の表に合わせる)。大きさは 1 MB のまま。辞書の形 (版 1) は変えない。
   語の数・境目・日常の語の入り方を report に。
3. **変換の側でカタカナの形を必ず候補に足す** (K3 の変換、C): 引いた読みのカタカナの形を候補に足す。位置は 2 番目
   (辞書の候補が 1 つ以上あるとき。無ければ 1 番目) を親の仮決めとし、report に書く。辞書に同じ形がもうあれば足さない。
   最長一致で読みの一部だけを変換するときは、その一部のカタカナの形。
4. **送り途中に前のアプリが変わったら残りを捨てる** (K4c・K3d の確定の文字の待ち。ESP32・ブラウザ版・ネイティブ版):
   ユーザの決定。捨てた数をログに 1 行。

5. **辞書を 2 つ配る** (ユーザの決定): 標準 1 MB と大きい辞書 4 MB。
   - 名前: 標準は今どおり `fmrb_kanji.dic`、大きい辞書は `fmrb_kanji_4m.dic` (親の仮決め)。端末は今の既定の
     `/sd/dict/fmrb_kanji.dic` を探す。大きい辞書を使う人は、名前を `fmrb_kanji.dic` に変えて置くか、`kanji_dict` で指す。
     端末のコードは変えない。
   - `rake kanji:dict` で両方作れるようにする (例: `rake kanji:dict_release` が 1 MB と 4 MB を作り、`kanji:place` が両方を写す)。
   - 案内 (`flash/usr/share/doc/kanji_ja.md`・`kanji.md`) と、直下の `doc/release.md` の添付の手順を 2 つにする
     (sha256 も 2 つ)。4 MB の辞書で、開く時間・PSRAM の量・日常の語の入り方を report に。

## 確認

`rake regress`、`rake regress:screens`、`rake kanji:test`、`rake regress:size` (内蔵 RAM の増分 0)。sim とネイティブ版で
`pa-thi-` → ぱーてぃー → 変換でパーティー、`fairu` → ふぁいる → ファイル、カタカナの候補が 2 番目に出る、辞書に無い読み
(例: `zuzuzu`) でもカタカナにできる。

## 触ってよい範囲

fmruby-core のかなの合成・変換・host task・tools/kanji・文書。コミットはパスを明示して develop へ。push しない (親がする)。
件名は `<type>(<scope>): <summary>` (fmruby-core/CLAUDE.md)。

## report

`doc/kanji_input/report/k6.md`。
