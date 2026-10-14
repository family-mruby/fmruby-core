# tts の API キーを secrets.toml に分けた

V2b で入れた OpenAI の API キーは `services.toml` の `[tts.config] api_key` に
あった。`services.toml` はふつうのファイルなので、BLE の GET ではいつでも、
devctl の `/fs/get` (LAN で無認証) でも読めた。CF2 で作った秘密の判定
(`main/kernel/fmrb_secret_path.c`、`doc/ruby_asterism/report/cf2.md`) は WiFi の
ファイルと `/home/asterism/` の tar だけを見ていた。キーを別のファイルに分け、
そのファイルを WiFi のパスワードと同じ強さで扱うようにした。

## 1. 作ったもの

### 1.1 キーだけのファイル

```
[openai]
api_key = "sk-..."
```

- 端末でユーザが書くのは `/home/secrets.toml`。開発ビルドが入れるのは
  `/etc/secrets.toml` (rake が `~/.openai_key` から作る)。
- `tts.rb` は `/home/secrets.toml` → `/etc/secrets.toml` の順に `[openai]` の
  `api_key` を探す。見つかればログに `tts: api_key from /home/secrets.toml` の
  ように**どこから読んだかだけ**を 1 行出す。値は出さない。
- `services.toml` の `[tts.config] api_key` は読まない。最初は移行のために
  読んで移すよう 1 行出していたが、今使っている人はいないので消した
  (2026-10-15 のユーザの決定。5 章)。
- 読み方は `TtsService#secrets_value` (表と文字列の値だけ。注釈と空行を飛ばす。
  逆エスケープはしない。OpenAI のキーに引用符やバックスラッシュは無い)。

### 1.2 rake

- `rakelib/build.rake` の `inject_tts_api_key` (services.toml に追記していた) を
  やめ、`write_dev_secrets` で `flash/etc/secrets.toml` を作る。Modern だけ。
- 作る前に `git ls-files --error-unmatch` で追跡されていないこと、
  `git check-ignore` で無視されることを確かめ、どちらかが外れたら abort する。
  `.gitignore` には `flash/etc/secrets.toml`・`flash/home/secrets.toml` (と
  `.part` などの派生名) を先に足した。`flash/home/services.toml` は無視されて
  いないので、sim で /home に書いたキーをうっかりコミットしないよう
  `flash/home/secrets.toml` も入れてある。
- 毎回まず消してから作る (`~/.openai_key` を消したら生成物からも消える)。
  CI では作らない (リリースのファームを作る場所なので)。権限は 0600。
- `services.toml` は毎回 `config/` から写す。写したあとに `api_key` の行が
  あれば abort する (`config/services.toml` にキーが書かれる事故の止め)。
  tts.rb はもう読まないので、この行は漏れるだけで役に立たない。
- Retro の storage image は service の一式と一緒に `etc/secrets.toml` も外す
  (トップの `CMakeLists.txt`)。

### 1.3 秘密の判定

- `/home/secrets.toml`・`/etc/secrets.toml` と、`secrets.toml` で始まる名前
  (`.part` など)、`/flash/...` の別名を WiFi のファイルと同じ区分にした。
  区分の名前は `FMRB_PATH_WIFI` から `FMRB_PATH_PASSWORD` に改めた。
- devctl の `/fs/get` はいつも断る (403)。BLE の GET は受け付ける時間の間だけ。
  PUT・RM は受け付ける時間の間だけ。RENAME は WiFi と同じ順位 (secrets.toml から
  ふつうの名前へは断る、WiFi どうし・secrets どうし・tar 側へは時間内なら通る、
  tar から secrets.toml へは断る)。
- `/home/sub/secrets.toml`・`/home/my_secrets.toml`・`/app/secrets.toml` は
  ふつうのファイルのまま (WiFi と同じ線引き)。

### 1.4 BLE のコンソール (tool/web、版 2026-10-14c)

Settings に「OpenAI API key」の欄を足した。WiFi のパスワードと同じ扱い。

- 閉じている間は欄が空。どのファイルにあるか (`/home/secrets.toml` か、
  ファームの `/etc/secrets.toml`) と set / not set だけを出す。有無は一覧
  (LS) で見ており、中身は読まない。
- 受け付ける時間が開くと端末から読んで欄を埋める。show で見える。閉じると消す。
- 空のまま書けるのは読めたときだけ (`/etc` のキーを `/home` に写すことになる)。
- 書く先は `/home/secrets.toml`。ほかの表や注釈は残し、`[openai]` の `api_key`
  だけを書き換える (無ければ足す)。引用符・バックスラッシュ・空白・ASCII 以外を
  含むキーは書かない。

