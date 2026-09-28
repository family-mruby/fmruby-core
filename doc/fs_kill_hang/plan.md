# ファイル操作中のアプリの kill でファイル操作が止まる件

> 状態: 完了 | 更新: 2026-09-29 | 原因は強制 kill がファイル操作の途中のアプリを消し、file HAL と LittleFS の錠が失われたこと (sim の gdb と P4-Nano で特定)。強制経路でファイル・registry・MicroPython の錠を持ったまま消し、終わったアプリの開きっぱなしのファイルを閉じる形で K1 完了 (report/k1.md)。Lua の io と Spinel の File の穴は「残り」

## 目的

アプリをいつ kill しても (ファイル操作の途中でも)、ほかのタスクのファイル操作が
止まらないようにする。

## 分かっていること (doc/iram_reduction/report/r2.md 「既存の問題」)

- NARYAv4 で、2MB を File#write で書いている途中のアプリを `/app/kill` で止めたら、
  その後の遠隔の要求がすべて応答しなくなった。ping は通り、リセットで戻った。
- ESP32 の file HAL (components/fmrb_hal/platform/esp32/fmrb_hal_file_esp32.c) は
  `s_file_mutex` で全操作を直列化し、`xSemaphoreTake(..., portMAX_DELAY)` で無期限に待つ。
- アプリの kill には強制終了の経路がある (fmrb_task_delete + force_release_resources、
  doc/archive/app_kill_fix)。錠を持ったままタスクを消すと、錠は二度と返らない。
- LittleFS 自身の錠 (esp_littlefs の lock) も、同じく持ったまま消えうる。

## 方針

- 推論で直さない。再現して、どの錠で止まっているかを特定してから直す
  (sim なら gdb で全スレッドの backtrace が取れる。ルートの CLAUDE.md)。
- 直し方の候補 (K1 で比べて決める):
  - kill の強制経路で、ファイル操作の最中のタスクを消さない (ファイル操作を
    終わらせてから、または区切りまで待ってから消す)。
  - ファイル操作を、消されない専用のタスク (既存の hw_proxy のような代理) に任せる。
  - 錠を持っているタスクを覚えておき、消すときに後始末する (ただし FreeRTOS の
    mutex は持ち主以外が返せず、中途の LittleFS の状態も壊れうるので、筋が悪い見込み)。
- **flash への書き込み試験は最小限** (ユーザの指示): 実機での再現は必要な回数だけ、
  1 回あたり数 MB 以内。繰り返しは sim か /tmp で。

## 段階

| 段階 | 内容 | 状態 |
|---|---|---|
| K1 | 再現 (sim と実機)、止まっている錠の特定、修正の案の比較と実装、確認 | 完了 (report/k1.md) |

## 受け入れ条件

- ファイル操作の途中のアプリを kill しても、その後のファイル操作 (別のアプリ、
  遠隔のファイル転送、エディタの保存) が止まらない (sim と P4 実機)
- 書きかけのファイルの扱い (途中まで残る / 消える) が決まっていて、ファイル
  システムが壊れない
- 内蔵 RAM を増やさない

## 残り

K1 では扱わず、別段階にする (report/k1.md 8 章)。

- Lua の `io` (submodule の liolib.c) は HAL を通らず VFS を直接呼ぶ。1 回の `f:write` が kill の猶予 (1 秒) を超えると、
  同じ停止が起こりうる。塞ぐなら `fmrb_lua` で `io` を HAL 経由の実装に差し替える。
- Spinel の実行時ライブラリの File (sp_io.c) も VFS を直接呼ぶ。
- PSRAM スタックのタスクを戻すときは、hw_proxy の途中で消される穴に対策が要る。
