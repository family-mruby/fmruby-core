# Family mruby App Distribution 設計書

## 1. 目的

Family mruby 上で動作する Ruby アプリケーションを、Web を介して公開・試用・配布・実機インストールできる仕組みを構築する。

対象はゲームに限定せず、以下を含む。

- ゲーム
- ツール
- 教育アプリ
- デモ
- プレゼンテーション
- メディア再生
- IoT / ロボット操作
- ハードウェア制御
- 開発支援ツール

WASM（WebAssembly：ブラウザ上でバイナリコードを実行する仕組み）版 Family mruby を利用し、同じ Ruby アプリをブラウザ上で試用し、そのまま Family mruby 実機へ転送できることを特徴とする。

---

## 2. 基本コンセプト

```text
                    Family mruby App Hub
                             |
                       App Registry
                             |
              +--------------+--------------+
              |              |              |
              v              v              v
        Run in Browser   Install to Device   Source
        Family mruby       BLE / Serial      GitHub
            WASM
```

基本的なユーザー体験は以下とする。

1. Web 上の App Hub を開く
2. アプリ一覧からアプリを選択する
3. ブラウザ上でそのまま実行する
4. 必要であれば Family mruby 実機へ転送する
5. 同じ Ruby ソースを実機でも実行する

---

## 3. 設計方針

### 3.1 静的配信を基本とする

初期段階では専用バックエンドやデータベースを持たない。

以下のみで構成する。

- GitHub Repository
- GitHub Actions
- GitHub Pages
- GitHub Raw Content
- Web Bluetooth
- Web Serial
- Family mruby WASM

これにより運用コストを小さく保つ。

### 3.2 Ruby アプリを第一級の配布単位とする

ファームウェア全体ではなく、Ruby アプリケーション単位で配布する。

小規模アプリでは `.rb` ファイル単体でも配布可能とする。

複数ファイルや画像・音声などを含む場合は App Package として扱う。

### 3.3 Web版と実機版で同じアプリを利用する

原則として、

```text
同じ Ruby アプリ
   |
   +-- Family mruby WASM
   |
   +-- ESP32-S3
   |
   +-- ESP32-P4
   |
   +-- PC
```

という構成を目指す。

プラットフォーム固有機能は feature 判定によって切り替える。

---

## 4. GitHub Repository 構成

初期構成は以下を推奨する。

| Repository | 役割 |
|---|---|
| `family-mruby` | Family mruby 本体 |
| `family-mruby-apps` | 公開アプリ Registry および公式・コミュニティアプリ |
| `family-mruby-web` | WASM版、App Hub、Web Installer |

### 4.1 `family-mruby-apps`

```text
family-mruby-apps/
├─ apps/
│  ├─ breakout/
│  │  ├─ manifest.json
│  │  ├─ main.rb
│  │  ├─ icon.png
│  │  └─ screenshot.png
│  │
│  ├─ pico-rabbit/
│  │  ├─ manifest.json
│  │  ├─ main.rb
│  │  └─ ...
│  │
│  └─ robot-controller/
│     ├─ manifest.json
│     ├─ main.rb
│     └─ ...
│
├─ registry.json
└─ tools/
```

`registry.json` は原則として GitHub Actions で自動生成する。

---

## 5. App Manifest

各アプリは `manifest.json` を持つ。

例:

```json
{
  "id": "breakout",
  "name": "Breakout",
  "version": "1.0.0",
  "author": "example",
  "description": "A simple breakout game",
  "category": "game",
  "entrypoint": "main.rb",
  "icon": "icon.png",
  "screenshots": [
    "screenshot.png"
  ],
  "targets": [
    "wasm",
    "esp32s3",
    "esp32p4"
  ],
  "features": [
    "display",
    "touch",
    "audio"
  ],
  "startup": "fullscreen",
  "min_system_version": "0.1.0",
  "files": [
    "main.rb",
    "icon.png",
    "screenshot.png"
  ]
}
```

---

## 6. Manifest 項目

