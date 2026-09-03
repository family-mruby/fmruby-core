# 報告: Retro でも開発用リモート制御を使えるようにした

> 状態: 完了 | 更新: 2026-09-03 | エンドポイントを drivers/devctl へ切り出し、
> Retro は WiFi が上がったときだけ自前の httpd を立てる

ユーザ要望 (2026-09-03):「`FMRB_DEV_REMOTE_CTL` の部分は開発のために Retro でも
使えるとうれしい。WiFi 利用時のみ RAM の負荷がかかる形なら許容できる」。

Retro (NARYA v3 / S3) で WiFi が繋がるようになった直後の話。**ping は通るのに
`/app/list` が即座に拒否される**ところから始まった。

## なぜ動いていなかったか

`FMRB_DEV_REMOTE_CTL` は機種を問わず既定 ON で、そこだけ見ると Retro でも
有効に見える。実際には**それを使っているのは `rd_http.c` ただ 1 つ**で、その
ファイルは `drivers/remote_desktop/` にあり、ディレクトリごと
`main/CMakeLists.txt` の esp32p4 のブロックの中にある。Retro には **HTTP
サーバ自体が入っていなかった**。

**マクロが定義されていることと、それを使うコードがビルドされていることは別**。
最初これを混同して「Retro でもファイル転送はできる」と誤って答えた。

## 切り分け

開発用エンドポイント (`rd_http.c` の 381 行) は**画面配信と独立**していた。
使っていたのは `fmrb_app` (spawn/kill/ps) と `fmrb_hal_file` だけで、
エンコーダにも display_p4 にも触っていない。同居していたのはファイルの都合。

- `main/drivers/devctl/devctl_http.c` — ハンドラ 8 本と登録。`httpd_handle_t`
  を受け取るだけで、サーバは誰が立てても構わない。
- `main/drivers/devctl/devctl_task.c` — Retro 用。小さな httpd を立てて上記を
  登録する。**Modern はこれを使わない** (ビューアのサーバに相乗り)。
- `rd_http.c` は 963 → 566 行。`devctl_http_register(s_server)` を 1 行呼ぶ。

## RAM は WiFi を使うときだけ

`devctl_start()` は **boot.c の `wifi_auto_start` の枝の中**から呼ぶ。
`wifi_auto_start = false` の機械ではタスクも作られない。

- httpd タスクのスタック **8192 B** (`FMRB_RD_HTTPD_TASK_STACK_SIZE` を共用)
- ソケットは **4** (ビューアの 7 に対して)。ここは何もストリームせず、使う道具
  (`tools/fmrb_rd_fs.rb`, curl) は 1 度に 1 リクエストしか出さない。
- `max_uri_handlers` は 12。**登録は `ESP_ERR_HTTPD_HANDLERS_FULL` を返すだけで
  起動は成功する**ので、ぴったりに切ると「サーバは健全、最後に足した経路だけ
  404」になる (Modern で一度踏んでいる)。
- フラッシュは +40KB (0x3e6160 → 0x3f0300、6M 区画に 34% 空き)

## 踏んだ罠

**`option()` の宣言位置**。`FMRB_DEV_REMOTE_CTL` の `option()` は
`idf_component_register` より**下**にあった。機種別の枝はその**上**で走るので、
そこで `if(FMRB_DEV_REMOTE_CTL)` を書くと**まっさらな configure では偽**になる。
一方 CMake はオプションをキャッシュするので、**再 configure では真**になる。
結果、「ソースはビルドされるのに、同じ枝で足したはずの `esp_http_server` が
REQUIRES に入っていない」という状態になり、`esp_http_server.h: No such file`
で止まった。宣言をファイル先頭へ移した。

`rd_http.c` から切り出したコードは `<ctype.h>` `<dirent.h>` `<sys/stat.h>`
`<unistd.h>` を使っていた。元ファイルの include に紛れていて、移した先で
`DIR` が未定義になるまで気づかない。**切り出しでは実体だけでなく include も
追う**。

## 確かめたこと

実機 NARYAv3 (WiFi 接続済み)。app のみの書き込みなので /home はそのまま。

```
W devctl: development remote control is enabled (/app/launch, /app/kill, /app/list, /fs/*)

$ curl http://<IP>/app/list
{"apps":[{"pid":0,"name":"fmrb_kernel","state":"RUNNING"},
         {"pid":2,"name":"system_desktop","state":"RUNNING"}]}

$ curl -X PUT --data-binary @hello.app.rb "http://<IP>/fs/put?path=/home/hello.app.rb"
{"ok":true,"size":255}

$ curl -X POST "http://<IP>/app/launch?path=/home/hello.app.rb"
{"ok":true,"pid":4}

I fmrb_app: [hello] Ruby script compiled successfully
I app_canvas: [hello] Created canvas 3 (100x100)
```

**put → launch の開発ループが Retro でも回る**。ログのタグが `rd_http` では
なく `devctl` になっているので、どちらの経路で動いているかは一目で分かる。

`fmrb_mem_log_boot_snapshot("devctl")` を入れたので、**次のビルドから
`M1|devctl|` の行が出る**。WiFi の段と分けてあるのは、ここだけが「要らないなら
持たなくていい」subsystem だから。今回焼いた firmware にはこの行はまだ無い。

## Modern の回帰確認 (2026-09-03、実機 Tab5)

フル書き込みして 4 系統を通した。**切り出しによる欠落は無い**。

| 見たもの | 結果 |
|---|---|
| ビューアのページ / | HTTP 200 (2423 B) |
| /status, /stream | `mjpeg`、MJPEG 1 枚取得・入力注入 OK |
| /app/launch, list, kill | put した app が pid 5 で起動、kill で消える。`pid=0` は拒否 |
| /fs/put, get, del, list | 往復の md5 一致、`..` は拒否、del は **DELETE** |
| `M1\|wifi_rd\|internal` | **187308** (切り出し前 187320)。実質同じ = 二重に持っていない |

**ログが 2 回出ていた**。切り出したとき、経路を宣言する行を登録側
(`devctl_http_register`) と呼ぶ側 (`rd_http.c` と `devctl_task.c`) の両方に
書いてしまい、Modern でも Retro でも同じ警告が並んで出ていた。実際に登録が
成功したことを知っているのは登録側だけなので、呼ぶ側の 2 つを消した。
**切り出しでは、実体と include だけでなくログの持ち主も動く**。

Retro (`devctl`) と Modern (`rd_http`) でタグが違うので、どちらの経路で
上がったかは 1 行目で分かる。この性質は残してある。
