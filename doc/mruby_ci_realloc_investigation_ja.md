# mruby ci Realloc調査経過報告

## 調査日
2025-01-XX (調査継続中)

## 調査の背景

### 症状
- `mrb_funcall()`実行後にクラッシュする問題
- ciポインタが不正な値を指している疑い
- メモリプール境界を超える可能性

### 調査目標
1. ciポインタのrealloc発生タイミングを特定
2. reallocとクラッシュの因果関係を解明
3. ciスタックのリーク原因を特定

## 実装したデバッグ機能

### 1. メモリプールポインタチェック機能 ([main/lib/fmrb_mem/fmrb_mempool.c](../main/lib/fmrb_mem/fmrb_mempool.c))

```c
void fmrb_mempool_check_pointer(const void* ptr);
```

- ポインタがどのメモリプール（SYSTEM, KERNEL, SYSTEM_APP, USER_APP0-2, PRISM）に属するか判定
- メモリプール境界外のアドレスを検出

### 2. ciポインタ妥当性チェック機能 ([lib/add/picoruby-fmrb-app/ports/esp32/app.c](../lib/add/picoruby-fmrb-app/ports/esp32/app.c))

```c
static bool check_mrb_ci_valid(mrb_state *mrb, const char* location);
```

**機能:**
- `mrb_callinfo`構造体のサイズ計算
- ciスタックの容量・使用率計算
- cibase/ciend範囲チェック
- **realloc検出** (前回の値と比較)
- メモリプール所属確認

**出力例:**
```
[before_funcall] sizeof(mrb_callinfo)=48 bytes
[before_funcall] cibase=0x64880234c270 ciend=0x64880234c3f0 (capacity=8 frames, range=384 bytes)
[before_funcall] ci=0x64880234c360 (using 5/8 frames, 62%, offset=240 bytes)
```

### 3. realloc検出機能

static変数で前回の`cibase`/`ciend`を保存し、変化を検出:

```c
static mrb_callinfo *prev_cibase = NULL;
static mrb_callinfo *prev_ciend = NULL;
```

**realloc検出時の出力:**
```
W (xxx) app: [after_funcall] *** REALLOC DETECTED ***
W (xxx) app: [after_funcall]   cibase: 0x... -> 0x... (moved=YES, delta=14280 bytes)
W (xxx) app: [after_funcall]   ciend:  0x... -> 0x... (moved=YES, delta=14664 bytes)
```

## 発見した事実

### 1. mrb_callinfo構造体のサイズ

**Linux環境（64ビット）:**
```
sizeof(mrb_callinfo) = 48 bytes
```

**構造体定義** ([components/picoruby-esp32/picoruby/mrbgems/picoruby-mruby/lib/mruby/include/mruby.h](../components/picoruby-esp32/picoruby/mrbgems/picoruby-mruby/lib/mruby/include/mruby.h) 行165-181):
```c
typedef struct {
  uint8_t n:4;                  /* (15=*) c=n|nk<<4 */
  uint8_t nk:4;                 /* (15=*) */
  uint8_t cci;                  /* called from C function */
  uint8_t vis;                  /* visibility */
  mrb_sym mid;
  const struct RProc *proc;
  struct RProc *blk;
  mrb_value *stack;
  const mrb_code *pc;
  union {
    struct REnv *env;
    struct RClass *target_class;
    const void *keep_context;
  } u;
} mrb_callinfo;
```

### 2. ciスタックの初期容量

```
cibase=0x57aa715722e0
ciend=0x57aa71572460
range=384 bytes
capacity=8 frames  (384 / 48 = 8)
```

初期状態では**8フレーム**分のスタックが確保される。

### 3. reallocのトリガー

**ケース1: 初回の`puts`実行**

Ruby側のコード:
```ruby
def on_event(event)
  puts "on_event: gui app"
end
```

ログ:
```
I (177420240) app: === BEFORE mrb_funcall ===
I (177420240) app: [before_funcall] cibase=0x57aa715722e0 ciend=0x57aa71572460 (capacity=8 frames)
I (177420240) app: [before_funcall] ci=0x57aa715723d0 (using 5/8 frames, 62%)
I (177420240) app: GC arena saved: ai=3

on_event: gui app  ← putsの出力

I (177420240) app: === AFTER mrb_funcall ===
W (177420240) app: [after_funcall] *** REALLOC DETECTED ***
W (177420240) app: [after_funcall]   cibase: 0x57aa715722e0 -> 0x57aa71575aa8 (moved=YES, delta=14280 bytes)
W (177420240) app: [after_funcall]   ciend:  0x57aa71572460 -> 0x57aa71575da8 (moved=YES, delta=14664 bytes)
I (177420240) app: [after_funcall] cibase=0x57aa71575aa8 ciend=0x57aa71575da8 (capacity=16 frames)
I (177420240) app: [after_funcall] ci=0x57aa71575b98 (using 5/16 frames, 31%)
```

