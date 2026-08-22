# 段 3 計画: 地の責任を 1 か所に集める

issues_s3.md の棚卸しを受けた実装計画。段 3 の目標は**作者の契約を減らす**こと
(補足の 8 項目のうち 4 と 5 を消し、6 を狭め、3 を検出可能にする)。

## 方針 (ユーザと合意済み)

課題 A・B・D は「地 (部品の後ろにある絵) の知識を誰が持つか」が決まっていない
という同じ穴から出ている。個別の対症でなく、**地を描く口を 1 つ決める**。

- 口の形は「**受け手を渡し、固定名メソッドを呼ぶ**」。素の Ruby ならブロックや
  Method オブジェクトで書く場面だが、Spinel は動的ディスパッチ不可
  (ruby_writing_constraints.md) で、保存ブロックは外側のローカルを触った
  ときだけ静かに壊れる。固定名メソッドなら書けるものは全部動き、崖が無い。
- **受け手は可変にする** (`bg_painter:`)。デスクトップのように面ごとに地が
  違うアプリが、判断ごと持ち込めるようにするため。
- 部品のアクションは id + case のまま。発火はクリック速度なので性能上の
  障害は無い (ブロック呼び出し 0.4ms は反復の中でだけ効く) が、保存ブロックの
  「ローカル捕捉だけ静かに死ぬ」崖が契約として悪質なので採らない。

## 作業項目

段 2 の S1-S4 と衝突しないよう、段 3 は T1-T5 と呼ぶ。実装ごとの報告は
report/t1.md ... に置く。編集先は lib/add/picoruby-fmrb-app/ のみ
(components/ 以下の複製は上書きで消える)。

### T1: イベント列テスト (課題 C) — 最初にやる

後続の T2/T3 の回帰網になるので先に書く。

- test/fmrb_ui/run.rb にイベント列ヘルパを足す:

  ```ruby
  # 列を順に流し、handle が返した id を (nil を捨てずに) 並べて返す
  def feed(ui, seq)
    seq.map { |e| ui.handle(e) }
  end
  ```

  検査は**発火回数**を数える形で書く (4 番の 2 段送りは回数でしか捕まらない)。
- 全部品に最低 1 本ずつ:
  - down + up の 1 クリックで id が**ちょうど 1 回**出る
  - down は部品内・up は部品外で **0 回**
  - `fires_on_press?` の部品 (Scrollbar) は down で 1 回、up で 0 回
  - down/up を 2 周して 2 回 (状態が残らないこと)
- 4 番の実列 (スクロールバー矢印の 1 クリック) をそのまま回帰テストに固定する。

完了条件: `ruby test/fmrb_ui/run.rb` が全通過。既存 112 項目に影響なし。

### T2: 地の描画の口 `bg_painter` (課題 A)

- `FmrbUI#initialize` に `bg_painter: nil` を足し、`flush` の非表示 dirty の
  分岐を変える:

  ```ruby
  p = @bg_painter
  if p
    p.paint_bg_rect(@gfx, w.x, w.y, w.w, w.h)
  else
    @gfx.fill_rect(w.x, w.y, w.w, w.h, @bg)   # 従来と同値
  end
  ```

  既定 (nil) は現行と同じ挙動なので、**既存アプリは全て無変更**。
- `paint_bg_rect(gfx, x, y, w, h)` の契約 (実装側が守るもの):
  - その矩形の地を描くだけ。present しない。確保しない (flush の定常経路から
    呼ばれる)。文字サイズやフォントを変えるなら戻す。
- Spinel 上の注意: 受信が poly になるので**メソッド名はプログラム全体で一意**
  (`draw_widget` を `draw` にしなかったのと同じ理由)。`paint_bg_rect` は
  現状どこにも無いことを確認済み。実装が 1 つでもコンパイル不能だと全体が
  落ちるので、実装は素朴な描画列に限る。
- 最初の利用者はデスクトップ: `FmrbUI.new(self, bg_painter: self)` とし、
  `paint_bg_rect` は前面 canvas の透明キー 0x01 で塗る (壁紙は背面 canvas に
  あるので、透明に戻せば合成で壁紙が出る)。ダイアログ枠の内側かどうかで
  塗り分けが要るなら、受け手の中で判断する (口は矩形を渡すだけ)。
  2 番 (白い矩形) の症状がこの経路で再発しないことを sim で見る。
