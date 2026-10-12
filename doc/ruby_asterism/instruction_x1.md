# Asterism X1 指示書: asterism 0.7.0 を protocol 1 だけにする (全部を一斉に入れ替える)

> 状態: 指示 | 更新: 2026-10-12 | X0 の決め直し 15 項目を全部おすすめで入れ、網の決まりを protocol 1 にする。今の protocol 0 は開発中なので捨て、互換の送り分けは作らない。gem・端末のファーム・sim・ブラウザ版・console・CRuby の例を一斉に入れ替える

前提: plan.md の X1 の行、report/x0.md、asterism の `docs/protocol.md` と `docs/protocol_review.md` (決め直しの各項目 R-1..R-16 と、おすすめ)、`test/protocol/` (適合の試験)、report/g1b.md (`Asterism.session`)、report/s4.md と g1.md (頭脳の `from:`、オセロの最初の引数 `from`)。

言葉: 文書では、Family mruby の基板・sim・ブラウザ版を「端末」と呼ぶ (「機体」は使わない)。仕様の文書は英語。

## 1. 決まっていること (2026-10-12 ユーザ: 「全部おすすめで」「今の protocol は捨ててよい」)

- **現行に引きずられず、きれいな形にする** (2026-10-12 ユーザ)。protocol 0 との互換を考えないので、X0 の review にあった「今の版が要素 3 つの呼び出ししか受けないので、呼び出しの中身を広げられない」などの、互換のための制約は無くなる。網の中身 (呼び出し・答え・例外・meta・プロフィールの形、キーの形) は、言語によらず読みやすく、あとで足しやすい形に決め直してよい (例: 呼び出しと答えを、位置で意味が決まる配列ではなく、名前つきの欄の map にする、など。選んだ形と理由を仕様と report に書く)。
- `docs/protocol_review.md` の各項目は、**おすすめのとおり**に入れる (R-1..R-15、R-16 のアプリごとの生存の印も)。ただし、おすすめが「互換のための形」だった項目は、きれいな形に置き換えてよい (例: R-1 の「3 要素のまま、2 要素も受ける」、R-9 の「4 つ目の要素を足す」)。
- **使う人から見える Ruby の API** (メソッドの名前・引数の形) を変える必要が出たら、案を書いて止まる (網の中身は自由、API はユーザが決める)。
- **protocol 1 だけ**を話す。protocol 0 の呼び出しは受けない (断るか、答えない。どちらかを決めて仕様に書く)。プロフィールの `protocol` は 1。
- **呼んだ相手 (R-14)**:
  - **キーの形は protocol 1 に入れる** (呼び出しのキーに呼んだ相手を入れる形、例: `asterism/<node>/<app>/<obj>/call/<caller>`。正確な形は仕様で決める)。後で protocol 2 が要らないようにするため。
  - 段 1: gem が自分のノードの名前を付けて送り、呼ばれた側は `Asterism.current_call` (呼ばれている間だけ。from・確かめられたか・オブジェクト・メソッド) で受け取る。`expose(..., caller: :from)` で、gem が `from:` を埋める形も入れる。
  - 段 2 (送り手の情報の突き合わせ): この段では入れない。zenoh-pico の試験中の機能 (`Z_FEATURE_UNSTABLE_API`) を入れたときの大きさと内蔵 RAM を測り、report に書く (入れるかはユーザ)。
  - 段 3 (ルータの ACL で確かめる): G1.5 で行う。この段では、キーの形が ACL で書けることだけを確かめる (zenohd の ACL で「この証明書の名前の相手だけが、自分の名前のキーで呼べる」を 1 つ書いて、手元で効くことを見る)。
- R-10 の、名前を文字列で指定して呼ぶ口の名前は、review のおすすめのとおり (無ければ案を 1 つにして report に書く)。

## 2. 作るもの

