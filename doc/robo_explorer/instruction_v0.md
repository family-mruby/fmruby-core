# ロボットエクスプローラー v0 実装指示

作成: 2026-08-16。対象: 実装セッション。前提知識はこの文書と plan.md で
完結すること。**動作確認は当面 Linux シミュレーションのみ** (実機不要)。

## 0. 何を作るか (2 ファイル + sidecar)

```
flash/app/game/robo_explorer/robo_explorer.app.rb    世界側 (アプリ 1)
flash/app/game/robo_explorer/robo_explorer.app.toml
flash/app/game/robo_explorer/robo_pilot.app.rb       操縦サンプル (段階 0+1)
flash/app/game/robo_explorer/robo_pilot.app.toml
```

プロトコルと題材は plan.md 4 章が正。この指示書と食い違ったら plan.md に
従い、食い違い自体を report に書く。

## 1. スコープ (v0 でやること / やらないこと)

やる:

- 固定 1 面 (seed 生成は次段)。12x12 マス、外周は壁。鍵 1・扉 1・ゴール 1。
  扉はゴールの手前を塞ぐ配置にする (鍵なしではクリア不能な面にする)。
- v0 プロトコル (robo/state, robo/cmd, robo/result) の完全実装。
- 表示は日本語専用 (漢字あり)。`@gfx.set_font(:ja, 12)` (efontJA_12)。
  i18n 表は作らず日本語リテラル直書き。文言は小学生が読める語彙。
- robo_pilot は段階 0+1 を 1 本で: state を日本語で表示し、矢印キーを
  cmd に転送する。↑= move、←/→ = turn L/R。
- クリア判定 (`done: true`) と手数 (steps) の表示。R キーでリセット
  (アプリ 1 側の操作。これと終了以外のキー入力をアプリ 1 に作らない)。
- ターン周期の実測 (7 章)。

やらない (plan.md 9 章の将来分):

- seed 生成面・複数面・成績保存
- map トピック、対戦、MicroPython ラッパ
- アニメーション (1 ターン = 1 回の再描画でよい)

## 2. 使う API (この形で動くことは確認済み)

Pub/Sub (FmrbApp のメソッド):

```ruby
subscribe("robo/cmd")            # on_create で
unsubscribe("robo/cmd")          # on_destroy で
publish("robo/state", { "turn" => 1, ... })   # Hash を渡す

def on_control(msg)
  if msg["cmd"] == "topic_data" && msg["topic"] == "robo/cmd"
    data = msg["data"]           # キーは String で届く (Symbol にならない)
    ...
  end
end
```

- publisher 自身には届かない (自分の publish を自分で受けようとしない)。
- 保持は無い。**state は操作処理後に必ず + 無操作でも 1 秒周期で publish**
  (後から起動した pilot が世界を知る唯一の手段)。周期は on_update の
  戻り値 (次回呼び出しまでの ms) と Machine.board_millis で作る。

描画:

```ruby
@gfx.set_font(:ja, 12)                     # efontJA_12。半角 6px / 全角 12px
@gfx.draw_text(x, y, "鍵 1本", color, bg, mixed: true)  # 混在文字列は mixed:
@gfx.fill_rect(x, y, w, h, color)          # 盤面タイルはこれで足りる
```

- 盤面は fill_rect の色タイルでよい (16px/マス、BMP アセット不要)。
  壁=灰、床=黒、鍵=黄、扉=茶、ゴール=緑、ロボ=白 + 向きの印。
  TileSheet を使いたければ使ってよいが、v0 の要求ではない。
- 描画後は `@gfx.present` (present しないと合成に載らない)。

キー入力 (robo_pilot):

```ruby
def on_event(ev)
  super(ev)
  return unless ev[:type] == :key_down
  case ev[:scancode]              # keycode ではなく scancode (HID Usage ID)
  when FmrbConst::KEY_UP    then publish("robo/cmd", { "op" => "move" })
  when FmrbConst::KEY_LEFT  then publish("robo/cmd", { "op" => "turn", "to" => "L" })
  when FmrbConst::KEY_RIGHT then publish("robo/cmd", { "op" => "turn", "to" => "R" })
  end
end
```

## 3. picoruby の地雷 (このアプリで踏みそうなものだけ)

- ファイル末尾に必ず起動コードを書く。無いと何も起きない:
  ```ruby
  begin
    app = RoboExplorerApp.new
    app.start
  rescue => e
    Log.error("RoboExplorer: #{e}")
  end
  ```
- 数十回以上のループは `each` でなく `while` (ブロック呼び出しが ~0.4ms)。
  12x12 の盤面走査は while で書く。
