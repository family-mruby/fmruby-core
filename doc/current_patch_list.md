# 現在適用中のパッチ一覧

## パッチ適用方法

Rakefile内で`cp`コマンドにより、`lib/patch`と`lib/replace`から`components/picoruby-esp32/picoruby`配下にファイルを上書きコピー。

## 完全なパッチリスト

### 1. picoruby-mruby (mrubyタスクシステム)

```ruby
# Rakefile L39-40
sh "cp -rf lib/patch/picoruby-mruby #{mrbgem_path}/"
sh "cp -f lib/replace/picoruby-machine/include/hal.h #{mrbgem_path}/picoruby-mruby/include/"
```

#### 置き換えられるファイル

```
lib/patch/picoruby-mruby/
├── include/
│   ├── hal.h          ★HAL抽象化インターフェース
│   └── task.h
├── src/
│   ├── task.c         ★★★古いmrubyc実装(967行)
│   └── alloc.c        メモリアロケータ
├── vm_cipush_debug.c  CI stackデバッグ
└── mrbgems/
    └── mruby-io/
        └── mrblib/
            └── file_constants.rb
```

**最重要**: `task.c`は本流と全く異なる実装

### 2. picoruby-machine (HAL実装層)

```ruby
# Rakefile L39
sh "cp -rf lib/replace/picoruby-machine #{mrbgem_path}/"
```

#### 置き換えられるファイル

```
lib/replace/picoruby-machine/
├── include/
│   ├── hal.h          HALインターフェース定義
│   └── machine.h
├── src/
│   └── machine.c
├── ports/
│   ├── esp32/
│   │   ├── hal.c      ★★マルチVM tick実装
│   │   └── machine.c
│   ├── posix/
│   │   ├── hal.c      POSIX用HAL
│   │   └── machine.c
│   └── rp2040/
│       └── machine.c  RaspberryPi Pico用
├── mrblib/
└── example/
```

**重要**: `ports/esp32/hal.c`にマルチVM対応のmruby_tick_task実装

### 3. mruby-compiler2 (コンパイラ)

```ruby
# Rakefile L44-48
sh "cp -f lib/patch/compiler/prism_xallocator.h #{mrbgem_path}/mruby-compiler2/include/"
sh "cp -f lib/patch/compiler/prism_alloc.c #{mrbgem_path}/mruby-compiler2/lib/"
sh "cp -f lib/patch/compiler/mruby-compiler2-mrbgem.rake #{mrbgem_path}/mruby-compiler2/mrbgem.rake"
sh "cp -f lib/patch/compiler/prism_tlsf_wrapper.c #{mrbgem_path}/mruby-compiler2/lib/"
sh "cp -f lib/patch/compiler/mruby-compiler2-compile.c #{mrbgem_path}/mruby-compiler2/src/compile.c"
```

#### 置き換えられるファイル

```
lib/patch/compiler/
├── prism_xallocator.h           カスタムアロケータ
├── prism_alloc.c                メモリ割り当て実装
├── prism_tlsf_wrapper.c         TLSFアロケータラッパー
├── mruby-compiler2-compile.c    コンパイラ本体
└── mruby-compiler2-mrbgem.rake  ビルド設定
```

**用途**: ESP32のメモリ制約に対応したアロケータ

### 4. その他のmrbgem

#### picoruby-env

```ruby
# Rakefile L43
sh "cp -f lib/patch/picoruby-env/ports/posix/env.c #{mrbgem_path}/picoruby-env/ports/posix/"
```

```
lib/patch/picoruby-env/
└── ports/
    └── posix/
        └── env.c    環境変数アクセス
```

#### ビルド設定ファイル

```ruby
# Rakefile L45-47
sh "cp -f lib/patch/picoruby-require/mrbgem.rake #{mrbgem_path}/picoruby-require/"
sh "cp -f lib/patch/picoruby-yaml/mrbgem.rake #{mrbgem_path}/picoruby-yaml/"
sh "cp -f lib/patch/picoruby-sandbox/mrbgem.rake #{mrbgem_path}/picoruby-sandbox/"
```