**発見:**
- `puts`の実行により、ciスタックが8フレームの容量を超える
- `cipush()`が`mrb_realloc()`を呼び出し、**8 frames → 16 frames**に拡張
- reallocにより`cibase`のアドレスが移動（delta=14280 bytes）
- 拡張後は5フレームしか使わないため、余裕ができる

**`puts`が多くのフレームを消費する理由:**
1. `puts`メソッド呼び出し
2. 内部でString生成
3. IO#write呼び出し
4. 各種メソッド呼び出しの積み重ね

### 4. 2回目以降は安定

reallocで16フレームに拡張された後、同じ`on_event`呼び出しは安定:

```
[before_funcall] ci=0x57aa71575b98 (using 5/16 frames, 31%)
on_event: gui app
[after_funcall]  ci=0x57aa71575b98 (using 5/16 frames, 31%)  ← 変化なし
```

## 発見した異常現象

### ciスタックのリーク

**ログ抜粋（最後のイベント）:**
```
I (177421493) app: === BEFORE mrb_funcall ===
I (177421493) app: [before_funcall] ci=0x57aa71575b98 (using 5/16 frames, 31%, offset=240 bytes)
I (177421493) app: GC arena saved: ai=3
I (177421493) app: === AFTER mrb_funcall ===
I (177421493) app: [after_funcall] ci=0x57aa71575bc8 (using 6/16 frames, 37%, offset=288 bytes)
I (177421493) app: GC arena restored to: ai=3
[INPUT_SOCKET] Client disconnected
```

**異常点:**
1. **`puts`の出力がない** - `on_event: gui app`が表示されていない
2. **`ci`が1フレーム増加** - 240 bytes → 288 bytes (+48 bytes = 1フレーム)
3. **例外チェックは通過** - `if (mrb->exc)`に引っかからない
4. **タイミング** - クライアント切断直前のイベント

### 正常時との比較

**正常時（2回目以降のイベント）:**
```
[before_funcall] ci=0x... (using 5/16 frames)
on_event: gui app  ← 出力あり
[after_funcall]  ci=0x... (using 5/16 frames)  ← 変化なし
```

**異常時（最後のイベント）:**
```
[before_funcall] ci=0x57aa71575b98 (using 5/16 frames)
                                   ← 出力なし
[after_funcall]  ci=0x57aa71575bc8 (using 6/16 frames)  ← 1フレーム増
```

### ciリークの影響

このまま続けば:
```
1回目: 5/16 frames
2回目: 5/16 frames (正常)
3回目: 5/16 frames (正常)
...
N回目: 6/16 frames (リーク)
N+1回目: 7/16 frames (さらにリーク)
...
N+10回目: 16/16 frames (capacity到達)
N+11回目: REALLOC → 32 frames
```

毎回リークすると、最終的にメモリ枯渇またはクラッシュ。

## 仮説

### 仮説1: `puts`の失敗と例外処理

**可能性:**
- I/Oエラー、バッファフル等で`puts`が失敗
- 内部で例外が発生するが、どこかで握りつぶされている
- 例外処理の過程で`ci`がpopされない

**根拠:**
- `puts`の出力がない
- しかし`mrb->exc`はNULL（例外チェックを通過）
- クライアント切断直前のタイミング

**検証方法:**
- `puts`の戻り値をチェック
- mruby内部の例外処理を追跡
- I/Oバッファの状態確認

### 仮説2: GCとの相互作用

**可能性:**
- `puts`実行中にGCが発動
- `mrb_gc_arena_restore()`が何らかの影響
- GCによるオブジェクト移動でスタック不整合

**根拠:**
- `GC arena saved: ai=3` → `GC arena restored to: ai=3`
- reallocとGCは両方ともメモリ再配置を行う

**検証方法:**
- GCログを有効化
- `mrb_gc_arena_restore()`前後でのci状態確認
- GC統計情報の収集