- monitor は無変更 (既定経路のまま。Tasks ページの kill ボタン消しが
  従来どおり動くことを sim で見る)。
- `bg:` の説明を「Label の箱や Stepper の値欄など、**部品が自分の中を塗る色**」
  に狭める (「消えた跡の色」の役割は bg_painter 側へ移ったため)。
  fmrb-ui.rb 冒頭コメントと Widget#bg のコメントを直す。
- ホストテスト: 記録用の FakeBgPainter を fake_gfx.rb に足し、
  「painter ありなら paint_bg_rect が正しい矩形で呼ばれ @bg の fill_rect が
  出ない」「painter なしなら従来どおり」を検査。

完了条件: ホストテスト通過 + sim でデスクトップのダイアログ開閉と monitor の
Tasks ページを確認 (確認手順は下の「検収」)。

### T3: clear_user_area に後始末を内蔵する (課題 B)

契約 5 (全面を消したら invalidate_all + draw_window_frame) を基底に移して消す。

- `FmrbAppBase#clear_user_area` を変更:

  ```ruby
  def clear_user_area(color = FmrbConst::THEME_WINDOW_BG)
    return unless @gfx
    @gfx.fill_rect(@user_area_x0, @user_area_y0,
                   @user_area_width, @user_area_height, color)
    # user area の矩形は下 2 隅の丸い縁と透明キー画素を巻き込むので、
    # 枠の再描画までがこのメソッドの仕事
    draw_window_frame unless @bg_canvas || @fullscreen
    _invalidate_attached_uis
  end
  ```

  - `@bg_canvas` ガードは必須 (デスクトップは枠を持たない。素通しすると
    `_build_frame_block` が走ってタイトルバーが描かれてしまう)。
  - `@fullscreen` は draw_window_frame 側でも弾かれるが、意図を明示する。
- FmrbUI を基底に知らせる: `FmrbUI#initialize` の末尾で `app.attach_ui(self)`
  を呼ぶ。基底は `initialize` で `@attached_uis = []` を持ち、

  ```ruby
  def attach_ui(ui)
    @attached_uis << ui
  end
  ```

  `_invalidate_attached_uis` は while で回して `invalidate_all` を呼ぶだけ。
  複数 UI (ページごとに分ける将来の形) もこれで受かる。
- **同じ変更を 2 か所に入れる**: lib/add/picoruby-fmrb-app/mrblib/fmrb-app.rb と
  main/prebuild_scripts/spinel/fmrb_app_base_spinel.rb (Spinel 用基底が
  clear_user_area と draw_window_frame を別に持っている)。片方だけ直すと
  engine 切替で挙動が割れる。
- ホストテストの FakeApp に `attach_ui` の空実装と window_width/height を足す
  (T4 も使う)。
- 費用の確認: clear_user_area は 43 か所から呼ばれ、大半は on_create だが、
  shell は再描画経路で呼ぶ。draw_window_frame は GfxBlock 再生 + フォント
  退避なので、shell のスクロールで体感が変わらないか sim で見る。
  重ければ「枠全体でなく丸縁 4 隅 + 透明キーの再打刻だけ」の縮小版
  (`_repair_corners`) に落とす。まず素朴な形で入れ、計測してから決める。
- これで nsf/smf が踏んだ 6 番 (角の丸み消え) は構造的に再発しない。
  両アプリに残る手書きの `draw_window_frame` / `invalidate_all` 呼びは
  **冗長になるだけで害は無い**ので、この段では消さない (触る理由ができた
  ときに落とす)。

完了条件: ホストテスト通過 + sim で nsf_player の角、shell のスクロール、
Spinel 構成のビルドを確認。

### T4: 生成時の範囲警告 (課題 D)

座標系の間違いを「静かにずれる」から「生成時に一言出る」に変える。
明示 API (`origin:` 必須化) は全アプリ改修の割に間違いを黙って選べる構図が
残るので採らない。`set_origin` は現状のまま。

