# プロジェクト名の決定: Asterism

決定: 2026-08-27。構想名 (旧仮称 Ruby Unified) を **Asterism** とする。

## 由来

スタールビーの星彩 (asterism)。ルビー内部に絹糸状の含有物 (silk) が
整列していると、光を当てたとき石の中に六条の星が浮かび上がる。

- 石 (Ruby) の中に走る無数の細い糸が、繋がって一つの星の像を結ぶ
  = 各地に散る Ruby ノードが Zenoh の糸で繋がり、一つの分散資源空間として
  像を結ぶ。
- 天文用語では「星群 (星座のような星の並び)」。独立した星々が一つの形を
  成す、という分散システムの比喩が重なる。
- 宝石用語なので gem 文化と地続き。

## 名前の使い分け

- 構想・上位 gem: `asterism` (rubygems は 2026-08-27 時点で空き。
  **早めに取得すること**)。
- 下位の通信層 (Zenoh 束ね) の名前として **Silk** を予約する
  (silk が石の中にあるから星が出る、の層構成そのまま。gem 名は
  `silk` 単体が取得済みのため `asterism-silk` のような従属名にする)。
- 発表タイトル案: "Asterism: A Star Network Inside Ruby —
  from the Web to Microcontrollers"。
- キャッチコピー候補: "Ruby is everywhere" の導入と接続する。
  コランダム (ルビーもサファイアも同じ鉱物) は「処理系が違っても下は
  同じ物質」のスライド 1 枚として使える。

## 検討した他候補 (経緯)

- Ruby Unified (旧仮称): 意味が一発で伝わるが、「Ruby 処理系の統一」と
  誤読される弱点。gem 名 ruby_unified は空きだった。
- Ruby Constellation: 衛星コンステレーションの含みは良いが詩情薄め。
  gem は 2012 年から放置の死蔵。
- Ruby Everywhere: 誤読されないが固有性が弱い。キャッチコピー側に回す。
- Corundum: 比喩は正確だが読みが通じにくい。gem 取得済み (2019 放置)。
- Ruby Lattice: wasmCloud の "lattice" と既視感。gem 取得済み。
- Geode: Apache Geode と衝突。
- それ以前の候補 (RubySpace / Continuum / Nexus / Cosmos / Universe /
  Grand Unified Ruby) は ruby_unified_discussion_summary.txt の 16 節。