| 項目 | 必須 | 内容 |
|---|---:|---|
| `id` | yes | App Registry 上で一意な識別子 |
| `name` | yes | 表示名 |
| `version` | yes | アプリバージョン |
| `author` | yes | 作者 |
| `description` | yes | 概要 |
| `category` | yes | アプリ分類 |
| `entrypoint` | yes | 起動 Ruby ファイル |
| `icon` | no | アイコン |
| `screenshots` | no | スクリーンショット |
| `targets` | yes | 対応プラットフォーム |
| `features` | no | 必要機能 |
| `startup` | no | 起動モード |
| `min_system_version` | no | 必要 Family mruby バージョン |
| `files` | yes | インストール対象ファイル |

---

## 7. Category

初期カテゴリ候補:

```text
game
tool
education
demo
media
presentation
iot
robotics
development
other
```

カテゴリは表示・検索用であり、互換性判定には利用しない。

互換性は `targets` と `features` で判定する。

---

## 8. Feature

アプリが要求する機能を宣言する。

初期候補:

```text
display
touch
keyboard
mouse
audio
network
filesystem
gpio
i2c
spi
uart
camera
microphone
accelerometer
gyroscope
gps
ble
usb
```

App Hub は接続先実機または WASM 実行環境の capability と比較し、実行可否を判断する。

例:

```text
Robot Controller
requires:
  touch
  network

Breakout
requires:
  display
  touch
  audio
```

---

## 9. Registry

`registry.json` は App Hub が最初に読み込むアプリ一覧ファイルとする。

例:

```json
{
  "format_version": 1,
  "apps": [
    {
      "id": "breakout",
      "name": "Breakout",
      "version": "1.0.0",
      "category": "game",
      "manifest": "apps/breakout/manifest.json",
      "icon": "apps/breakout/icon.png"
    }
  ]
}
```

### 9.1 Registry を置く理由

ブラウザから GitHub REST API を大量に呼ばないため。

App Hub は通常、

```text
registry.json
```

を1回取得するだけでアプリ一覧を表示できる。

ユーザーがアプリを選択した時点で必要な Manifest とファイルのみ取得する。

---

## 10. GitHub Actions

Pull Request または main ブランチ更新時に以下を実行する。

```text
apps/*
   |
   v
manifest validation
   |
   v
file existence check
   |
   v
compatibility validation
   |
   v
registry.json generation
   |
   v
GitHub Pages deployment
```

チェック項目:

- `id` 重複
- JSON syntax
- 必須項目
- `entrypoint` の存在
- `files` の存在
- icon / screenshot の存在
- version format
- target 名の妥当性
- feature 名の妥当性

将来的には Ruby syntax check も行う。

---

## 11. App Hub

App Hub は静的 Web アプリケーションとして実装する。

画面例:

```text
Family mruby Apps

[ All ] [ Games ] [ Tools ] [ Demo ]

+--------------------------------+
| Breakout                       |
|                                |
| screenshot                     |
|                                |
| A simple breakout game         |
|                                |
| [ Run ] [ Install ] [ Source ] |
+--------------------------------+
```

### 11.1 Run

WASM版 Family mruby へアプリをロードして実行する。

```text
App Hub
   |
   v
manifest取得
   |
   v
files取得
   |
   v
WASM VFSへ配置
   |
   v
Family mruby起動
   |
   v
entrypoint実行
```

---

## 12. Startup App

WASM版 Family mruby は Startup App 指定に対応する。

例:

```text
https://example.org/run/?app=breakout
```

起動フロー:

```text
WASM load
   |
   v
Family mruby kernel
   |
   v
System VM
   |
   v
Startup App 判定
   |
   v
/apps/breakout/main.rb
```

スマートフォンではデスクトップ環境を表示せず、指定アプリを直接 fullscreen 起動できるようにする。

---

## 13. Web からのアプリ取得

通常は GitHub Raw Content または GitHub Pages から取得する。

