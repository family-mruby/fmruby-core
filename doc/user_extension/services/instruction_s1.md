# 実装指示書 S1: ユーザサービスのホストと ps/kill 管理

対象: 実装担当セッション。作業リポジトリ: fmruby-core のみ。

先に読むもの (この順で):

1. **plan.md (同ディレクトリ)** — 仕様の本体。契約・toml・優先度・単発・
   二層・ps/kill の要求/応答は全部そちらに書いてある。本書は進め方と
   コードの当たりだけ。
2. ../ideas.md — 背景 (この機能が 11 案のどこに立つか)。
3. doc/picorabbit/instruction_p0_p1.md の「進め方の約束」 — sim の罠
   (stale build の偽グリーン、3 コンテナ再起動、.env の復元) をそのまま適用。
4. doc/spinel_aot/ruby_writing_constraints.md — **kernel に触る 1 か所**は
   dual-safe (保存ブロックなし、bare 定数なし) で書くため。

報告は report/s1.md (ディレクトリは作る)。

## 範囲

plan.md の **S1 + S1.5**: ホスト、契約、二層 (システム/ユーザ)、oneshot、
`app =` 自動起動 (fullscreen 上書き込み)、kernel のブート spawn、
優先度の新設、shell の ps / kill / svc。システムサービスの実体は
**clock 1 本だけ** (候補表の残りはやらない)。

## コードの当たり (調べてある入口)

| こと | 場所 |
|---|---|
| Pub/Sub の API | lib/add/picoruby-fmrb-app/mrblib/fmrb-app.rb の `subscribe` / `unsubscribe` / `publish` (515 行付近)。配信は `on_control` に `"cmd"=>"topic_data"` で届く (flash/app/demo/sub_demo.app.rb が実例) |
| アプリからの spawn 要求 | fmrb-app.rb 537 行付近 (「アプリはアプリを spawn できないので kernel への要求」の既存 API)。launcher (system_desktop/launcher.rb 780 行付近) が `{"cmd"=>"spawn"}` を送る実例。kernel 側の受けは fmrb_kernel.rb 172 行付近 |
| spawn 時の fullscreen | spawn 属性に `fullscreen` フラグが既にある (main/app/fmrb_app_spawner.c が app.toml の `default_window_mode` から立てている、630 行付近)。**要求メッセージに上書きを載せられるか先に読む**。載らなければ spawn 後に `fmrb_app_set_fullscreen(pid, true, w, h)` (main/app/fmrb_app.c 2820 行) |
| kernel のブート spawn | fmrb_kernel.rb 606 行付近 (desktop の spawn)。この後ろに足す |
| 優先度 | components/fmrb_common/include/fmrb_task_config.h に `FMRB_SERVICE_APP_PRIORITY (1)` を新設。spawn 経路でこのアプリだけ 1 を使う方法は spawner を読んで決める (app.toml に priority 項目を足すのが素直なら、それでもよい。**一般アプリが 2 より上を指定できない**ようには縛る) |
| /etc/services.toml の生成 | rakelib/build.rake 16 行付近 (system_conf を config/ から flash/etc/ へ cp している)。`config/services.toml` を足して同じ形で cp |
| shell | main/prebuild_scripts/default_app/shell/shell_commands.rb (`cmd_ps` 841 行、`cmd_kill` 878 行)。**kill が kernel の `kill_result` を非同期に受けて印字する形が、svc の応答表示のそのまま手本** |
| ホストの置き場所 | main/prebuild_scripts/default_app/services.app.rb (新規、headless、launcher_visible = false) |
| ファイルの同梱 | システム: flash/usr/share/services/clock.rb と config/services.toml。ユーザ既定: flash/home/services.toml と flash/home/services/ (heartbeat / hourly_chime / broken) |

## 進め方

- タスク順: **T1 ホスト + 契約 + サンプル → T2 kernel spawn + 優先度 +
  生成 → T3 `app =` 自動起動 → T4 shell (S1.5)**。
- **関門は 2 つ**:
  - 関門 1: T1-T3 を実装し、plan.md の「検収 (sim, headless)」の表を
    通した時点で report を見せて止まる (shell に触る前)。
  - 関門 2: T4 完了時 (ps の子行・kill 名前・svc start の実演)。
- コミットは 3 本 (ユーザ確認のうえ、英語):
  1. ホスト + 契約 + サンプル + kernel spawn + 優先度 + 生成 (T1-T3)
  2. shell の ps / kill / svc (T4)
  3. 本書と plan.md ほか docs (ideas.md 含む user_extension 一式が未追跡
     なのでまとめて。1 に含めてもよい)
- kernel (fmrb_kernel.rb) の変更は**標準 (Spinel カーネル) と全 mruby の
  2 構成**でビルド + sim 起動を確認 (engine 方針どおり)。
  `rake spinel:doctor` の新規指摘 0。
- ホスト・サンプル・shell は mruby 専用なので dual-safe 縛りは不要。
  ただし書き方の基本 (picoruby の地雷) は fmrb-app-new skill に従う。
- `rake test` (host テスト) が通ること。ホストの契約部分 (toml の併合、
  上書き、oneshot の外れ方、エラー 3 回で failed) は**ロジックを
  ホストテストにできる形に切る** (ファイルとメッセージに触らない純粋な
  部分を分け、test/ に 1 本足す。FmrbUI の run.rb と同じやり方)。

## plan.md からの補足 (実装の決め)

- toml の読解: 既存アプリがやっている toml の読み方に合わせる (kernel は
  C の fmrb_toml。Ruby 側に toml 読みが無ければ、**services.toml の文法を
  「1 行 = key = value と [名前]」の素朴な部分集合に限定**して自前で読む。
  どちらにしたかを report に書く)。
- `clock/hour` の配信は分の変わり目で判定 (秒精度は要らない)。RTC が
  未設定 (1970 年) の間は publish しない。
- 検収の「配送」は pub_demo からでよいが、`svc/ctl` の応答検査は
  一時アプリ (コミットしない) で行ってよい。
- broken.rb は `enable = false` で同梱し、検収のときだけ /home 側の toml を
  書き換えて有効化する (sim の storage は build で作り直されるので、
  実機の /home を汚さない)。

## 受け入れ条件

- plan.md の検収表 (二層・oneshot・アプリ起動・隔離・警告・ヒープ・費用・
  終了) + S1.5 の実演が report/s1.md に画像とログつきで揃う。
- 2 構成 (標準 / 全 mruby) でビルドと sim 起動が通る。doctor 新規指摘 0。
- `rake test` 通過 (足したホストテスト含む)。
- 実機 (Tab5): ブートでホストが上がる、M1 の `spawn:` 行と `fmrb_task:` の
  スタック実測、ps の子行が shell で見える、の 3 点だけ。
- `.env` が作業前の値に戻っている。

## やらないこと (plan.md の「やらないこと」に加えて)

- clock 以外のシステムサービス実体 (net / timesync / flightrec は候補表の
  まま)。
- monitor の Tasks ページ統合、自動再 spawn、`restart = true` (S2)。
- 真のキオスク (desktop を出さない起動)。
