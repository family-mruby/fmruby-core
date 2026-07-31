# Phase 3: gfx/app バインディングとデモアプリ、ランチャー対応

## 目的

Lua に提供している描画 API と同じ面を Python から使えるようにし、
ランチャーから起動できる見栄えのするデモアプリを追加する。

## 提供 API (Lua 版と同一の面)

components/lua/extension/fmrb_lua_gfx.c が現物仕様。同じ名前・引数順で揃える。

- app モジュール
  - `app.create_canvas(...)` — ウィンドウ付きキャンバス生成 (引数は Lua 版に合わせる)
  - `app.sleep(ms)` — 待機。内部で ctx->should_exit を監視し、停止要求で
    即座に例外脱出する (Lua 版 lua_app_sleep の挙動を読んで合わせる)
- gfx (キャンバスオブジェクトのメソッド)
  - fill_rect / draw_rect / fill_round_rect / draw_round_rect /
    draw_line / fill_circle / draw_text / present / clear

Python 側の見た目は `import fmrb` のような単一モジュールでも、Lua と同じく
app / gfx の 2 モジュールでもよいが、**Lua 版とかけ離れた独自設計にしない**。
キャンバスは MicroPython のオブジェクト (mp_obj_t) として実装し、
ネイティブ側ハンドルを保持する。

## 作業項目

1. **C モジュール実装**: components/micropython/modules/ に
   fmrb_mod_app.c / fmrb_mod_gfx.c を作成し、MP_REGISTER_MODULE で登録する。
   fmrb_lua_gfx.c の各関数と 1:1 対応させ、内部で呼ぶ fmrb_gfx / メッセージ
   API も同じものを使う。CMakeLists.txt の SRCS と REQUIRES
   (fmrb_msg fmrb_gfx) を更新する。

2. **qstr 再生成**: モジュールのソースを phase0 で確定した手順で生成対象に
   含め、rake micropython:gen を再実行して mp_embed/ を更新・コミットする。
   以後「modules/ を触ったら micropython:gen」の運用を README に追記する。

3. **デモアプリ**: flash/app/demo/python.app.py + python.app.toml を追加する。
   雛形は flash/app/demo/lua.app.toml / lua.app.lua。内容は Lua デモと同等の
   描画 (矩形・円・線・テキスト・present ループ) に、Python らしい要素
   (リスト内包表記など) を少し足したもの。phase2 の pytest.app.py は
   ここで整理 (残すなら test 用と分かる置き場へ)。

4. **デスクトップ側の拡張子対応**: 分割ソース
   main/prebuild_scripts/kernel/system_desktop/ 以下を編集する。
   - launcher.rb: SCRIPT_EXTS に "py"、拡張子->表示文字の表に "P"。
     アイコン (usr/share/icon/*.icon) は任意。追加する場合は rake icons で
     BMP を再生成する。
   - file_manager.rb: 対象拡張子判定 (.rb/.lua/.bas の並び) に .py を追加。
   - **注意**: main/prebuild_scripts/kernel/mrb/system_desktop_combined.rb に
     同じコードが結合された形で存在する。この結合ファイルがどのタスクで
     再生成されるかを最初に確認し (再生成されないなら両方を同じ内容に
     編集する)、分割ソースと結合ファイルの不一致を残さない。
   - Spinel 構成のデスクトップを使う場合は rake spinel:gen の再実行も必要。
   - shell からの起動経路 (prebuild_scripts/default_app の shell コマンド) に
     lua.app のような特例名があれば python.app も同様に足す (任意)。

## 検証手順 (headless)

1. rake build:linux + tools/dev_run_check.sh --keep でデスクトップ起動。
2. ruby tools/fmrb_input.rb でランチャーを開き、python.app をダブルクリック
   起動 (座標は Launcher の並びをスクリーンショットで確認して決める)。
3. python3 tools/fmrb_screenshot.py で撮影し、デモの描画内容
   (矩形・円・テキスト) が出ていることを画像で確認する。
4. ウィンドウを閉じて停止し、再度起動できることを確認する。
5. lua.app と python.app を同時に動かし、両方のウィンドウが描画される
   ことを確認する (Lua と Python の共存確認。Python 同士は排他)。

## 完了条件

- 上記検証 1-5 がすべて通り、3 のスクリーンショットで描画が確認できる。
- app.sleep 中の停止要求が効く (sleep の長いスクリプトを kill して確認)。
- modules/ 変更 -> micropython:gen -> ビルド、の手順が README に追記済み。
