# R2 指示書: TOML の読み書き FmrbToml を作り、ばらばらの読み手を寄せる

> 状態: 指示 | 更新: 2026-10-25 | plan.md の R2。ファームにある C の TOML の読み手 (`components/fmrb_toml`、中身は tomlc99) に Ruby の口を付けて `FmrbToml` とし、Ruby の各所にある自前の小さな TOML の読み手をこれに寄せる。書き戻しは Ruby で「1 つの鍵の行だけ書き換え、ほかの行とコメントを残す」。FmrbApp にはアプリごとの設定ファイルの薄い入口を足す。置き場はファーム (gem) (2026-10-25 ユーザ)

言葉: 文書は常体、「確かめ」ではなく「確認」。コードのコメントは英語。

## 1. 最初に確かめる (結果を report に。合わなければ止まる)

1. `components/fmrb_toml` (と `components/toml_parser/tomlc99`) が、ESP32 (Tab5・Retro・StickS3)・Linux の sim・ブラウザ版 (wasm) の全部でビルドに入っているか。入っていない構成があれば、入れるのに要る変更の大きさ。
2. tomlc99 の確保がどこから取られるか (`fmrb_toml_init` の allocator)。アプリの VM から呼んだとき内蔵 RAM を使わないこと (アプリのタスクでは fmrb_malloc の決まり、CLAUDE.md)。
3. Spinel で動くアプリ・カーネルから、mrbgem の C の口が使えるか (Spinel のアプリは C の関数をどう呼ぶか、既存の gem の例を読む)。使えないなら、Spinel 側では R2 の対象外にしてよいか判断材料を report に (止まって返す)。
4. 今ある自前の読み手の一覧と、それぞれが扱っている書式 (表・配列・文字列のエスケープ・16 進の数・コメント)。少なくとも: `SvcConf.parse` (main/prebuild_scripts/default_app/services/registry.rb)、`FmrbColors.parse` (lib/add/picoruby-fmrb-app/mrblib/fmrb-colors.rb)、`FmrbAssoc` (fmrb-assoc.rb)、`FmrbAsterism.settings` の config.toml の読み書き (fmrb-asterism.rb)、FmrbI18n と FmrbApp のテーマ (system_conf の読み)、アプリの conf_int・save_tune (step_sensor、オセロ、walk_scroller の台本、麻雀の補助の設定)。

## 2. 作るもの

1. **`FmrbToml`** (picoruby-fmrb-app か新しい gem。lib/add の決まりで):
   - `FmrbToml.load(path)` → Hash (表は Hash、配列は Array、表の配列は Array of Hash、文字列・整数・小数・真偽)。読めない・書式の誤りは nil と理由 (例外にするかは既存の流儀に合わせて決める)。`FmrbToml.parse(text)` も。
   - `FmrbToml.update(path, values)` → 1 つ以上の鍵 (上の段、または `"section.key"`) の行だけを書き換え、無ければ足し、ほかの行とコメントは残す。書いたあと読み直して確かめ、結果を返す。
   - 小さな値の書き方 (文字列の引用、数、真偽) を 1 か所に。
2. **FmrbApp の入口** (mruby 版 `lib/add/picoruby-fmrb-app/mrblib/fmrb-app.rb` と、Spinel 版 `main/prebuild_scripts/spinel/fmrb_app_base_spinel.rb` の両方。Spinel が C の口を呼べない場合は 1-3 の判断に従う): `app_settings` (→ `/home/<app_handle_name>.toml` を FmrbToml.load、無ければ空の Hash) と `save_app_settings(values)` (→ FmrbToml.update)。**基底クラスにインスタンス変数を足さない** (Spinel で親子の ivar の配置がずれた前例: doc/spinel_aot/reports/editor_ivar_layout_bug.md)。
3. **寄せる**: 1-4 の読み手を FmrbToml に置き換える。振る舞い (読む値、既定値、書いたファイルの形) を変えない。順番は、試験しやすいもの (アプリの conf_int・save_tune、FmrbColors、FmrbAssoc) → FmrbAsterism.settings → SvcConf・I18n・テーマ。置き換えると危ないもの (起動の早い段階で読むものなど) は、理由を report に書いて残してよい。
4. **試験**: FmrbToml の読み (C の口なので sim のアプリか、ホストで C ごと試せる形) と update (行の保持、コメント、鍵の追加、section)。既存の各読み手の試験があればそのまま通す。

## 3. 確認

- `rake test`、新しい試験。標準構成の sim でエディタを 1 回起動して 1 打鍵。
- sim (Modern と Retro): デスクトップ・サービス・色・関連付け・Asterism の設定が前と同じに読める (起動ログ、画面)。step_sensor (sim) の tune → save → 起動し直して残る。walk_scroller (ブラウザ版) の台本 (`[[mark]]`) が前と同じに読める。
- 内蔵 RAM: Tab5・Retro・StickS3 のビルドの `idf.py size` の DIRAM が増えない (前後で比べる)。ファームの大きさの増分も report に。
- 実機は使わない (StickS3 も焼かない。焼くのは親とユーザが後で)。

## 4. 範囲・決まり

- **触ってよいもの**: fmruby-core (develop) の lib/add の gem、`components/fmrb_toml` の口 (要る範囲)、上の読み手とそれを使うアプリ、FmrbApp の 2 か所、試験、report。lib/ を触ったら `rake clean`、ターゲットの切り替えは `rake clean_all`。
- **触らないもの**: sdkconfig、asterism の gem 本体、asterism-games・console、実機 (Tab5・StickS3・シリアル)、VPS、`~/asterism_keys/`。
- push・タグはしない。`git commit -a` は使わない。件名 `app:` `services:` `toml:` など履歴に合わせ、本文英語、末尾 `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`。doc (この指示書・report) はコミットしない (親がする)。
- 終わったら sim・web を片付ける。
- **止まる条件**: fmrb_toml が入っていない構成があり入れるのに大きな変更が要る、内蔵 RAM が増える、Spinel から使えない (判断材料を返す)、振る舞いを変えないと寄せられない読み手がある (一覧にして返す)。
- **report**: `doc/app_refactor/report/r2.md` (常体): 1 の確認結果、FmrbToml と入口の形、寄せた読み手と残したもの (理由)、行数の前後、試験、RAM とファームの大きさ、残件、コミット。