```text
App Hub
   |
   +--> registry.json
   |
   +--> manifest.json
   |
   +--> main.rb
   |
   +--> assets/*
```

GitHub REST API をランタイムの必須要件にはしない。

---

## 14. BLE インストール

BLE（Bluetooth Low Energy：低消費電力Bluetooth）経由で Family mruby 実機へアプリを転送する。

概念フロー:

```text
Browser
   |
Web Bluetooth
   |
   v
Family mruby Device
   |
INSTALL_BEGIN
   |
manifest
   |
files
   |
INSTALL_COMMIT
   |
RUN
```

---

## 15. BLE App Transfer Protocol

初期プロトコル候補:

```text
HELLO
DEVICE_INFO
INSTALL_BEGIN
FILE_BEGIN
FILE_DATA
FILE_END
INSTALL_COMMIT
INSTALL_ABORT
REMOVE_APP
LIST_APPS
RUN_APP
```

必要に応じて chunk 単位で送信する。

各ファイルに以下を持たせる。

- path
- size
- hash
- offset

---

## 16. 安全なインストール

アプリは直接 `/apps/<id>` に書き込まない。

```text
/tmp/install/<id>
        |
        v
全ファイル受信
        |
        v
hash確認
        |
        v
manifest確認
        |
        v
atomic commit
        |
        v
/apps/<id>
```

転送中に BLE が切断されても、既存アプリを破壊しない。

---

## 17. App と User Data の分離

アプリ本体とユーザーデータを分離する。

```text
/apps/<app-id>/
    main.rb
    lib/
    assets/

/home/apps/<app-id>/
    settings.json
    save.dat
    cache/
```

これにより App Update 時にもセーブデータを保持できる。

---

## 18. App Update

実機側はインストール済み App の以下を保持する。

```text
id
version
installed_at
```

App Hub の Registry と比較し、

```text
Installed: 1.0.0
Available: 1.1.0

[ Update ]
```

を表示可能とする。

---

## 19. App Package

初期段階では個別ファイル取得でよい。

将来的には単一 Package 化する。

例:

```text
breakout.mrpkg
```

内部:

```text
manifest.json
main.rb
lib/
assets/
```

Package 形式は ZIP 互換または独自単純形式を候補とする。

ただし初期実装では Package Format を必須にしない。

---

## 20. Source 表示

各 App には source repository を設定可能とする。

例:

```json
{
  "source": "https://github.com/example/family-mruby-breakout"
}
```

App Hub 上から Ruby ソースへ直接アクセスできるようにする。

Family mruby の特徴として、

```text
Run
Install
Read Source
Modify
```

の距離を短くする。

---

## 21. Web Serial

Web Serial API（ブラウザからシリアルポートを利用する仕組み）は主に以下に使用する。

- Firmware Flash
- App Transfer
- Console
- REPL
- File Manager
- Log Viewer

ESP32 firmware 書き込みには `esptool-js` 等の利用を検討する。

---

## 22. Device Information

Web App Hub 接続時に実機から capability を取得する。

例:

```json
{
  "device": "M5Stack Tab5",
  "family_mruby_version": "0.5.0",
  "target": "esp32p4",
  "features": [
    "display",
    "touch",
    "audio",
    "wifi",
    "ble"
  ],
  "display": {
    "width": 1280,
    "height": 720
  }
}
```

これにより App Hub は、

```text
Compatible
Incompatible
Partially Supported
```

を判定できる。

---

## 23. WASM Capability

WASM版でも capability を定義する。

例:

```text
display
touch
keyboard
mouse
audio
filesystem
network
```

JavaScript bridge を利用して将来的に以下にも対応可能。

```text
accelerometer
gyroscope
camera
microphone
gps
vibration
```

---

## 24. セキュリティ

初期段階でも最低限以下を行う。

### 24.1 Hash

配布ファイルまたは Package に SHA-256
（Secure Hash Algorithm 256-bit：ファイル内容の改変を検出するハッシュ方式）
を付与する。

### 24.2 App ID

