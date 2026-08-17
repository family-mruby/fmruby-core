# 実装レポート

各フェーズの実装中に得た気づき・実測値・次フェーズへの申し送りを
フェーズごとに置く。計画文書 (../phase*.md) は「何をやるか」を書いたもので、
こちらは「やってみて何が分かったか」を書いたもの。

計画文書に書くのは未確定事項の確定結果だけにして、詳細はこちらに寄せる。
仕様として確定した制約は README の「受け入れる制約」に上げる。

| レポート | 内容 |
|---|---|
| [phase0.md](phase0.md) | submodule 追加と embed port 生成の道具立て |
| [phase1.md](phase1.md) | ESP-IDF コンポーネント化と fmrb_mp ラッパ |
| [phase2.md](phase2.md) | fmrb_app 統合 (.py の起動・停止・排他) |
| [phase3.md](phase3.md) | FmrbApp / FmrbGfx バインディングとデモアプリ |
| [phase4.md](phase4.md) | ESP32 ビルド・資源実測・制限事項文書 |
| [phase5.md](phase5.md) | 土台 (時計・ファイル・import・タイマ・pub/sub) と性能の実測 |
| [phase6.md](phase6.md) | 絵 (日本語・画像・スプライト・タイル) とスプライトの費用 |
| [phase7.md](phase7.md) | アプリ間通信 (Ruby の世界 x Python の操縦) と応答時間の比較 |
| [phase8.md](phase8.md) | 音 (曲と効果音)、音符の組み立てを 1 か所に集約 |
| [phase9.md](phase9.md) | 見本のゲーム (ブロック崩し) と 1 フレームの実測 |
