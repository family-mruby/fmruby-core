# F6 指示書: GPIO だけが見る「アプリが触ってはいけないピン」の表

> 2026-11-25 | 親セッションからサブへの指示。計画は plan.md (決定 20)。F4 の GPIO の直しは report/f4.md
> (1〜4 章と 7 章)。F4 で入れた判定 (台帳に尋ねる・ボタンは入力だけ・読むだけは通す) はそのまま使う。

## 背景

F4 で、`GPIO.new` と設定の変更はピンの台帳に尋ね、システムのピンは IOError で断るようにした。しかし台帳に載っている
システムのピンは一部だけ (P4-Nano は 35・37・38、Tab5 は 24・25)。SD・WiFi 用の C6 との SDIO・I2S・Ethernet・画面・
USB などは台帳に無く、アプリが `GPIO.new` で取れてしまい、設定が変わる。ユーザ決定: **台帳は変えず、GPIO だけが見る
表を機種ごとに持つ**。

## やること

1. **表を作る**: 機種ごと (NARYAv3・NARYAv4 (P4-Nano)・TAB5・ATOMS3R・STICKS3。ATOM_DISPLAY は中断中なので、
   今ある定義で組める範囲) に、`fmrb_pin_assign.h` の定義から「アプリが触ってはいけないピン」のビットの表を、
   **ビルド時の定数** (RAM を使わない) として作る。対象は、その機種でシステムが使っているピン全部:
   画面 (DSI・SPI・バックライト)・SD・SDIO (WiFi の子チップ)・I2S・リンクの SPI・USB・Ethernet・flash と PSRAM・
   起動の strap・JTAG・UART0 (シリアルのログ)・システムの I2C・LED・電源の制御 ほか。**`fmrb_pin_assign.h` に名前が
   ある物は名前から、チップが内部で使っていて名前の無い物 (flash・PSRAM のピンなど) は `FMRB_PIN_RESTRICTED_*` の
   今ある定義から**。根拠 (どの定義・どの回路) を機種ごとの表にして報告に書く。分からないピンは入れず、報告に並べる。
   - ボタン (F4 の「入力として読んでよい」) はこの表に入れない (入力だけ通す、のまま)。
   - Grove・拡張のピンなど、アプリに使わせる想定のピンは入れない。機種ごとに「アプリが使えるピン」の一覧も報告に書く。
2. **判定に足す**: F4 の判定 (`fmrb_hal_pin_manager` の、GPIO が尋ねる関数) で、台帳より先にこの表を見る。表にある
   ピンの `GPIO.new`・`set_dir`・`set_pull`・`write`・`*_at` の設定は IOError ("GPIO n is used by the system")。
   読むだけ (`read_at`・`high_at?`・`low_at?`) は今までどおり通す (gpio_viewer)。台帳は変えない (I2C・RMT など
   GPIO 以外の動きは変えない)。
   - `FmrbHw.pin_status`・`pin_status_all`・`pin_available?` (gpio_viewer が使う) が、この表のピンを「システム」と
     答えるようにするか、今のまま (台帳のとおり) にするかは、gpio_viewer の表示と sig の説明 (`sig/fmrb_hw.rbs`) を
     読んで、**アプリを書く人が「このピンは使えない」と分かる方**にする。どうしたかと理由を報告に書く。
3. **同梱のアプリと見本を調べる** (`flash/app`・`flash/usr/share/samples`・`flash/lib`): `GPIO`・`I2C`・`RMT`・
   `FmrbHw` でピンの番号を渡している所を全部挙げ、各機種でこの表に当たるかを表にする。F4 の報告にある
   step_sensor の 11 番 (P4-Nano では I2S、NARYAv3 ではリンクの SPI) を含む。**今動いている物が動かなくなる組み合わせが
   新しく出るなら、そこを報告に書いて止まる** (step_sensor は StickS3 用のアプリで、ほかの機種では rescue して続く。
   これは今までどおりなら止まらなくてよい)。
4. **試験**: F4 のホストの試験 (`test/api_fixes/pin_ledger/`。5 機種を本物の `fmrb_pin_assign.h` で組む) に、機種ごとの
   表の中身 (代表のピンが入っている・ボタンと拡張のピンが入っていない) と判定を足す。
5. sig (`sig/gpio.rbs`・`sig/fmrb_hw.rbs`) の説明を合わせる。機種ごとの使えるピンは長文に書きすぎず、
   `FmrbHw.pin_available?` で確かめる、と案内する。

## 確かめる

- `rake regress`・`rake sig:coverage` (「3. doc gaps」が 0 のまま)、`rake clean` → `rake build:linux` → sim で
  gpio_viewer が開く・エディタの起動 + 1 打鍵。
- ESP32 だけのコードは、ホストの試験と構文検査まで (F4 と同じ)。直した ESP32 だけのファイルを報告に並べる
  (親が P4-Nano 用にビルドして焼いて確かめる)。**親が P4-Nano で流す式** (1 行 60 文字以内。SD のピン 39 の
  `GPIO.new` が IOError で、48 は取れる、など) を報告に書く。
- 内蔵 RAM の増分を報告する (表は定数なので 0 のはず)。ファームの増分 (バイト) も。**AtomS3R・StickS3 の区画に
  入ること** (空きは約 520 KB)。

## 報告 (doc/api_fixes/report/f6.md)

機種ごとの表 (触ってはいけないピンと根拠、アプリが使えるピン)、同梱のアプリへの影響の表、`pin_status` の扱い、
試験、ESP32 だけのファイル、親が実機で流す式、確かめていないこと、コミット。

## 範囲と止まる条件

- 触ってよい: pin manager と HAL (GPIO が尋ねる所)、`fmrb_pin_assign.h` (表のための定義を足す分。**番号は変えない**)、
  `lib/upstream/patch/picoruby-gpio`・`picoruby-gpio-proxy` (要れば)、試験、`sig/gpio.rbs`・`sig/fmrb_hw.rbs`、
  `doc/api_fixes/report/f6.md`、`doc/README.md`。
- 変えない: 台帳に登録するピン、I2C・RMT の動き、`components/` の submodule の中、sdkconfig。
- 利用者から見た動きが「表にあるピンの GPIO の設定が IOError になる」以外に変わる直し方が要るときは止まって報告する。
- push しない・マージしない (ブランチ `api-fixes-f6`)。ユーザに質問しない。終わったら sim を止める。
- シリアルのポートと実機には触らない。ESP32 のビルドはしない (`build/` は P4-Nano 用)。
  コメントとコミットは英語、報告は日本語 (常体)。実在の値を書かない。