### 仮説3: 非同期処理との競合

**可能性:**
- クライアント切断処理と`on_event`処理の競合
- マルチタスク環境でのコンテキスト切り替え
- 別タスクがmrb_stateを操作

**根拠:**
- `[INPUT_SOCKET] Client disconnected`の直後
- タイミング依存の現象

**検証方法:**
- タスク間排他制御の確認
- mrb_state へのアクセスログ
- シングルスレッドでの再現テスト

### 仮説4: cipopの失敗

**可能性:**
- `mrb_funcall`から戻る際の`cipop()`が正しく実行されない
- 条件分岐でpopがスキップされる
- 例外パスでのクリーンアップ不足

**根拠:**
- ciが1フレーム増えたまま
- 通常の関数呼び出しなら`push`と`pop`は対

**検証方法:**
- vm.cの`cipop()`にログ追加
- 例外発生時のスタック巻き戻しを追跡

## 残された謎

### Q1: なぜ`puts`の出力がないのか？

- I/Oエラー？
- バッファリング？
- 例外で処理が中断？

### Q2: なぜ例外チェックを通過するのか？

```c
if (mrb->exc) {  // ← ここを通過している
    FMRB_LOGE(TAG, "Exception in on_event()");
    mrb_print_error(mrb);
    mrb->exc = NULL;
    return false;
}
```

- 例外が発生していない？
- 例外が既にクリアされている？
- 別の場所で処理済み？

### Q3: なぜciが1フレームだけ増えるのか？

- `cipush()`が1回余分に呼ばれる？
- `cipop()`が1回スキップされる？
- 何かの途中状態？

### Q4: クラッシュとの関連は？

- このciリークが積み重なってクラッシュ？
- それとも別の原因？

## 次のステップ候補

### 優先度: 高

1. **vm.cにデバッグログ追加**
   - `cipush()`のログ（呼び出し元、スタック状態）
   - `cipop()`のログ（正常/異常パス）
   - [lib/patch/picoruby-mruby/vm_cipush_debug.c](../lib/patch/picoruby-mruby/vm_cipush_debug.c)を適用

2. **例外処理の追跡**
   - mruby内部の例外処理フローを調査
   - `mrb_protect()`の使用状況確認
   - 例外が握りつぶされる箇所を特定

3. **`puts`の実装確認**
   - picorubyの`puts`実装を調査
   - エラー処理の有無
   - バッファリング動作

### 優先度: 中

4. **GCログの有効化**
   - GC発動タイミングの記録
   - ciとの相関分析

5. **タスク間排他制御の確認**
   - mrb_stateへのアクセス保護
   - マルチタスク環境での動作検証

6. **シンプルな再現テスト**
   - `puts`なしの`on_event`でテスト
   - シングルスレッド環境でテスト
   - 問題の切り分け

### 優先度: 低

7. **cibase事前拡大の効果測定**
   - `CALLINFO_INIT_SIZE`を32に増やす
   - reallocの頻度が減るか確認

8. **固定サイズスタックへの移行**
   - reallocを完全に排除
   - 安定性への影響確認

## 関連ドキュメント

- [doc/mruby_ci_pointer_crash_investigation_ja.md](mruby_ci_pointer_crash_investigation_ja.md) - 初期調査レポート
- [doc/gc-arena-howto_ja.md](gc-arena-howto_ja.md) - GC Arenaの使い方
- [lib/patch/picoruby-mruby/README_vm_cipush_debug.md](../lib/patch/picoruby-mruby/README_vm_cipush_debug.md) - cipushデバッグパッチ

## まとめ

### 明らかになったこと

1. **reallocは正常に動作している**
   - `puts`実行時にスタックが不足
   - 8 frames → 16 framesに拡張
   - メモリプール内で正しく再配置

2. **異常なciリークが発生している**
   - 特定条件下で`ci`が1フレーム増える
   - `puts`の出力がないケースと相関
   - クライアント切断時に発生しやすい

3. **クラッシュの直接原因は未解明**
   - ciリークが積み重なる可能性
   - しかし今回の観測ではクラッシュ未発生
   - さらなる調査が必要

### 次のアクション

**immediate:**
- vm.cのcipush/cipopにデバッグログを追加
- 例外処理フローを詳細に追跡
- `puts`が失敗するケースを再現

**long-term:**
- reallocを避けるための設計変更検討
- ciスタックの監視機能強化
- 自動テストでの再現
