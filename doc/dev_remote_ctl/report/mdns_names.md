# 報告: ボードごとの mDNS 名

> 状態: 完了 | 更新: 2026-09-03 | 各ボードが `fmruby-XXXXXX.local` を名乗り、
> `fmruby.local` にも応答する。ツールは固有名を先に試す

Retro で WiFi が使えるようになった結果、**Tab5 と NARYAv3 が両方
`fmruby.local` を名乗る**ようになった。2 台を同時に繋いだ日に、MCP の
`tab5_*` が Retro を引いて「404 for /status」で止まり、IP 直打ちで回避した。
どちらが応答するかは早い者勝ちで、**同じ操作が日によって別のボードに届く**。

## 名前

`fmruby-<WiFi MAC 下位 3 バイト>`。BLE が `Family-mruby-XXXXXX` を作るのと
**同じ規則・同じ綴り** (小文字 16 進、区切り無し)。

実測 (Tab5):

```
ble_task: BLE device name: Family-mruby-90bcea
wifi:     mDNS hostname: fmruby-90bce8.local
```

**数字は一致しない**。WiFi と Bluetooth には同じ base から別のアドレスが
振られるため。読み方が同じで、どちらも起動ログに出る、というところまでが
揃っている。

MAC は **`esp_wifi_get_mac(WIFI_IF_STA)`** から取る。BT MAC ではない
(あれは BLE が上がったときしか `fmrb_sysinfo` に入らないので、WiFi だけの
構成では空。そもそもこれは WiFi の名前である)。

## `fmruby.local` も残す

`mdns_delegate_hostname_add` で**共有名にも応答する**。既存のツールと
これまでの手順書がこの名前を使っており、**1 台しか繋いでいない場合は
こちらの方が便利**だからである。2 台いるときに早い者勝ちなのは従来どおりで、
狙って選びたいときに固有名がある、という形。

**貼り直しが要る。** 委譲した名前はインタフェースを追わずアドレスの一覧を
持つので、`IP_EVENT_STA_GOT_IP` のたびに `mdns_delegate_hostname_set_address`
で入れ直す。入れないと、リースが別のアドレスに変わったあと共有名が古い方を
指し続ける。

## 設定で上書きできる

`wifi.toml` の `hostname` に書けばそれが勝つ。**既定値を空にした**ので、
書かなければ自動生成される。同梱の `wifi.toml.example` はキーを
コメントアウトしてある。

## ツール側

`tools/mcp/lib/tab5.rb` は**固有名を先に、共有名を後に**試す。固有名は
**シリアルのログから読む** (`wifi: mDNS hostname: X.local`)。キャプチャは
たいてい動いており、そこに出ているのは**今刺さっているボード** = 作業対象
だからである。`FMRB_MCP_TAB5_HOST` を置けばその名前だけを使う。

罠が 2 つあった。

- **シリアルのログはテキストではない。** 回線が出したバイトがそのまま入って
  いるので、UTF-8 として読むと最初のノイズで `match` が例外を投げる。
  `rescue` が握り潰して「固有名が見つからない」に化けていた。バイナリで読む。
- **末尾だけ読む。** capture.log は 56 万行あり、欲しいのは最後の起動だけ。
  256KB の末尾を見る。

## 確かめたこと

実機 Tab5 (フル書き込み。`app_only` では `/etc/wifi.toml` が古いままで、
設定の既定値が変わったことが端末に届かない)。

```
$ Resolve-DnsName fmruby-90bce8.local  -> 192.168.10.21
$ Resolve-DnsName fmruby.local         -> 192.168.10.21
```

**両方が同じボードを指す。** 起動ログも 2 つの名前を出す:
`Connected, ip=192.168.10.21 (http://fmruby-90bce8.local/, also http://fmruby.local/)`。

Retro 側は未確認 (今 Tab5 が繋がっているため)。ネイティブ WiFi の S3 でも
`esp_wifi_get_mac` は同じように答えるはずだが、実測はまだ。