```
lib/patch/
├── picoruby-require/
│   └── mrbgem.rake
├── picoruby-yaml/
│   └── mrbgem.rake
└── picoruby-sandbox/
    └── mrbgem.rake
```

**用途**: ビルドオプションのカスタマイズ

### 5. ESP32固有コンポーネント

#### esp_littlefs

```ruby
# Rakefile L42
sh "cp -f lib/patch/esp_littlefs/CMakeLists.txt components/esp_littlefs/"
```

```
lib/patch/esp_littlefs/
└── CMakeLists.txt    ビルド設定
```

**用途**: LittleFSファイルシステムのビルド設定

## パッチの目的別分類

### A. タスクシステム関連 (本流統合で置き換え対象)
- ✅ `lib/patch/picoruby-mruby/src/task.c` - **本流の新実装に置き換え**
- ⚠️ `lib/patch/picoruby-mruby/include/hal.h` - HAL IFは要確認
- ⚠️ `lib/patch/picoruby-mruby/include/task.h` - 本流と統合可能か確認

### B. マルチVM対応 (family-mruby独自、要保持)
- ⚠️ `lib/replace/picoruby-machine/ports/esp32/hal.c` - マルチVM tick実装
  - **問題**: 外部からmrb_tick()を呼ぶレースコンディション
  - **対策案**: scheduler_lockを考慮した修正、またはメッセージング方式に変更

### C. メモリ管理 (ESP32制約対応、要保持)
- ✅ `lib/patch/compiler/prism_*` - カスタムアロケータ
- ✅ `lib/patch/picoruby-mruby/src/alloc.c` - メモリアロケータ

### D. ビルド設定 (環境依存、要保持)
- ✅ `lib/patch/*/mrbgem.rake` - ビルドオプション
- ✅ `lib/patch/esp_littlefs/CMakeLists.txt` - ESP32固有設定

### E. デバッグ機能 (本流統合で要確認)
- ⚠️ `lib/patch/picoruby-mruby/vm_cipush_debug.c` - CI stackデバッグ
  - 本流で同等機能があるか確認

## 本流統合時の対応方針

### 即座に置き換え可能
1. task.c - 本流の新実装(1573行)を使用
2. task.h - 本流の定義を使用

### 修正して保持
1. hal.c (ESP32) - scheduler_lock対応版に書き換え
   - `mrb_set_in_c_funcall()` → scheduler_lockベースに変更
   - または、メッセージング方式に変更検討

### そのまま保持
1. コンパイラパッチ (prism_*)
2. ビルド設定ファイル (mrbgem.rake)
3. alloc.c

### 要調査
1. hal.h - 本流との互換性
2. vm_cipush_debug.c - 本流での代替手段
3. picoruby-env/ports/posix/env.c - 変更理由の確認

## ファイルサイズ比較

```bash
# 主要ファイルの行数
$ wc -l lib/patch/picoruby-mruby/src/task.c
967 lib/patch/picoruby-mruby/src/task.c

$ wc -l /home/kishima/fmrb/investigate/picoruby/mrbgems/picoruby-mruby/lib/mruby/mrbgems/mruby-task/src/task.c
1573 /home/kishima/fmrb/investigate/picoruby/.../task.c

# 本流の方が606行多い (scheduler_lock, mrb_execute_proc_synchronously等の実装)
```

## 変更履歴の確認方法

```bash
# パッチファイルの変更履歴
cd /home/kishima/fmrb/family-mruby/fmruby-core
git log --oneline --all -- lib/patch/picoruby-mruby/src/task.c
git log --oneline --all -- lib/replace/picoruby-machine/ports/esp32/hal.c

# 本流の最新状態
cd /home/kishima/fmrb/investigate/picoruby
git log --oneline -20 mrbgems/picoruby-mruby/lib/mruby/mrbgems/mruby-task/src/task.c
```

## 次のステップ

1. ✅ パッチ一覧作成完了
2. ⏭️ 本流の最新コミット確認
3. ⏭️ scheduler_lock実装の調査
4. ⏭️ サブモジュール更新とテストビルド
