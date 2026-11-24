# S2a FmrbConst と Math を sig に書く

> 2026-11-24 | サブ (Sonnet) の報告。指示書は instruction_s2a.md。ブランチ `sig-s2a`。

## 1. 書いたもの

| ファイル | 内容 |
|---|---|
| `sig/fmrb_const.rbs` (新) | `FmrbConst` の定数 162 個と `FmrbConst.bt_mac` |
| `sig/math.rbs` (新) | `Math` の関数 26 個、定数 `PI`・`E`、例外 `Math::DomainError` |

内訳:

- FmrbConst の定数 162: `KEY_*` 99 (A-Z 26、数字 10、記号 11、Enter ほか 5、F1-F12、システム 3、移動 6、矢印 4、NumLock、修飾 8)、
  `MOD_*` 9、`GP_*` 20、`THEME_*` 9、`WHEEL_LINES`、機種・ビルド 17 (`PLATFORM`・`BOARD`・`NATIVE`・`HW_FAMILY`・`FONT_JA`・
  `HAS_WIFI`・`LANGUAGE`・`OS_VERSION`・`GA_VERSION`・`LINK_VERSION`・`BUILD_DATE`・`IDF_VERSION`・`MAC_ADDRESS`・`CHIP_*` 3・
  `FLASH_SIZE_MB`・`PSRAM_SIZE_MB`・`RESET_REASON`)、`PROC_ID_*` 8、`PROC_STATE_*` 5、`MSG_TYPE_*` 4。
- 素直 / つまずく: 定数は gen_help が長文を出さない (定数は要約 1 行だけが db に入る) ので、全部「素直」の形 (要約 1 行) で書いた。
  機種で値が変わるもの (`BOARD`・`PLATFORM`・`HW_FAMILY`・`CHIP_*`・`FLASH_SIZE_MB`・`PSRAM_SIZE_MB`・`MAC_ADDRESS`・`RESET_REASON` ほか)
  は、要約に「linux では ...」「機種ごとに違う」を書き、モジュールの長文に「機種やビルドで変わる」と書いた。
  メソッドは `bt_mac` が「つまずく」 (起動後に決まる・分からない間は "-")。
- Math の関数 26: 素直 15 (`sin cos tan atan atan2 sinh cosh tanh asinh exp expm1 cbrt hypot erf erfc ldexp`)、
  つまずく 10 (定義域の外で DomainError: `asin acos acosh atanh log1p log log2 log10 sqrt`)、`frexp` (戻りが配列) を加えると 11。
  実際の数は素直 15 + つまずく 11 = 26。`log` は省略できる底を `?` で書いた。

## 2. 根拠の表 (読んだ場所)

`lib/gems/picoruby-fmrb-const/ports/esp32/const.c` (components 側の mrbgems の写しと diff -r で同一を確認)。

