# 作業指示書 R2: ファイル書き込みのバッファ

対象: 実装担当のサブエージェント。前提: plan.md、report/r1.md (7 章)、
reference/internal_ram_budget.md (落とし穴)、doc/reference/psram_dma 関係の記録
(memory の「PSRAM スタックは S3 で撤退済」の経緯。reference/internal_ram_budget.md の軸 B)。
report は `report/r2.md` へ。

## 決定 (ユーザ、2026-09-28)

- **link_local のメッセージバッファは移さない** (描画の命令が毎回通る経路で、
  速さへの影響が心配なため)。
- R2 は**ファイル書き込みのバッファ**に絞る: `s_fs_buf` (devctl、4KB) と
  `s_file_write_bounce` (fmrb_hal_file_esp32.c、4KB)。
- prebuilt_gems はやらない。pm_binding_powers は後回し。

## 確かめること (移す前に)

1. `s_file_write_bounce` が**なぜ内蔵 RAM にあるのか**を確かめる。コメントの
   「PSRAM の元から flash に書くと黙って失敗する」が、どの経路 (LittleFS の
   内蔵 flash、SD カードの sdmmc、FAT) の、どの条件 (flash の書き込み中に
   キャッシュが止まって PSRAM が読めない、DMA が PSRAM を読めない、など) の
   話かを、コードと IDF の中身と過去の記録 (git log / report) で特定する。
2. 書き込みの経路ごとに、書き込み元のバッファが PSRAM でよいかを判定する。
   - LittleFS (内蔵 flash): IDF の esp_littlefs は書き込み前に自分のキャッシュへ
     写すか。flash の書き込み中に PSRAM を読むことがあるか (S3 と P4 で、
     PSRAM と flash が同じキャッシュ / バスを共有するかも含めて)。
   - SD カード (sdmmc / SDSPI): DMA が PSRAM を読めるか。IDF が跳ね返し用の
     バッファを自分で持つか。
3. 判定の根拠 (コードの場所、IDF のどの関数) を report に書く。**推論だけで
   決めない**。実機での試験で裏付ける。

## 実機での試験

- P4-Nano (NARYAv4。/dev に無ければ rake attach を自分で)。flash / serial / tab5_* は許可済み。
- 移した版で、LittleFS (/mnt や /home) と SD カード (刺さっていれば) に、
  いろいろな大きさ (4KB 未満・ちょうど・大きい、数 MB) のファイルを書いて、
  読み戻して一致を確かめる。遠隔のファイル転送 (tab5_fs の put、devctl の /fs/put) と、
  エディタの保存の両方の経路で。何度も繰り返す (黙って失敗する種類の不具合なので)。
- 書き込み中に表示や他のタスクが動いている状態 (アプリを動かしながら) でも試す。
- S3 (Retro) がつながっていれば同じ試験をする。つながっていなければ、S3 は
  flash と PSRAM の関係が P4 と違う可能性があるので、**S3 では移さない (P4 だけ移す)**
  選択肢も検討し、report に書く。
- 移さないと判断したら、その根拠を書いて、コードは変えずに終わってよい。

## 作業の決まり

- 作業ブランチ `feature/iram-reduction-r2` (親が develop から切る)。
  自分が変えたファイルだけをパスで指定してコミット。push とマージはしない。
- .env は書き換えずコミットしない。最後に `git diff .env` が scratchpad の
  env_before_iram_r2.diff と一致することを確かめる。
- sdkconfig と graphics-audio、submodule・外部の部品は変えない。
- report は日本語の常体。コードのコメントと commit メッセージは英語。
- 結果を reference/internal_ram_budget.md の計測記録に追記する。

## 受け入れ条件

- 2 つのバッファについて、移せるか・移せないかの判定と根拠がある
- 移した場合: 3 機種の静的な D/IRAM が減り、P4 実機でファイルの書き込みと
  読み戻しが一致する (繰り返しの試験つき)