- `Array#include?` は使わない (Ruby 実装で 1.7ms)。`index` か比較の並記。
- `defined?` は無い。msgpack のキーは String。バイナリは getbyte/setbyte。
- クラス内の bare 定数参照は `::` を付ける (`::JSON` など)。
- 時間は `Machine.board_millis` (実時間)。フレーム数で数えない。

## 4. sidecar (.app.toml) の雛形

```toml
# robo_explorer.app.toml
app_handle_name = "robo_explorer"
app_screen_name = "RoboExplorer"
app_screen_name_ja = "ロボットエクスプローラー"
default_window_mode = "window"
default_window_width = 260     # 盤面 192px + 状態欄。実装しながら調整可
default_window_height = 220
default_window_pos_x = 4
default_window_pos_y = 16
launcher_visible = true

# robo_pilot.app.toml
app_handle_name = "robo_pilot"
app_screen_name = "RoboPilot"
app_screen_name_ja = "ロボ操縦"
default_window_mode = "window"
default_window_width = 150
default_window_height = 140
default_window_pos_x = 270
default_window_pos_y = 40
launcher_visible = true
```

窓サイズ・位置は Modern (426x240) で 2 窓が重ならない値から出発。
Retro (320x240) では重なってよい (決定済み)。

## 5. 世界側の実装メモ

- 盤面データは文字列 1 本 + getbyte が最軽量だが、v0 は 12x12=144 なので
  Array of Array でも害はない。書きやすい方でよい。
- ターン処理は on_control の中で完結させる:
  cmd 受信 → 判定 → 状態更新 → result publish → state publish → 再描画。
  この順序 (result が先、state が後) を守る。pilot は result で失敗を知り、
  state で世界を知る。
- `goal` (方角) は 8 方位の文字列 ("N","NE","E",...)。ロボの現在地と
  ゴールの座標差から算出。
- 不正な cmd (知らない op、turn の to が L/R 以外) は
  `ok: false, reason: "bad_cmd"` を返す。黙って捨てない。
- クリア後は cmd を受けても `reason: "done"` で断る。R でリセット。

## 6. Linux での動作確認 (これが v0 の検収)

**重要: 現在 build/ は esp32 (P4) の残骸。必ず `rake clean_all` してから
`rake build:linux`。ビルド後 `file build/fmruby-core.elf` で x86-64 を確認
すること** (Xtensa のまま "Linux build complete" と出る罠がある)。
graphics-audio 側は変更しないので、ビルド済みならそのまま。

```bash
# 起動 + 画面確認 (リポジトリルートで)
tools/dev_run_check.sh --keep out.png

# ランチャーから robo_explorer → robo_pilot を起動 (座標は画面を見て調整)
ruby tools/fmrb_input.rb click 20 5 sleep 500 click 15 17      # メニュー→Launcher
python3 tools/fmrb_screenshot.py s1.png                        # アイコン位置確認
ruby tools/fmrb_input.rb dclick <x> <y>                        # 起動

# 操縦: pilot の窓をクリックしてフォーカス → 矢印キー注入
ruby tools/fmrb_input.rb click <pilot_x> <pilot_y> sleep 300 key up
python3 tools/fmrb_screenshot.py s2.png

# 片付け
docker compose down
```

確認項目 (スクリーンショットで判定できるものはそれで):

1. 両アプリが起動し、pilot に「まち」ではなく state の中身 (座標・向き・
   前方) が日本語で出る (= state の周期 publish が効いている)。
2. ↑ で 1 マス進み、両窓の表示が一致して更新される。
3. 壁に向かって ↑ → pilot に「まえは かべ」等の失敗表示 (= result 経路)。
4. 鍵を拾う → 所持数が増える。鍵なしで扉 → locked。鍵ありで扉 → 開く。
5. ゴール到達で done + 手数表示。以後の cmd は done で断られる。R で再開。
6. ログ確認: `docker logs fmruby_core | grep -i "robo\|error"` に例外が
   無いこと。

## 7. ターン周期の実測 (1 回だけ)

pilot 側で cmd publish 直前と、対応する result 受信時の
`Machine.board_millis` の差を取り、10 手分を Log.info で出す。
`docker logs` から読めれば良い (画面表示は不要)。目安: 1 往復 100ms を
超えるようなら報告に書く (設計の前提が崩れるため)。実測値は
report に必ず残すこと。Linux の値は実機より速い点も添える。

## 8. 進め方と報告

1. robo_explorer (世界側) を先に。state の周期 publish まで入れて、
   pilot なしで「画面に迷路とロボが出て state がログに流れる」まで。
2. robo_pilot を書いて 6 章の確認項目を上から順に。
3. 気づき・確定した実測値・plan.md との食い違いは
   doc/robo_explorer/report/v0.md に書く (計画書には書き戻さない)。
4. コミットは 6 章の 1-6 が全部通ってから。メッセージは英文。
