# ヘッドレス S3 S1 指示書: StickS3 の 6 軸センサで一歩を検知し、Asterism に流す

> 状態: 指示 | 更新: 2026-10-24 | M3s で起動した StickS3 (`FMRB_HW_TARGET=STICKS3`) の BMI270 を Ruby から使い、歩いた一歩ごとに Asterism へ値を出すアプリを作る。受け側 (ブラウザ版 Family mruby で歩くキャラを動かす) は次の段で作るので、ここでは「一歩がすぐ網に出る」ところまで。ユーザの来週の用事に使うため、遅れの少なさを最優先にする

言葉: 文書は常体、「確かめ」ではなく「確認」。端末と呼ぶ。コードのコメントは英語。

前提: doc/headless_s3/report/m3s.md (ピン、I2C の石 0x68 = BMI270、ボタン KEY1 G11・KEY2 G12、WiFi・devctl・Asterism の繋ぎ方)、report/h3.md (`asterism_demo` の作り、WiFi を待つ)、doc/imu/report/p1.md とブランチ `feature/imu-bmi270` (584e1264・59bc3181: Tab5 向けの BMI270 の Ruby の gem と設定データ 8192 バイトの流し込み、試験 tool/imu/test/verify.rb)、.claude/skills/fmrb-app-new、ルートの CLAUDE.md (MCP)。

## 1. 作るもの

1. **BMI270 を S3 で使う**: ブランチ `feature/imu-bmi270` の gem と設定データを develop に取り込み (cherry-pick か写し。Tab5 の I2C の経路 (hw_proxy) と S3 のシステムの I2C (SDA 47・SCL 48) の違いを吸収)、StickS3 で加速度が読めるようにする。Tab5 で壊れないこと (Tab5 では読める I2C の経路のまま。Tab5 の実機確認はしない)。
2. **一歩の検知**: 加速度 (50〜100 Hz) から一歩を拾う。
   - **遅れの少なさを最優先**: 足が着いてから網に出るまで 100 ms 程度を目標。BMI270 の歩数カウンタ (数歩まとめて増える) は使わない。BMI270 の「一歩ごとの検知 (step detector)」を使うか、Ruby で 3 軸の大きさの山を拾う (ならし、しきい値、前の山から 0.25〜2 秒) かは、遅れと誤検知を比べて選び、report に理由を書く。BMI270 の割り込みの線は S3 に来ていない (M5PM1 の G4) ので、使うなら読みに行く形。
   - 手に持って振る・机を叩くでも数えてしまうのは許す (舞台では歩くだけ)。ただし止まっているときに勝手に数えないこと。
3. **アプリ** (`flash/app/test/` か適切な場所に `step_sensor.app.rb` + `.app.toml`。名前は既存の流儀で決めてよい):
   - startup_app にできる (WiFi を待ってから Asterism に繋ぐ。H3 の作りに合わせる。繋がらなかった・切れたら繋ぎ直す (今の gem は 1 回だけつなぐので、アプリの側で数秒おきに試す)。再起動の直後に前の自分がまだ網に残っていて断られる場合 (「already on the network」) も、待って繋ぎ直す)。
   - 一歩ごとに Asterism のアプリ自身のキーに値を出す (今の Asterism のやり方: `asterism_values` で読める形)。中身は少なくとも「通算の歩数」と「一歩の時刻」、できれば「歩く速さ (直近の歩調、歩/分)」。歩いていないときも数秒おきに同じ値を出してよい (受け側が繋いだ直後に今の値が分かるように)。
   - Asterism のオブジェクトとして `reset` (歩数を 0 に) と状態を返すメソッドを公開する。
   - 画面 (240x135): 歩数を大きく、接続の状態を小さく。KEY1 (表のボタン) で歩数を 0 に。
4. **受け側の確認の道具**: PC の CRuby で値を見る簡単な方法 (`asterism` の CLI、または小さな Ruby スクリプト)。一歩が出てから PC に届くまでの時間を測る。

## 2. 確認 (受け入れ)

- 実機の StickS3 (IP は tab5_ip ではなく、M5 の mDNS か手元の ARP で探す。ip を tab5_* に渡す) で、アプリが起動して手元の zenohd に繋がり、`/fb` で画面が撮れる。
- 歩く検知は**ユーザが振って・歩いて確認する**。止まらずに先へ進め、確認の手順 (何をして、PC で何が見えれば合格か) を report に書く。自分でできる確認として、加速度のログを採って (端末を置いたまま) 勝手に数えないこと、検知の部分を CRuby の試験で合成の波形 (1.5〜2.5 歩/秒の歩行、静止、雑音) にかけて数えた数と遅れを report に。
- Asterism: PC で値が更新されるのが見える。PC から `reset` を呼べる。端末で一歩を出してから PC に届くまでの時間 (ログの時刻の差でよい)。
- アプリを落としても端末ごと落ちない。`Guru|abort` 0。内蔵 RAM の定常を M3s の値 (132,064 B) と並べる。
- `rake test` が通る。Tab5・Retro・AtomS3R のビルドが通る (共通の部分を変えた場合)。

## 3. 範囲・決まり

- **触ってよいもの**: fmruby-core (develop) の BMI270 の gem・設定データの取り込み (lib/add の決まり)、新しいアプリ、ビルドの gem の一覧、試験、report。StickS3 への flash・serial・devctl (ポートとファームの種類を確認してから。`FMRB_HW_TARGET=STICKS3 rake flash`)。手元の zenohd (親の compose、zenoh-lan を重ねる)。
- **触らないもの**: sdkconfig 全部、`.env` の既定、Tab5 (焼かない・リセットしない)、VPS、`~/asterism_keys/`、ブラウザ版 (次の段)、asterism / asterism-games の gem。
- StickS3 の起動の注意: 書き込み待ちから抜けないときは、**電源ボタンを 1 回クリック**するとフラッシュから起動する (ユーザが確認済み)。これはユーザの手が要るので、焼いたあとに書き込み待ちに留まったら、まず esptool の `--after hard_reset` などで抜けられるか試し、抜けられなければ M3s の RAM 起動で続けるか、止まって返す。
- push・タグはしない。`git commit -a` は使わない。件名は履歴に合わせる (`imu:` `app:` など)、本文英語、末尾 `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`。doc (この指示書・report) はコミットしない (親がする)。
- 端末固有の値 (MAC、mDNS の後半、SSID、LAN の IP) を report・コミットに書かない。
- **止まる条件**: sdkconfig を変える必要がある、既存の端末の内蔵 RAM が増える、Asterism の gem の変更が要る、ユーザの手が無いと先へ進めない。
- **report** (`doc/headless_s3/report/s1.md`、常体): 取り込みの方法、検知の方式と選んだ理由、合成の波形での数と遅れ、網での値の形 (キーと中身)、PC に届くまでの時間、内蔵 RAM、ユーザに確認してほしい手順、残件、コミット。