### 1.5 wasm:scan

`wasm_local_secrets` が `flash/etc/secrets.toml`・`flash/home/secrets.toml` の
値も拾って bundle を検査する。名前が `secrets.toml` で始まるファイルが
bundle に入っていたら、それだけで問題とする。

## 2. 確認

2026-10-14 の確認。sim のログの 1 行目 (移す案内) は、5 章で消した作りのときのもの。

- `rake secret:test`: 148 項目 (37 項目増) が通る。secrets.toml の区分 (別名・
  `.part`・`..`・近い名前)、GET・PUT・RM の時間の扱い、RENAME の順位を見る。
- `rake tts:test` (`test/tts/run.rb`): 偽のキー (`sk-test-0000...`) で
  `secrets_value` の読み方と、`/home` → `/etc` の順、ログに値が出ないことを
  見る (5 章で services.toml を読まないことの確認に替えた)。`rake test` 全体も通る。
- `tool/web/test/settings.mjs`: 48 項目 (15 項目増) が通る。閉じている間の
  表示と、端末の答えにキーが入らないこと、開くと読み込むこと、show、空のまま
  書くと読んだキーを /home に書くこと、ほかの行を残すこと、閉じると消えること。
  `ui.mjs` も通る。
- `rake build:linux` (TAB5 の sim): 生成物の `flash/etc/secrets.toml` に
  `api_key` の行が 1 つ、`flash/etc/services.toml` には 0 (行の有無と件数だけを
  見た)。sim を起こし、`/home/services.toml` に偽のキーを置いて tts を有効にした
  状態でログを見た。

  ```
  svc[tts] tts: api_key in services.toml can be read over the network; move it to /home/secrets.toml under [openai]
  svc[tts] tts: api_key from /etc/secrets.toml
  svc[tts] tts ready (openai/gpt-4o-mini-tts)
  ```

  ログに本物のキーも偽のキーも 0 件。エディタを起動して 1 打鍵まで確かめた。
  確認のあと `/home/services.toml` は消した。
- ESP32-P4 (TAB5、`rake clean_all` のあと `rake build:esp32`) が通る。
  storage の staging に `etc/secrets.toml` (api_key 1 行) が入り、
  `etc/services.toml` には api_key の行が無い。
- wasm (`rake wasm:web` → `rake wasm:scan`): 497 ファイル、資格情報なし。
  scan は生成物の secrets.toml の値でも bundle を探した。

### 2.1 内蔵 RAM (TAB5、develop 8712f0df と同じ設定で比べた)

| | develop | このブランチ | 差 |
|---|---|---|---|
| DIRAM (静的、.text / .bss / .data) | 85,210 / 29,700 / 15,044 | 85,210 / 29,700 / 15,044 | 0 |
| External RAM .bss | 18,148,516 | 18,148,516 | 0 |
| flash .text | 3,702,854 | 3,702,918 | +64 |
| fmruby-core.bin | 6,632,096 B | 6,632,176 B | +80 |

足したのは判定の文字列比べだけで、静的な内蔵 RAM は変わらない。

## 3. ユーザの確認の手順 (端末は焼いていない)

`rake flash` は storage を書き直すので /home が消える。/home に残したいもの
(`/home/secrets.toml` など) があれば先に手元へ写しておく。

Step 1. `~/.openai_key` がある状態で `rake build:esp32` して焼く。ブートの
ログ (シリアル) に `tts: api_key from /etc/secrets.toml` と
`tts ready (openai/...)` が出ること。tts を有効にしてあることが前提
(`/home/services.toml` の `[tts] enable = true` か `svc enable tts`)。

Step 2. 開発ビルドの devctl で `/fs/get?path=/home/secrets.toml` を叩くと
403 (`this file holds keys or passwords ...`) が返ること。`/etc` はもともと
devctl の `/fs` の範囲 (`/mnt/sd`・`/home`・`/app`・`/usr/share`) の外なので、
`/fs/get?path=/etc/secrets.toml` は秘密の判定より前に 400
(`path outside ...`) で断られる (最初はここを 403 と書いていた。6 章で実測)。

Step 3. BLE のコンソール (版 2026-10-14c) で Settings を開く。閉じている間は
「OpenAI API key」の欄が空で、On the board に `set (the firmware's /etc/secrets.toml ...)`
と出ること。端末で「BLE Setup」を開くと欄にキーが入り、show で見えること。
閉じると欄が空に戻ること。

Step 4. (任意) 開いた状態で欄を空のまま「Write API key」を押すと
`/home/secrets.toml` ができ、起動し直すとログが `api_key from /home/secrets.toml`
になること。`tts/say` で声が出ること。

## 4. 気づいたこと