| 仲間 | 場所 |
|---|---|
| `KEY_*` (値はそのまま sig の要約に書いた) | const.c:161-269 (USB HID Usage ID。letters/ev[:scancode] の注記は 157-160) |
| `MOD_*` | const.c:273-285 |
| `GP_*` | const.c:290-323 |
| `PLATFORM` | const.c:354-358 |
| `BOARD` | const.c:363-381 (wasm / linux / tab5 / naryav4 / atom_display / atoms3r / sticks3 / 既定 narya_v3) |
| `NATIVE` | const.c:387-391 |
| `HW_FAMILY` | const.c:398-406 |
| `FONT_JA` | const.c:410-414 |
| `PROC_ID_*` | const.c:417-424。値は components/fmrb_common/include/fmrb_task_config.h:419-441 (KERNEL=0, HOST=1, SYSTEM_APP=2, SYSTEM_OVERLAY=3 (Ruby へは出ない), USER_APP0=4 ... USER_APP4=8) |
| `PROC_STATE_*` | const.c:427-431。値は components/fmrb_common/include/fmrb_app.h:29-33 |
| `MSG_TYPE_*` | const.c:434-437。値は components/fmrb_msg/fmrb_msg.h:31-34 |
| `OS_VERSION` `GA_VERSION` `LINK_VERSION` | const.c:449-451 (中身の値は sig に書いていない) |
| `HAS_WIFI` | const.c:457-461 |
| `BUILD_DATE` | const.c:469-479 |
| `IDF_VERSION` `MAC_ADDRESS` `CHIP_*` `FLASH_SIZE_MB` `PSRAM_SIZE_MB` `RESET_REASON` | const.c:482-554 (linux の値は 483-490) |
| `LANGUAGE` | const.c:562-566 |
| `THEME_*` | const.c:569-578。既定値 35-45、`[theme]` での上書きは main/kernel/fmrb_kernel.c:655-667 |
| `WHEEL_LINES` | const.c:580、既定 3 は const.c:62、上書きは main/kernel/fmrb_kernel.c:580 |
| `bt_mac` | const.c:332-342、登録は 351 |
| `Math` の関数・引数の数 | components/picoruby-esp32/picoruby/mrbgems/picoruby-mruby/lib/mruby/mrbgems/mruby-math/src/math.c:714-746 (登録)、各関数 189-692 |
| `Math::PI` `E` `DomainError` | 同 math.c:700-712。例外メッセージは 15-21 |
| 引数が Integer も可・数でなければ TypeError | math.c:176 の `mrb_as_float`、mruby の src/object.c:692-725 (`mrb_ensure_float_type`: Integer→Float、nil と他は TypeError) |
| 定義域の判定 | asin/acos 237-261 (x<-1 か x>1)、acosh 380-385 (x<1)、atanh 399-404 (-1..1 の外)、log1p 454-456 (x<-1)、log 480-491 (x<0、base<0)、log2/log10/sqrt 510-559 (x<0) |
| `frexp` の戻り | math.c:615-624 (`mrb_assoc_new` で [Float, Integer]) |
| ビルドに入るか | lib/upstream/family_mruby_esp32.rb:95、family_mruby_esp32p4.rb:107、family_mruby_linux.rb:105、family_mruby_wasm.rb:85、family_mruby_native.rb:100 に `mruby-math` |

## 3. 書かなかったもの / 判断が要るもの

- `FmrbConst::APP_CTRL_SPAWN`・`APP_CTRL_KILL`・`APP_CTRL_SUSPEND`・`APP_CTRL_RESUME` (const.c:440-443): 同梱のアプリ・kernel の Ruby のどこでも使われておらず
  (アプリは `send_message` に Hash で `"cmd" => "spawn"` を渡す)、内部用と判断して書かなかった。`MSG_TYPE_APP_CONTROL` は使われているので書いた。
- `FmrbConst::MAX_PATH_LEN` (const.c:446)・`LED_ERR_NONE`・`LED_ERR_FATAL`・`LED_ERR_VERSION_MISMATCH` (const.c:557-559): アプリは使わない
  (LED_ERR_* は kernel の Ruby だけ)。内部用として書かなかった。要るなら足せる。
- `FmrbHw` (const.c:588 以降の `PIN_*` と 4 つのメソッド): 範囲外。アプリが `FmrbHw::PIN_I2C1_SCL` ほかを使う (別の段で)。
- `Math` で CRuby にあって端末に無いもの (`lgamma`・`gamma` など): 実装に無いので書かなかった。
- アプリが使うのに実装に見つからなかったもの: 無し (使われている `FmrbConst::` の名前は全部 const.c にある。`KEY_`・`THEME_` だけの名前は grep の
  文字列の途中切れ)。
- spinel 側の `FmrbConst` (main/ruby/spinel/fmrb_app_base_spinel.rb:140、tools/spinel/gen_const_rb.rb) は部分集合の写しなので見ていない。
  Spinel の `Math` の扱いも読んでいない。

