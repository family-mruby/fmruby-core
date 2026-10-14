# Asterism X2 指示書: 呼んだ相手の段 2 (送り手の情報の突き合わせ) を入れて効果を見る

> 状態: 指示 | 更新: 2026-10-12 | zenoh-pico の試験中の機能 (`Z_FEATURE_UNSTABLE_API`) を入れ、呼び出しに送り手の Zenoh の ID (source info) を付けて送り、受けた側で読めるようにする。asterism は、その ID を生存の印と突き合わせ、`Asterism.current_call.verified` で「つじつまが合う / 食い違う / 分からない」を出す。効果と費用を測る

前提: plan.md の X2 の行、report/x0.md・x1.md (R-14 の段 1-3、測った大きさ)、asterism の `docs/protocol.md` (protocol 1) と `docs/protocol_review.md` の R-14、asterism-zenoh の docs/feature_coverage.md。

言葉: 文書では、Family mruby の基板・sim・ブラウザ版を「端末」と呼ぶ (「機体」は使わない)。

## 1. 決まっていること (2026-10-12 ユーザ: 「入れて効果を見るのは早めがよい」)

- 段 2 を入れる。**これは証明ではない** (送り手が自分で書く値なので偽れる)。分かるのは「つじつまが合うか」。本当の証明は段 3 (G1.5 のルータの ACL)。仕様と docs にもそう書く。
- 形は X0 の R-14 の案に沿う:
  - 各アプリは、生存の印として自分の Zenoh の ID を出す (例: `asterism/<node>/@zid/<zid>`。正確な形は仕様で決める。protocol 1 の決まりの中で、足すだけの変更として扱う)。
  - 呼ぶ側の gem は、問い合わせに送り手の情報 (source info) を付ける。
  - 受ける側の gem は、キーの `<呼んだ相手>` と、送り手の Zenoh の ID と、生存の印の対応を突き合わせ、`current_call.verified` を出す: 合う (`:consistent`)、食い違う (`:mismatch`)、分からない (`:claimed`。送り手の情報が無い・印がまだ無い)、同じアプリの中 (`:local`)。名前は X1 の `current_call` の流儀に合わせて決める。
  - **食い違いは断る** (`Asterism::CallerMismatch` など。例外の種類 `kind` も決める)。分からないときは断らない (今までどおり動く)。
- API の名前・`verified` の値の名前は、R-14 の案とおすすめに沿う。それ以外に使う人から見える名前を足す必要が出たら、案を書いて止まる。

## 2. 作るもの

1. **picoruby-asterism-zenoh**: `Z_FEATURE_UNSTABLE_API` を入れたビルド (zenoh-pico の設定の生成は gem の mrbgem.rake の中。sdkconfig は触らない)。`get` に送り手の情報を付ける口と、`Query` から送り手の Zenoh の ID を読む口を足す。版を上げる (0.5.0 など、版の付け方の流儀に合わせる)。
2. **asterism-zenoh (CRuby)**: zenoh-c の試験中の API (今のビルド済みのものに入っている) で、同じ 2 つの口を足す。版をそろえる。
3. **asterism**: 上の口を使った段 2。仕様の文書・CHANGELOG・README。適合の試験の見本に、合う・食い違う・分からないの場合を足す。版を上げる (0.8.0 など)。gem の依存を新しい部品の版に。
4. **fmruby-core**: 端末のビルドに新しい部品を入れる (PIN)。ブラウザ版も。試しのアプリで、頭脳 (rover_brain) とオセロの画面かログに `verified` を出す。
5. **asterism-console**: 呼び出しの記録に `verified` を出す (console 自身が呼ばれた側になるとき)。Gemfile は公開後に親が上げる。

## 3. 測る・確かめる

1. **網を通るか**: 送り手の情報が、(a) 同じルータ、(b) 家のルータ経由 (ルータどうしの中継、W1 の構成か G0 の通し稽古の構成)、(c) ブラウザ版 (`wss://`)、(d) CRuby、のそれぞれで、受けた側に届くか。届かない経路があれば、その経路での `verified` が「分からない」になることを確かめる。
2. **食い違いを断れるか**: キーの呼んだ相手を偽った呼び出し (CRuby の生の Zenoh で作る) が、`:mismatch` で断られる。
3. **費用**: P4-Nano と AtomS3R で、ファームの大きさ、静的な内蔵 RAM、**動いているときの内蔵 RAM と PSRAM** (呼び出しを続けているとき)、呼び出しの往復時間 (前と後)。ブラウザ版の wasm の大きさ。
4. 全テスト (asterism の CRuby・mruby・適合の試験、console、`rake test`)、sim の標準構成 (エディタの 1 打鍵)、ブラウザ版のビルド、Retro・Tab5・AtomS3R のビルド。内蔵 RAM が増えない (増えるなら数字を出して止まる)。

## 4. 範囲・決まり

- **asterism / asterism-zenoh / picoruby-asterism-zenoh**: ブランチ `feature/source-info` にコミットのみ。push・タグ・gem の作成・公開はしない (親が検収し、公開はユーザ)。
- **fmruby-core**: develop から `feature/asterism-x2`。コミットのみ、push しない。**コミットは必ずファイルを指定する。`git commit -a` は使わない** (`.env` は変えない)。PIN は手元の作業場所 (`ASTERISM_DIR` など) でビルドする。
- **asterism-console**: `feature/x2` にコミットのみ。
- **触らないもの**: sdkconfig、submodule、VPS。
- **端末**: P4-Nano (192.0.2.10、`/dev/ttyACM0`)、AtomS3R (WiFi、`/dev/ttyACM1`)。アプリの区画だけを、ポートを明示して焼く。どちらもミュート中。端末の IP・mDNS の名前・MAC はリポジトリに書かない。
- **終わったら**: 起動したものを全部止める。`build/` は Linux の標準構成 (x86-64)、ブラウザ版のビルドが通る状態。
- **止まる条件**: zenoh-pico・zenoh-c の試験中の API で、送り手の情報を付ける・読むことができない (足りないものを書いて返す)。内蔵 RAM が増える。仕様の選択が要る。
- **report**: `doc/ruby_asterism/report/x2.md`、Japanese plain style (常体)。