1. **asterism (gem)**: protocol 1 の実装 (portable と CRuby)。版は 0.7.0。CHANGELOG に「互換を切る」と大きく書く。`docs/protocol.md` を protocol 1 に書き直す (protocol 0 は付録に「捨てた形」として短く残す)。適合の試験の見本を protocol 1 に作り直し、CRuby と mruby の両方で通す。
2. **asterism-zenoh / picoruby-asterism-zenoh**: bin を扱うのに要る直しがあれば行う (版をそろえるかは理由とともに決める。asterism-zenoh を上げるなら 0.7.0、gem の依存も直す)。
3. **fmruby-core**:
   - 端末の MessagePack (`lib/add/picoruby-fmrb-msgpack`): bin を読む (Ruby の文字列として、バイナリのまま)、余計なバイトを断る、64 ビットの整数を正しく扱う。ext の扱いは仕様に合わせる。
   - 試しのアプリ (asterism_demo・othello・rt_probe・rover_brain・rover_pilot・rover_cam・ros2_* など、Asterism を使うもの全部) を protocol 1 に合わせる。頭脳の `from:` は `expose(..., caller: :from)` に、オセロの最初の引数 `from` は `Asterism.current_call` に移す。
   - `ASTERISM_PIN` (push しないので、ビルドは `ASTERISM_DIR=../asterism`)。
   - 端末 (P4-Nano・AtomS3R) を焼き直す (アプリの区画だけ)。sim とブラウザ版を作り直す。
4. **asterism-console**: bridge (呼び出し・プロフィールの読み取り・アイコンが bin になる・例外の種類の表示・アプリごとの生存の印の扱い)、記録 (MCAP) の中身の読み方。protocol 0 の古い記録を開いたときの扱い (読めない旨を出す、で可)。
5. **CRuby の例** (asterism の `examples/`): protocol 1 で動くように。

## 3. 受け入れ条件

1. 適合の試験 (protocol 1) が CRuby と mruby で全部通る。protocol 0 の呼び出しが、仕様のとおりに扱われる。
2. Python の標準の MessagePack の読み方 (`msgpack` の既定) で、プロフィール (アイコンつき) と呼び出しの答えが壊れずに読める (小さな Python の試しで確かめる。Python の部品を作るのではなく、読めることの確かめだけ)。
3. 端末 (P4-Nano・AtomS3R)・sim・ブラウザ版・console・CRuby がつながり、オセロ・`asterism_demo`・台車の頭脳と操縦機が動く。console の図・プロフィール・アイコン・呼び出しの記録が出る。
4. 頭脳とオセロが、自分で `from` を渡さず、gem から呼んだ相手を受け取っている。console の呼び出しの記録にも呼んだ相手が出る。
5. 呼んだ相手のキーの形で、zenohd の ACL が「自分の名前でだけ呼べる」を書けることを、手元で 1 つ確かめる。
6. zenoh-pico の試験中の機能の大きさと内蔵 RAM の測り (入れない。数字だけ)。
7. 全テスト (asterism・console・`rake test`)、sim の標準構成 (エディタの 1 打鍵)、ブラウザ版のビルド、Retro・Tab5・AtomS3R のビルドが通る。内蔵 RAM が増えない。

## 4. 範囲・決まり

- **asterism / asterism-zenoh / picoruby-asterism-zenoh**: ブランチ `feature/protocol1` にコミットのみ。push・タグ・gem の作成・公開はしない (親が検収し、公開はユーザ)。
- **fmruby-core**: develop から `feature/asterism-x1`。コミットのみ、push しない。**コミットは必ずファイルを指定する。`git commit -a` は使わない** (`.env` は変えない)。
- **asterism-console**: `feature/x1` にコミットのみ。Gemfile の rubygems の行は、公開の後で親が上げる (今は `~> 0.6.0` のまま)。
- **触らないもの**: sdkconfig (zenoh-pico の試験中の機能を入れるのに要るなら、測りの試しのビルドだけで、コミットしない)、submodule、VPS、G0 の `deploy/vps` の中身 (protocol 1 に合わせる直しが要るなら最小限にし、report に書く)。
- **端末**: P4-Nano (192.168.10.10、`/dev/ttyACM0`)、AtomS3R (WiFi、`/dev/ttyACM1`)。アプリの区画だけを、ポートを明示して焼く。どちらもミュート中。端末の IP・mDNS の名前・MAC はリポジトリに書かない。
- **終わったら**: 起動したものを全部止める。`build/` は Linux の標準構成 (x86-64)、ブラウザ版のビルドが通る状態。
- **止まる条件**: review のおすすめでは決まらない仕様の選択が出たとき、zenoh-pico・zenoh-c の制約で R-14 のキーの形が成り立たないとき。
- **report**: `doc/ruby_asterism/report/x1.md`、Japanese plain style (常体)。
