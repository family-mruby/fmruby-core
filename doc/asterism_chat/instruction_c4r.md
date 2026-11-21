# C4r 指示書: 役割 chat-host・chat の行を、手元の console の通し稽古 (mTLS のルータ) で確かめる

> 2026-11-21 | 親セッションからサブ (Opus) への指示。先に読むもの: doc/asterism_chat/plan.md (決定 22)、
> report/c3h_c4a.md (役割の行の案)、`../asterism-chat` の `docs/install.md` (役割の節)・`deploy/vps/`・`test/sender.rb`、
> **doc/ai_tutor_vps/report/v1.md と v1c.md (AI ホストで同じことをしたときのやり方: console の rehearsal の起こし方、
> `bin/rails runner` で役割と台帳の行を作る、証明書の出し方、後片付け)**、`../asterism-console` の `deploy/local/` と
> rehearsal の道具 (読む。下の範囲で一時的に使う)。

## やること

1. `../asterism-console` の通し稽古 (deploy/local の rehearsal。v1.md と同じやり方) を起こす。console の**コードは変えない**。
   試験用の `.env`・証明書・volume は一時的に作り、**終わったら全部消して、console の `git status` を元どおりにする**。
2. console に役割 **chat-host**・**chat** を、`../asterism-chat/docs/install.md` の行のとおりに作り、台帳にホスト (`chat`) と
   端末の代わり (fmruby-aaaaaa・fmruby-bbbbbb。役割 chat)、役割の無い端末 (fmruby-cccccc) を置いて、証明書を出す。
3. チャットホストを `deploy/vps/` の compose (手元で作ったイメージ) で、通し稽古のルータの network につないで起こす。
4. 端末の代わりは CRuby の送り手 (`test/sender.rb`。`--credentials` の形が無ければ、asterism-tutor の sender に倣って足す)。
   確かめること:
   - aaaaaa と bbbbbb が、`hello`・`rooms`・`post`・`history`・`stamp_put`・`stamp_get` を呼べ、`news` を受け取れる。
   - **役割の無い cccccc は呼べない・`news` を受け取れない**。aaaaaa がほかの端末の名前 (`from`) を名乗れない。
   - 行が足りずに通らないものがあれば、**足りない行を特定して最小の形で足し** (`docs/install.md` を直す)、
     逆に、無くても通る行 (要らない権限) があれば外せるかを試して報告する (外すかどうかは親が決める。行は残してよい)。
   - 推測の印 (*) の付いた行 (`asterism/*/chat/news` など) の結果を、確かめた・要らなかった、で書き直す。
5. ブラウザの利用者ごとの役割 (door 経由) は、rehearsal の設定が重ければ試さなくてよい (試さなかったと書く)。

## 受け入れ条件

1. 上の 4 の表 (呼べる・呼べない) が、mTLS のルータで実際に確かめた結果として report にある。
2. `../asterism-chat/docs/install.md` の役割の行が、確かめた結果に合っている (推測の印が消えるか、残る理由が書いてある)。
   `bundle exec rake test` が通る。gem を直したらコミット (push しない)。
3. 後片付け: 通し稽古の stack・volume・network・証明書・`.env` を消してある。`../asterism-console` の `git status` が
   始める前と同じ。`docker ps -a`・volume・network に rehearsal や chat の付くものが残っていない。

## 範囲と止まる条件

- 触ってよい所: `../asterism-chat` (役割の文書、sender、不具合の直し)、`../asterism-console` は**一時的な設定のファイルだけ**
  (コードを変えない・コミットしない)、スクラッチ。report は最後の返事に全部書く (fmruby-core では別のサブが作業中。
  **fmruby-core は読むだけ**: ビルド・sim・web・git の操作をしない)。
- **触らない**: VPS・端末・本物の鍵 (`~/asterism_keys`・`~/.config/fmrb`)、asterism-tutor・asterism-games・asterism 本体 (読むだけ)、
  ほかのサブが使っているコンテナとポート (8006・7447・7449、名前に fmruby の付くコンテナ)。ポートがぶつかるなら rehearsal の
  側のポートを変える。
- 止まる条件: console のコードの直しが要る (役割の作りで表せない権限がある、など)。線の形を変える必要がある。
  ユーザの作業が要る。選ばずに、選択肢にして返す。
