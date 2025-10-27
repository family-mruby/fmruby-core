# Family mruby OS 開発メモ

## ユーザビリティ

- アプリを書いて、１コマンドで、実機へファイル転送
  - Linuxターゲットの場合は、Filesystemを直で触ってもいい。
- GUIでアプリを更新して、再起動してもいい
- アプリからは自由にrequireできる
- アプリからアプリを起動できる（例 kernel -> app、 app -> 個プロセスみたいな ）

## 実現したい優先度

Linux上で

- DONE: 大まかなリポジトリ構造の設計
- DONE: SDL2 Hostアプリとシリアル通信して、LovyanGFXでの描画を実装
- DONE: MicorRuby のマルチタスク起動
- DONE: ファイルシステムのFmrbHAL化
- DONE: シリアル通信でファイル転送
- OnGoing: Filesystem関連の picoruby mrbgemのFmrbHAL対応
- SystemGUIで、画面全体を描画　ツールバー描画
  - FmrbAppとFmrbGfxクラスのmrbgem実装。まずはGUI関連から。
  - App -> Host へのIPCの実現
  - 各種描画APIの動作確認
- User Appで、require の動作検証デバッグ
  - pre-buildとdynamic buildそれぞれ
- User Appで、全画面描画
- User Appで、Window描画
- User Appで、キー,マウス操作イベント受信
  - SDL2 & USB HOST
- User Appで、Shell実装（キーボードと描画の連携）
  - App -> App 起動機能のみ実装
  - 他はFilesystem関連とプロセス関連
    - cd, ls, rm, mv, ps, mem, top???
- User Appで、アプリのライフサイクル（開始、クローズ、開始）
  - Kernel <-> App 通信 
- 単体アプリの開始起動やAPIなどテストする
- 多重User Appの動作確認
- User Appで、Window位置制御。Z順番制御
- User Appで、マウス制御でWindowのサイズダイナミック変更
- 複数アプリの開始起動やAPIなどテストする
- SystemGUI ツールバー機能の実装
  - アプリランチャー
  - 時計（デモ用に早くてもいいかも）
  - 起動中のアプリ表示（デモ用に早くてもいいかも）
- ファイルUIアプリの実装
- App <> App 通信

開発基版の製造が終わり次第、ESP32実機でも確認進める。
音周りはGUIが落ち着いたら考える。

## todo

- アプリからrequire必要なのはどの範囲か確認する
  mrbgem 不要？
- RubyKaigi投稿時は、READMEでは、開発予定アイテムを進捗を可視化する。
