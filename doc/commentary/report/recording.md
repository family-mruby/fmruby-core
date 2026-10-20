# 実況の画面と声を録画する手順

> 状態: 手順 | 更新: 2026-10-21 | PC の実況アプリ (`asterism-games commentate mahjong`) のページを、画面と声 (ずんだもん) ごと MP4 に録る。2026-10-21 に VPS の卓の観戦対局を 75 秒録り、ユーザが確認した (問題なし)

## 1. 仕組み

- 実況アプリは手元 (WSL) の `127.0.0.1:<port>` でページを出す。
- 録画用のコンテナの中で、仮想の画面 (Xvfb) と仮想のスピーカー (PulseAudio の空の出力先 `rec`) を作り、Chromium でそのページを全画面で開く。ffmpeg が画面 (`x11grab`) と、空の出力先に流れた音 (`rec.monitor`) を一緒に録る。
- ページは最初のクリックまで声を出さない (ブラウザの自動再生の決まり) ので、`xdotool` で 1 回クリックする。
- **Docker Desktop の注意**: コンテナから WSL の `127.0.0.1` には直接届かない (`--network host` でも届かない)。コンテナの中で `socat` を `127.0.0.1:<port>` に立て、`host.docker.internal:<port>` へ流す (WSL の網が mirrored なら、Windows の localhost を通って WSL に届く)。実況アプリは Host が `127.0.0.1` 以外の要求を断るので、ページは必ずコンテナの中の `127.0.0.1` で開く。

## 2. 録画用のイメージ (1 回だけ)

```
mkdir -p ~/rec && cd ~/rec
cat > Dockerfile <<'EOF'
FROM mcr.microsoft.com/playwright:v1.56.0-noble
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends xvfb pulseaudio ffmpeg fonts-noto-cjk xdotool socat && rm -rf /var/lib/apt/lists/*
EOF
docker build -t fmrb-rec:local .
```

Chromium は playwright のイメージに入っているものを使う (`/ms-playwright/chromium-*/chrome-linux/chrome`)。

## 3. 録画の台本 (record.sh)

```
#!/bin/bash
# Records the commentary page (screen + sound) for SECS seconds into /out/OUT.
set -e
PORT=${PORT:-7471}
URL=http://127.0.0.1:$PORT/
SECS=${SECS:-70}
OUT=${OUT:-commentary.mp4}
W=1280; H=720
socat TCP-LISTEN:$PORT,bind=127.0.0.1,fork,reuseaddr TCP:host.docker.internal:$PORT &
export XDG_RUNTIME_DIR=/tmp/xdg; mkdir -p $XDG_RUNTIME_DIR; chmod 700 $XDG_RUNTIME_DIR
pulseaudio -D --exit-idle-time=-1 --system=false --disallow-exit
sleep 1
pactl load-module module-null-sink sink_name=rec >/dev/null
pactl set-default-sink rec
Xvfb :99 -screen 0 ${W}x${H}x24 -nolisten tcp &
export DISPLAY=:99
sleep 1
CHROME=$(ls -d /ms-playwright/chromium-*/chrome-linux/chrome | head -1)
$CHROME --no-sandbox --user-data-dir=/tmp/prof --no-first-run --kiosk \
  --window-position=0,0 --window-size=${W},${H} --autoplay-policy=no-user-gesture-required \
  --disable-gpu "$URL" >/tmp/chrome.log 2>&1 &
sleep 6
xdotool mousemove 640 400 click 1
sleep 1
ffmpeg -loglevel error -y -video_size ${W}x${H} -framerate 15 -f x11grab -i :99 \
  -f pulse -i rec.monitor -t "$SECS" -c:v libx264 -preset veryfast -pix_fmt yuv420p \
  -c:a aac -b:a 128k "/out/$OUT"
echo "recorded /out/$OUT"
```

## 4. 録る

1. **VOICEVOX** (Windows のアプリ) を起動しておく。
2. **実況アプリ**を、ブラウザを開かずに別の番号で起動する (AI のひとことを入れるならキーを渡す)。
   ```
   cd ~/fmrb/family-mruby/asterism-games
   ANTHROPIC_API_KEY=$(cat ~/asterism_keys/anthropic_api_key) \
     bundle exec bin/asterism-games commentate mahjong \
     --credentials ~/asterism_keys/ai-viewer.tar --table games/games/mahjong1 --no-open --port 7471
   ```
3. **対局を始める**: 卓の主がブラウザ版か `play mahjong` で「観戦で始める」(W)。
4. **録る** (例: 75 秒):
   ```
   mkdir -p ~/rec/out
   docker run --rm --name fmrb-rec -e PORT=7471 -e SECS=75 -e OUT=fmrb_commentary_$(date +%Y%m%d_%H%M).mp4 \
     -v ~/rec/record.sh:/record.sh:ro -v ~/rec/out:/out fmrb-rec:local bash /record.sh
   ```
5. **確認する**:
   ```
   ffmpeg -hide_banner -i ~/rec/out/<名前>.mp4 -af volumedetect -f null - 2>&1 | grep max_volume   # 音が入っているか (-91 dB なら無音)
   ffmpeg -v error -ss 30 -i ~/rec/out/<名前>.mp4 -frames:v 1 /tmp/frame.png                     # 途中の 1 枚
   ```
6. **共有の場所へ置く** (Windows の共有フォルダ):
   ```
   powershell.exe -NoProfile -Command "Copy-Item -LiteralPath '$(wslpath -w ~/rec/out/<名前>.mp4)' -Destination '\\<NAS>\<共有>\<名前>.mp4'"
   ```
7. 実況アプリを止める (Ctrl-C)。

## 5. 実績と目安 (2026-10-21)

- 1280x720・15fps・H.264 + AAC 128kbps で、75 秒が 1.8MB。
- 音は最大 -5.6 dB・平均 -29 dB (声と決まり文句の WAV)。
- 録った場面: 東 1 局の始まりから東 2 局の途中 (テンパイ、チー、リーチ、AI のひとこと、ロン、トップの入れ替わり)。AI のひとことは 4 回で $0.0006。

## 6. つまずいたこと

- `--network host` で `127.0.0.1` を開くと「接続を拒否」になった (Docker Desktop のコンテナからは WSL の localhost が見えない)。socat の中継で解決した。
- 最初の試し撮りは無音 (-91 dB) だった。ページが開けていなかったため。ページが開ければ声は録れる。
- キーボードの配列の警告 (`Could not resolve keysym`) は Xvfb が出すもので、録画には関係ない。