## 4. 数える道具と型 db の大きさ

- `rake sig:coverage` 前 -> 後: sig の 67 クラス・830 メソッド -> 70 クラス・857 メソッド。アプリが使う未定義: クラス 31 -> 29、
  定数 76 -> 18、既知の受け手のメソッド 65 -> 61、型の決まらない名前 97 -> 96。実装にあって sig に無い C: 144 -> 117。
  基準との差は `app_class -2, app_constant -58, app_method -4, app_name -1, impl_c -27`。`FmrbConst`・`Math` の「無い」は 0 になった。
  `rake sig:coverage:update` で基準を更新 (別コミット)。
- 型 db (`rake ti:test` が作る、sig/ と tools/ti/test_sig/ を重ねたもの。ファームの db とは gpio の分だけ違う):

| 領域 | 前 | 後 | 差 | 上限 |
|---|---|---|---|---|
| 名前 | 7,362 | 9,170 | +1,808 | 65,535 |
| シグネチャ | 29,719 | 33,842 | +4,123 | 65,535 |
| 要約 (文書) | 53,219 | 65,137 | +11,918 | 24 ビット |
| メソッドの数 | 2,570 | 2,672 | +102 | |
| 定数の数 | 30 | 194 | +164 | |

  上限に対して 名前 14%、シグネチャ 52%。

## 5. 確認の結果

- `rake ti:test`: 通った (sig の追加で期待値の直しは不要)。
- `rake regress`: 通った (host 34 suites 全部 pass、描画の記録 310 runs pass)。
- `rake sig:coverage`: 通った、基準更新後は差なし。
- `rake docs:index`: 流していない (doc/README.md の索引に report の行が要るなら親で流す)。
- `boundary:check` は `rake regress` の中に入っており落ちなかったので、`sdk/` の変更は無い。

## 6. 確かめていないこと

- **ビルドも sim も動かしていない**。エディタの補完・F1・Ctrl+T・Ctrl+E に実際に出るかは見ていない (型エンジンの試験が通っただけ)。
  定数のあるモジュールに長文を書いたが、F1 のモジュールの節に出るかは確認していない。
- 定数は長文を出さない作りと読んだ (tools/ti/gen_help.rb に const の語が無い) だけで、`FmrbConst` モジュールの先頭の長文が F1 に出るかは動かしていない。
- `Math` の値の例 (`Math.frexp(1234)` の 0.6025390625・11 は math.c の doc コメントから、`Math.asin(0.5)` などは数学の値) は動かして確かめていない。
- `Math.log(x, base)` の x や base が 0 のときは C の log に渡る、と書いたが動かしていない (コードの読みだけ)。NaN を渡したときは読んでいない。
- mrb_float が double か float かは確認していない (mrbconf を見ていない)。
- `ldexp` の第 2 引数を Float で渡したときは読んでいない (`"fi"` の取り方だけ)。
- `BUILD_DATE`・`MAC_ADDRESS` などの実値、機種ごとの `BOARD` の値は、ソースの #if を読んだだけ。
- `FmrbConst` の 4 つの機種の値の対応 (`BOARD` が各端末で何になるか) は、`FMRB_HW_*` が各機種でどう定義されるかまで追っていない。

## 7. 迷った所・型紙で足りなかった所

- 定数に長文を書く場所が無い (gen_help が定数を出さない)。「機種で変わる」を各定数の長文に、という指示は、要約の 111 バイトとモジュールの長文で代えた。
  定数の長文を F1 に出すには gen_help に手を入れる必要があるので、親の判断。
- `THEME_*` の既定値は書いていない (`[theme]` で変わるため。値を書くと config とずれる)。
- `Math` は引数を `(Integer | Float)` と書いた (`Numeric` は他の sig に無い)。
- 要約が 111 バイトを超えたもの 16 件ほどを書き直した (最終は 0 件、道具で確認)。