- `FmrbUI#initialize` で `@win_w = app.window_width`、`@win_h = app.window_height`
  を控え、`add(w)` で:

  ```ruby
  if w.x < 0 || w.y < 0 || w.x + w.w > @win_w || w.y + w.h > @win_h
    Log.warn("FmrbUI: widget #{w.id} outside window (#{w.x},#{w.y},#{w.w},#{w.h})")
  end
  ```

  生成時 (on_create) だけなので文字列補間の確保は構わない。
- 1 番の「11px 下にはみ出たボタン」はこれで生成ログに出る。
- `move` では警告しない (monitor が実行時に呼ぶ。定常経路に確保を持ち込まない)。
  この非対称はコメントに書く。
- 既存アプリ全部を sim で一度起動し、**現状の座標で警告が出ない**ことを確認
  する (出たらそれ自体が直すべきはみ出し)。

完了条件: はみ出し部品を作るテストで warn が出る (FakeGfx 側で Log を差し替えて
検査)。既存アプリの起動ログに warn が出ない。

### T5: 文書・契約表・型の更新 (課題 E ほか)

- issues_s3.md 補足の契約表を改訂して fmrb-ui.rb 冒頭コメントに反映:
  - 4 (閉じるときは隠してから flush) — **残る**が「消えた跡は bg_painter が
    描く」に説明が変わる
  - 5 (全面を消したら invalidate_all + draw_window_frame) — **消える**
  - 6 (暗い背景なら bg:) — 「bg: は部品が自分の中を塗る色」に**狭まる**
  - 3 (座標系) — 残るが生成時警告で**検出可能**になる
  - 新規: bg_painter を渡すなら paint_bg_rect の契約 (描くだけ・present しない・
    確保しない・名前は固定)
- fmrb-app-new skill の該当記述を更新 (「resizable は draw_window_frame」の
  条は基底内蔵後は不要になる。契約一覧も同期)。
- sig/fmrb_ui.rbs に `bg_painter` 引数と `paint_bg_rect` を持つ interface を
  追記。**sig を直したら rake clean してからビルド** (sig/README.md)。
- 確認手順 (課題 E) を doc/ui_widgets/verify.md として定型化:
  - 枠と部品の境界は**ピクセルで数える** (境界の y を走査して並びを読む)
  - 「自分の変更が原因か」は**変更前の版を同じビルドに一時アプリとして同梱**
    して並べる
  T2/T3 の検収でこの手順を実際に使い、書き足りない所を埋める。

## 検収 (sim)

ビルドは標準 (Spinel カーネル) と全 mruby の 2 構成 (engine 方針どおり)。
`rake build:linux` 後は `file build/fmruby-core.elf` で x86-64 を確認する。

| 場面 | 見るもの | 対応 |
|---|---|---|
| デスクトップ: ダイアログ開閉 | 白い矩形が出ない。壁紙が透けて戻る | T2 (painter 経路) |
| monitor Tasks: タスク減少 | kill ボタンの跡が従来どおり消える | T2 (既定経路) |
| nsf_player: 起動と停止 | 四隅の丸みが保たれる | T3 |
| shell: 連続スクロール | 体感が変わらない (重ければ縮小版へ) | T3 |
| スクロールバー矢印 1 クリック | 1 段だけ送られる | T1 (回帰) |
| 全アプリ起動ログ | FmrbUI の warn が出ない | T4 |

境界の検査は verify.md の手順 (ピクセル走査) で行い、目視だけで済ませない。

## 順序と理由

T1 (網を張る) -> T2 (地の口) -> T3 (基底の後始末) -> T4 (警告) -> T5 (文書)。
T2 と T3 は独立だが、どちらも T1 の網の上で行う。T3 は 2 ファイル同時変更
(mruby 基底 / Spinel 基底) なので、engine 2 構成の検収を T3 の直後に一度回す。

## やらないこと (段 3 では)

- 部品アクションのブロック / コールバック化 (上記の方針どおり)
- List 部品・Slider・config 設定行の部品化 (issues_s3.md の判断を維持)
- nsf/smf に残る冗長な手書き後始末の削除 (害が無いので触る理由ができるまで)
- `move` の範囲警告 (定常経路に確保を持ち込まない)