App ID は変更不可とする。

### 24.3 Path Validation

以下を禁止する。

```text
../
absolute path
/system/*
```

アプリは原則として、

```text
/apps/<app-id>/
```

以下にのみインストール可能とする。

### 24.4 将来

必要になれば以下を追加する。

- package signature
- trusted publisher
- permission system
- sandbox
- app review

---

## 25. Permission

将来的には `features` とは別に permission を追加する。

例:

```json
{
  "permissions": [
    "network",
    "filesystem.write",
    "gpio"
  ]
}
```

`features` は必要能力を表し、`permissions` はユーザー許可が必要な操作を表す。

---

## 26. Application Lifecycle

基本状態:

```text
NOT_INSTALLED
   |
   v
INSTALLED
   |
   +----> RUNNING
   |
   +----> UPDATE_AVAILABLE
   |
   +----> REMOVE
```

Startup App の場合:

```text
System Boot
   |
   v
Startup App
   |
   v
RUNNING
```

---

## 27. 初期実装範囲

最初の Proof of Concept では以下だけを実装する。

### Phase 1

1. `family-mruby-apps` repository
2. `manifest.json`
3. `registry.json`
4. GitHub Raw から `.rb` 取得
5. WASM版 Family mruby へロード
6. Startup App として実行

### Phase 2

1. Web Bluetooth 接続
2. `.rb` ファイル転送
3. 実機 `/apps` へのインストール
4. Web からアプリ起動
5. アプリ削除

### Phase 3

1. 複数ファイル
2. assets
3. version 管理
4. update
5. hash verification

### Phase 4

1. App Package
2. GitHub Actions Registry 生成
3. compatibility 判定
4. screenshot / category / search
5. Web Serial
6. Firmware Flash

---

## 28. MVP

MVP（Minimum Viable Product：最小限の機能で価値を確認する初期版）の完成条件を以下とする。

```text
GitHub に main.rb を公開
        |
        v
App Hub に表示
        |
        v
Run
        |
        v
ブラウザ Family mruby で実行
```

さらに、

```text
Install
   |
   v
BLE
   |
   v
ESP32 Family mruby
   |
   v
同じ main.rb を実行
```

まで到達すれば、Family mruby App Distribution の基本コンセプトが成立する。

---

## 29. 将来構想

### 29.1 分散 Repository

初期は中央 Repository へ App 本体を置く。

規模拡大後は Registry のみ中央管理し、App 本体は作者 Repository に置けるようにする。

```text
Central Registry
     |
     +--> Author A GitHub Release
     |
     +--> Author B GitHub Release
     |
     +--> Author C GitHub Release
```

### 29.2 App URL

アプリを直接共有可能にする。

```text
https://apps.family-mruby.org/app/breakout
```

または

```text
https://apps.family-mruby.org/run/breakout
```

URL を開くだけで Family mruby WASM が起動し、対象 App を実行する。

### 29.3 PWA

PWA（Progressive Web App：Webサイトをスマートフォンアプリのようにインストールできる仕組み）化し、

- Home Screen 起動
- Fullscreen
- Offline Cache
- Startup App

に対応する。

### 29.4 Community App Ecosystem

最終的には、

```text
Rubyを書く
   |
GitHubへ公開
   |
Pull Request
   |
App Registry登録
   |
Webで即実行
   |
実機へInstall
```

という開発・公開フローを目指す。

---

## 30. Family mruby 固有の強み

既存の組み込み向け App Store と比較した場合、Family mruby では以下が特徴となる。

1. Ruby ソースをそのまま配布できる
2. WASM版で同じ App を即時実行できる
3. ESP32 実機へ同じ App をインストールできる
4. OS・GUI・File System・複数 VM を含む共通実行環境を持つ
5. Web が単なる Store ではなく実行環境でもある
6. Source を読み、変更し、再実行するまでの距離が短い

最終的なコンセプトは以下とする。

> Write once in Ruby, run in the browser, install on Family mruby devices.