- `flash/home/services.toml` は gitignore に入っていない。sim で /home に
  キーを書く運用は、今回足した `flash/home/secrets.toml` の無視で守る。
- develop に戻した作業コピーでは `flash/etc/secrets.toml` が無視されない
  (develop の `.gitignore` にまだ無い) ので、作業の終わりに消した。develop の
  rake は今どおり services.toml に追記する。

## 5. 移行の分を消した (2026-10-15)

ユーザの決定: 今 `services.toml` にキーを書いて使っている人はいないので、移行は
考えない。

- `tts.rb` の `read_api_key` は `/home/secrets.toml` → `/etc/secrets.toml` だけを
  見る。`[tts.config] api_key` を読む分と、移すよう促すログ
  (`api_key in services.toml can be read over the network; ...` と
  `api_key from services.toml`) を消した。引数 (`cfg`) も要らなくなった。
  先頭の注釈も合わせた。
- 端末で書いている人向けの移し方の節 (旧 2 章) は消した。
- rake の「写した `flash/etc/services.toml` に `api_key` の行があれば abort」は
  **残した**。移行のためではなく漏れの止めだから: tts.rb が読まなくなった今、
  その行は何の役にも立たず、遠隔のファイルの口で誰にでも渡るファイルに鍵が
  載るだけになる。注釈をその理由に書き直した。
- `test/tts/run.rb`: services.toml の順と移す案内の項目を消し、`/etc` から
  読んだときのログ、`/home` の空のキーで `/etc` へ落ちること、tts.rb が
  `cfg["api_key"]` を参照しないことを足した。
- 確認: `rake tts:test`・`rake secret:test` (159 項目)・`rake test`・
  `tool/web/test/settings.mjs`・`ui.mjs` が通る。

## 6. P4-Nano の実機で確かめた (2026-10-15)

P4-Nano (NARYAv4。`FMRB_HW_TARGET=NARYAv4`、`rake clean_all` のあと
`rake build:esp32`) に**アプリの区画だけ**を焼いた。/home (Asterism の
`config.toml` と証明書の tar) はそのまま残った (焼いたあとの一覧で 109 B と
8192 B)。

- app だけの焼き直しでは storage の `tts.rb` が古いまま (secrets.toml を知らない
  版) なので、このブランチの `flash/usr/share/services/tts.rb` を devctl の
  `/fs/put` で `/usr/share/services/tts.rb` に置いた。`/etc` は焼き直さない
  ので `/etc/secrets.toml` は端末に無い。
- キーは devctl の `/fs/put` で `/home/secrets.toml` に書いた (`[openai]` の下に
  `api_key`、186 B)。手元の写しは 0600 で作り、送った直後に消した。
- tts は `svc/ctl` の `enable` (shell の `svc enable tts` と同じ要求) を小さな
  アプリから送って有効にした。シリアルのログ:

  ```
  svc[tts] tts: api_key from /home/secrets.toml
  svc[tts] tts ready (openai/gpt-4o-mini-tts)
  services: tts started (sys, /usr/share/services/tts)
  services: tts enabled
  ```

- 07:54:32 に `tts/say` で「こんにちは。秘密のファイルから鍵を読みました。」を
  送った。TLS の証明書の検証が通り、`tts: cloud synthesised in 3299 ms`、
  07:54:35.85 に `audio_p4: play_wav: /tmp/tts_....wav (94800 frames @ 24000 Hz)`
  (約 4 秒)。音量は 6、消音なし。声が聞こえたかはユーザが確かめる。
- devctl: `/fs/get?path=/home/secrets.toml` と `/home/./secrets.toml` は 403
  (`this file holds keys or passwords and is not read remotely`)。
  `/fs/get?path=/etc/secrets.toml` は 400 (`path outside /mnt/sd, /home, /app,
  /usr/share`)。`/etc` は devctl の `/fs` の範囲の外で、秘密の判定より前に断られる。
  どちらにしても読めない。一覧 (`/fs/list`) には名前と大きさだけが出る。
- シリアルのログ (この作業の分と、それまでの capture の全部) に、キーの値も
  先頭 20 文字も 0 件。crash の印 (`Guru`・`abort(`) も 0 件。
- 後片付け: `svc/ctl` の `disable` で tts を止め (`services: tts disabled`)、
  ホストが書いた `/home/services_state.toml` を消した (有効にする前は無かった)。
  確認用のアプリ 2 本も消した。`/home/secrets.toml` は残した。
- Asterism: othello を起動して `othello: fmruby-aaaaaa up (tls/<VPS>:<port>, 819 ms)`
  (TLSv1.3 の握手 333 ms)。今までどおり繋がる。確かめたあと閉じた。
