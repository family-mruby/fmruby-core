# MIDI 対応 作業指示書 (P7: 演奏を止めない — GC 停止の除去)

対象: 実装セッション (Opus)
ブランチ: `feature/midi`
前提: [report/p6.md](report/p6.md) の **10 章 (実機実測)** を必ず読む。
[work_order_p6.md](work_order_p6.md)、[README.md](README.md) も前提。

## 0. この段の位置づけ

P6 の実機実測で犯人が確定した。**実機の major GC は 1 回 100〜205ms 止まり**
(sim の約 40 倍)、それが音の遅れ (max_late 198ms) と stall にそのまま出る。
APU 経路は 3 個/イベントの割り当てが残っているため 47 秒に 9 回 GC が走り、
serial 経路 (0 個/イベント) は 3 回 — ユーザの聴感「APU の方が揺らぎが
大きい」はこの差である。

この段は 2 本柱:

- **A. `_spin` 前の GC ステップ** (本命)。205ms の一括停止を、アプリの
  暇な時間の細かい歩みに割る。**MIDI に限らず全アプリに効く**
  (raycaster は毎フレーム Hash x 25 を割り当てている等、割り当てを
  消しきれないアプリは他にもある)。
- **B. 音声メッセージの C 化**。APU 経路の 3 個/イベントを 0 にし、
  serial と同じ「GC の契機なし」にする。MIDI の決定打。

A と B は独立に実装・計測できる。**A から着手すること** (効果範囲が広く、
B の成否に関わらず価値がある)。

## 1. 実測値 (P6 10 章の再掲、この段の出発点)

同じ曲を約 46 秒ずつ、S3 実機、計測ビルド:

| | APU 出力 | serial 出力 |
|---|---|---|
| GC 回数 | 9 回 / 47 秒 | 3 回 / 46 秒 |
| 1 回の停止 | **110〜205 ms** | 94〜104 ms |
| max_late / stalls | 198 ms / 6 | 157 ms / 2 |

- serial 側の 3 回の内訳は「5 秒ごとの計装ログ自身の文字列組み立て」と
  「再生開始時の読み込み」。**計装ログの割り当ては直さなくてよい**
  (5 秒に 1 行は無害)。ただし計測を読むときは差し引くこと。
- 全 GC が major だった (n と major が常に一致)。

## 2. A. `_spin` 前の GC ステップ

### 2.1 使う道具 (調査済み、全部既存)

**mruby 本体にも mruby-task にも手を入れずに済む見込み**である。

- `mrb_gc_scheduler_driven(mrb, TRUE)` / `mrb_gc_scheduler_pending(mrb)` /
  `mrb_gc_step(mrb)` は **mruby/gc.h の中核 API** で、コメントに
  「任意の独自スケジューラが自分のアイドル点から GC を進めるための、
  gem 非依存の組み込み向けプリミティブ」と明記されている。
  `lib/add/picoruby-fmrb-app/ports/esp32/app.c` から直接呼べる。
- `gc->prof_last_step_us` (gc.h) が**直近 1 ステップの実時間**を持つ。
  ステップ予算の制御に使える。
- Ruby 側スイッチ `GC.scheduler_driven=` は mruby-task gem が提供済み。
- 計測: `FMRB_GC_PROFILE=1` ビルドで `GC.stat` に `prof_sync_*` (同期停止 =
  減らしたいもの) と `prof_step_*` (ステップに移った分) が**別々に**出る。
  `prof_step_jitter_*` はステップが起床を遅らせた量。効果測定はこの分離を
  そのまま読めばよい。

### 2.2 実装の指針

`mrb_fmrb_app_spin` (app.c) の**メッセージ待ちに入る前**に:

```
残り待ち時間に余裕がある間 (例: remaining > 2ms):
  mrb_gc_scheduler_pending() が真なら mrb_gc_step()
  1 ステップごとに経過を確認 (prof_last_step_us で予算管理)
  余裕が尽きるか pending が偽になったら抜けて、残りで従来どおり待つ
```

- **posix 側にも同じものを入れる** (sim と実機で挙動を揃える。
  `ports/posix` に _spin の POSIX 実装があるはず。無ければ所在を調べて
  同じ位置に)。
- 有効化 (`GC.scheduler_driven = true`) の場所とタイミングは**判断して
  報告に書く**こと。候補: fmrb-app.rb の main_loop 開始時。

### 2.3 読んでから設計すべき注意点 (ここで嵌まる)

- **scheduler_driven を有効にすると、割り当て経路の自動 GC が止まる**
  (gc.c の説明)。アプリが忙しくて `_spin` の余裕が無いままだと debt が
  積み上がり、安全弁 `GC.debt_limit` が発火する。**debt_limit の意味と
  既定値を gc.h / gc.c で読み、忙しいアプリでも破綻しないことを
  確かめてから設計する**こと。「暇が無いときにどうなるか」は
  報告書に必ず書く。
