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
- 移行のため `services.toml` の `[tts.config] api_key` も読む。ただし
  secrets.toml にキーがあればそちらが勝つ。`services.toml` にキーがあると、
  使うかどうかに関係なく
  `tts: api_key in services.toml can be read over the network; move it to /home/secrets.toml under [openai]`
  を 1 行出す。
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
- `services.toml` は毎回 `config/` から写すので、古い生成物に追記された
  `api_key` の行は写した時点で消える。加えて、写したあとに `api_key` の行が
  あれば abort する (`config/services.toml` にキーが書かれる事故の止め)。
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

## 2. 端末で /home/services.toml にキーを書いている人の移し方

Step 1. `/home/services.toml` の `[tts.config]` から `api_key = "..."` の行を
切り取る (`[tts.config]` のほかの行は残す)。

Step 2. `/home/secrets.toml` を作り、次の形で書く。

```
[openai]
api_key = "sk-..."
```

エディタで書いてもよいし、BLE のコンソールの Settings で端末の「BLE Setup」を
開いて「OpenAI API key」の欄から書いてもよい。

Step 3. 端末を起動し直す (tts サービスは起動時にキーを読む)。ログに
`tts: api_key from /home/secrets.toml` が出て、`move it to /home/secrets.toml`
の行が出なければ移し終わっている。

Step 1 を忘れても動くが、ログに移すよう 1 行出続け、キーは
`services.toml` 経由で読めるままになる。

## 3. 確認

- `rake secret:test`: 148 項目 (37 項目増) が通る。secrets.toml の区分 (別名・
  `.part`・`..`・近い名前)、GET・PUT・RM の時間の扱い、RENAME の順位を見る。
- `rake tts:test` (`test/tts/run.rb`): 偽のキー (`sk-test-0000...`) で
  `secrets_value` の読み方と、`/home` → `/etc` → services.toml の順、ログに
  値が出ないこと、移す案内が出ることを見る。`rake test` 全体も通る。
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

### 3.1 内蔵 RAM (TAB5、develop 8712f0df と同じ設定で比べた)

| | develop | このブランチ | 差 |
|---|---|---|---|
| DIRAM (静的、.text / .bss / .data) | 85,210 / 29,700 / 15,044 | 85,210 / 29,700 / 15,044 | 0 |
| External RAM .bss | 18,148,516 | 18,148,516 | 0 |
| flash .text | 3,702,854 | 3,702,918 | +64 |
| fmruby-core.bin | 6,632,096 B | 6,632,176 B | +80 |

足したのは判定の文字列比べだけで、静的な内蔵 RAM は変わらない。

## 4. ユーザの確認の手順 (端末は焼いていない)

`rake flash` は storage を書き直すので /home が消える。/home に残したいもの
(`/home/services.toml` のキーなど) があれば先に手元へ写しておく。

Step 1. `~/.openai_key` がある状態で `rake build:esp32` して焼く。ブートの
ログ (シリアル) に `tts: api_key from /etc/secrets.toml` と
`tts ready (openai/...)` が出ること。tts を有効にしてあることが前提
(`/home/services.toml` の `[tts] enable = true` か `svc enable tts`)。

Step 2. 開発ビルドの devctl で `/fs/get?path=/etc/secrets.toml` を叩くと
403 (`this file holds keys or passwords ...`) が返ること。
`/fs/get?path=/etc/services.toml` は読めて、`api_key` の行が無いこと。

Step 3. BLE のコンソール (版 2026-10-14c) で Settings を開く。閉じている間は
「OpenAI API key」の欄が空で、On the board に `set (the firmware's /etc/secrets.toml ...)`
と出ること。端末で「BLE Setup」を開くと欄にキーが入り、show で見えること。
閉じると欄が空に戻ること。

Step 4. (任意) 開いた状態で欄を空のまま「Write API key」を押すと
`/home/secrets.toml` ができ、起動し直すとログが `api_key from /home/secrets.toml`
になること。`tts/say` で声が出ること。

## 5. 気づいたこと

- `flash/home/services.toml` は gitignore に入っていない。sim で /home に
  キーを書く運用は、今回足した `flash/home/secrets.toml` の無視で守る。
- develop に戻した作業コピーでは `flash/etc/secrets.toml` が無視されない
  (develop の `.gitignore` にまだ無い) ので、作業の終わりに消した。develop の
  rake は今どおり services.toml に追記する。
