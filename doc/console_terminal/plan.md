# asterism-console の Terminal (ブラウザから CLI で操作する)

> 状態: 計画済 | 更新: 2026-11-06 | console のページに端末を置き、asterism gem の CLI と同じコマンドを、console のログインと権限と記録の下で打てるようにする。サーバの OS のシェルは出さない。段は VT1〜VT4、未確定事項 5 つ

## 目的

- asterism-console (Rails、網の図とメソッドの呼び出し) の中に **Terminal** を置き、asterism gem の CLI (`asterism` コマンド) と同じ操作を、ブラウザから打てるようにする。
- 手元に gem や証明書を入れなくても、console にログインすればどこからでも (外出先のスマホやタブレットからでも) 網を操作できる。
- 手元の CLI に無いものを足す: **console の役割による権限**と、**誰が何を打ったかの記録**。
- 図とコマンドを行き来できるようにする (図で選んだものを Terminal で使う、Terminal で見ているものを図で光らせる)。
- 先の構想 (FmrbKernel と Asterism の統合、private-notes の構想メモ) が進んだら、全端末の `ps`・`kill`・`move`・ファイルの送受や、**端末の Shell** に console から入れる入口にする。

## 今の形 (2026-11-06)

- asterism gem の CLI (`lib/asterism/cli.rb`):
  - 一覧と中身: `node list|info`、`app list|info`、`object list|info|find|get|echo`、`tree`
  - 状態: `status get|echo|hz`、`watch` (ノード・アプリ・オブジェクトの出入りと状態の変化)
  - 操作: `call PATH METHOD [ARG...]`、`ping`
  - そのほか: `mcp` (stdio の MCP サーバ)、`completion bash|zsh` と補完の口 `__complete`
  - 出力先を差し替えられる作り (`Asterism::CLI.new(out:, err:, env:)`)。
- asterism-console: Rails。網の図、メソッドの呼び出し、記録と再生 (V4)、部屋と役割 (G1.5・G2)、ログイン (O1)。ブラウザと常に話す仕組み (ActionCable) がある。console 自身が Zenoh のセッションを持ち、`console` と名乗る。

## 方針

1. **同じ実装を使う**: Terminal のコマンドは asterism gem の CLI をそのまま console のサーバの中で動かし、出力をブラウザに流す。コマンドの名前・引数・出力は手元の CLI と同じにする (覚えることを増やさない)。補完は `__complete` をそのまま使う。
2. **サーバの OS のシェルは出さない**: 打てるのは Asterism のコマンドだけ。コマンドの一覧に無いものは受けない。ファイルの読み書き・外のプロセスの起動につながるもの (`mcp`、`completion` など) は Terminal では使わせない。
3. **console のセッションで動かす**: 網へは console が既に持っている Zenoh のセッション (と console の証明書・ACL) で出る。打った人ごとに別の証明書を使うことはしない。そのかわり、誰が打ったかを console が記録し、役割で絞る。
4. **権限は 2 段**: 読むだけのコマンド (list・info・find・get・echo・status・watch・tree・ping) と、何かを起こすコマンド (call、将来の kill・move・ファイルの送受) を分け、役割ごとに許すかを決める。
5. **流れ続けるコマンドを止められる**: `watch`・`echo`・`status echo` は Ctrl+C で止まり、ページを閉じても止まる。1 人が同時に流せる数に上限を持つ。
6. **console を OSS として配る前提を崩さない**: 作者の環境 (アドレス・ノード名) を焼き込まない。違いは設定で吸収する。

## スコープ (段)

| 段 | 内容 | 状態 |
|---|---|---|
| VT1 | 読むだけの Terminal: console のページに端末 (ブラウザの端末の部品) を置き、読むだけのコマンドを打てる。補完 (Tab)、履歴 (↑↓)、Ctrl+C で止める、色つきの出力 (CLI の色をそのまま)。ログインした人だけ | 未着手 |
| VT2 | 操作と権限と記録: `call` を打てるようにし、役割ごとに許すコマンドを決める。打ったコマンドと結果の要約を console の記録に残す (誰が・いつ・何を)。一覧から消せない | 未着手 |
| VT3 | 図とのつながり: 図で選んだノード・アプリ・オブジェクトのパスを Terminal に入れる、Terminal で `watch`・`echo` しているものを図で示す、図の右クリックから同じコマンドを開く | 未着手 |
| VT4 | 端末の OS につなぐ (FmrbKernel と Asterism の統合が進んでから): 全端末のアプリの一覧・停止・移動・ファイルの送受をコマンドにする (doc/asterism_file_transfer、doc/checkpoint)。端末の Shell をオブジェクトとして出し、console の Terminal からその Shell に入る (Asterism 越しの ssh のようなもの) | 構想 |

## 受け入れ条件

- VT1: ブラウザの Terminal で `node list`・`tree`・`object info <path>`・`status get <node/app>` が手元の CLI と同じ出力を返す。`watch` を流して Ctrl+C で止まる。ページを閉じると流れていたコマンドが止まる (サーバに残らない)。Tab で補完される。ログインしていない人は Terminal を開けない。OS のコマンド (`ls` など) やコマンドの一覧に無いものは理由つきで断られる。
- VT2: 役割で許していない `call` は理由つきで断られる。打った `call` が記録に残り、誰が打ったか分かる。
- VT3: 図で選んだオブジェクトのパスが Terminal の入力に入る。
- どの段も、console の既存の試験と、手元の CLI の試験 (asterism gem の試験) が通る。CLI の振る舞いを変えたら gem の版を上げる。

## 未確定事項

1. **コマンドを動かす場所**: (a) console の Rails のプロセスの中で CLI を動かす (速い。流れ続けるコマンドのスレッドを console が抱える) / (b) コマンドごとに別のプロセスで動かす (分けて安全。起動が遅く、console のセッションを使えないので接続し直しが要る)。
2. **ブラウザの端末の部品**: xterm.js を同梱するか (console の配り方、importmap か vendor か)、もっと簡素な入力欄と出力欄にするか。
3. **記録の残し方**: console の既存の記録 (V4 の記録と再生) に乗せるか、操作の記録を別に持つか。どれだけの期間残すか。
4. **役割と許すコマンドの対応**: 今の console の役割 (管理者・一般・部屋の参加者など) のどれに、どのコマンドを許すか。
5. **手元の CLI から console を通る口**: 手元の `asterism` コマンドが console にログインして、console の権限と記録の下で打つ形 (`asterism --console URL ...`) も作るか。

## ほかの計画とのつながり

- doc/ruby_asterism/plan.md の V (console) の系列: この計画は V の続きの 1 つ。console の `docs/` に段ごとの報告を置く。
- doc/asterism_file_transfer/plan.md: VT4 のファイルの送受のコマンドの裏の仕組み。
- doc/checkpoint/: VT4 の `move` (アプリを別の端末へ移す)。
- private-notes の構想メモ (FmrbKernel と Asterism・checkpoint の統合): VT4 はその入口。