- generational mode は scheduler_driven 中は使えない (有効化時に off に
  なる)。現状の測定で全 GC が major だったので実害は無い見込みだが、
  戻すときの挙動 (自動では戻らない) は把握しておく。
- ステップ中にアプリ宛メッセージが届いた場合の遅れは
  `prof_step_jitter_*` に出る。**HID の体感を悪化させていないか**を
  jitter の数字と実際の操作で確かめる。

### 2.4 適用範囲の判断

理想は全アプリ既定 ON だが、**まず既定 OFF のスイッチとして入れ、
MIDI アプリ (smf_player / midi_apu デモ) で ON にして計測し、
既定化できるかを数字で判断**する形を推奨する (判断は任せる。
理由を報告に書くこと)。ON/OFF の切り替え方も設計に含める
(app.toml のフラグか、FmrbApp の API か)。

## 3. B. 音声メッセージの C 化

### 3.1 現状

`lib/add/picoruby-fmrb-app/mrblib/fmrb-audio.rb` の `note_on` / `note_off` が
Symbol キーの Hash を組み、`MessagePack.pack` して `_send_message` に渡す。
1 回あたり 3 オブジェクト (Hash 1 + pack 2)。`_send_message` 自体は 0。

### 3.2 やること

- `note_on` / `note_off` **だけ** (連打される 2 つ) を、C バインディングで
  組み立てる形に変える。Ruby の API は一切変えない。
- **ワイヤ上のバイト列は現状と完全一致させる**こと。受け手は kernel の
  audio_handler.rb で、msgpack の map を期待している。メッセージの形は
  固定なので、C 側はテンプレートのバイト列に値をパッチする実装で足りる
  (msgpack-c を引き込む必要はないはず。ただし判断は任せる)。
- **posix 側にも同じものを入れる** (sim で同一挙動)。
- 検証: 変更前後のバイト列一致 (sim でキャプチャ比較するか、ホストで
  組み立て関数を単体テスト)。bench アプリの alloc 計測で
  `audio.note_on` が **0 個/回**になること。

## 4. 任意 (できたら): プールサイズと停止時間の関係を 1 点測る

smf_player は 1 MB プールに移した直後に 100〜205ms を測ったため、
**プール拡大が sweep を伸ばした可能性**が p6.md 10.2 に書いてある。
`large_memory = 1` を一時的に外した 500 KB での停止時間を同条件で 1 回
測れば、「プールを大きくすると停止が伸びるか」に答が出る。
プールサイズの指針 (selective_offload.md の切り出し設計にも効く) に
なるので、余裕があれば。実機が要るので、**手順だけ整えてユーザに依頼**
する形でもよい。

## 5. やらないこと

- 受信 (MIDI IN)、USB-MIDI host、ピン排他、audio_note_lat (別の機会)。
- `lib/add/picoruby-midi/` は無改変 (md5 一致維持)。
- **mruby 本体・mruby-task gem の変更** (2.1 の既存 API で足りる見込み。
  どうしても足りない場合は変更せず、何が足りないかを報告に書いて止める)。
- 計装ログ自身の割り当て除去 (1 章)。

## 6. 守ること

- 報告は `doc/midi/report/p7.md`。README は書き換えない。
- sim 優先。実機の確認手順を報告書に書く (p6.md 7 章の形式)。
- `FMRB_GC_PROFILE` は既定無効のまま (計測時だけ ON)。
- lib/ 編集後は `rake clean`。esp32 と linux の両方でビルドが通ること
  (posix 側も入れるので必ず両方)。
- 既存テスト 122 項目 + sim の実アプリ動作 (smf_player / midi_apu /
  ゲーム 1 本) を壊さない。
- コメントは英語、コミットログは英文、ASCII 以外の記号は使わない。
- 作業ツリーは現在 **esp32 ビルド** (計測ビルド、FMRB_GC_PROFILE=1)。
  sim を使うには `rake clean_all` から。

## 7. 完了の条件

1. **A が入り、sim で効果が数字で出ている**: APU 再生中の `prof_sync_*`
   がほぼ 0 になり、仕事が `prof_step_*` に移っている。stalls が増えて
   いない。jitter が HID の体感を壊していない。
2. **「暇が無いアプリでどうなるか」が説明されている** (debt_limit の
   挙動を含む)。
3. **B が入り、`audio.note_on` の割り当てが 0 個/回** (bench 実測)。
   ワイヤのバイト列が変更前と一致。
4. 実機での確認手順が報告書にある (計測ビルドで焼く。APU 再生で
   stalls=0 / max_late < 50ms が目標値)。
5. 既存テストと sim の実アプリが全部動く。
