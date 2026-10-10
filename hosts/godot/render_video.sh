#!/usr/bin/env bash
# Renders the demo stage reacting to a song into an MP4 with sound, using
# Godot's Movie Maker mode (fixed frame rate, so the video is in sync even
# when rendering is slower than real time).
#
#   hosts/godot/render_video.sh <song.wav> <out.mp4> [start_s] [length_s] [caption] [timeline]
#
# Pass 1 as `timeline` to show the Timeline panel along the bottom.
#
# Needs: the extension built (cmake --preset godot && cmake --build --preset godot),
# Godot 4.5+ as $GODOT (default: godot on PATH) and ffmpeg. With no display,
# it runs under xvfb-run if available.
set -euo pipefail

song=$(realpath "$1")
out=$(realpath -m "$2")
start=${3:-0}
length=${4:-30}
caption=${5:-}
timeline=${6:-0}
godot=${GODOT:-godot}
project="$(cd "$(dirname "$0")/project" && pwd)"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

wrap=()
if [[ -z "${DISPLAY:-}" ]] && command -v xvfb-run >/dev/null; then
  wrap=(xvfb-run -a -s "-screen 0 1280x720x24")
fi

# Godot's WAV loader takes plain PCM only (not WAVE_FORMAT_EXTENSIBLE), so
# play and analyse a 16-bit PCM copy.
ffmpeg -loglevel error -y -i "$song" -c:a pcm_s16le "$tmp/song.wav"

[[ -d "$project/.godot" ]] || "$godot" --headless --path "$project" --import >/dev/null 2>&1 || true

"${wrap[@]}" "$godot" --path "$project" --rendering-driver opengl3 --resolution 1280x720 \
  --write-movie "$tmp/frames.avi" --fixed-fps 30 res://demo/stage.tscn \
  -- --song="$tmp/song.wav" --start="$start" --length="$length" --caption="$caption" --timeline="$timeline"

ffmpeg -loglevel error -y -i "$tmp/frames.avi" -c:v libx264 -pix_fmt yuv420p -crf 20 \
  -c:a aac -b:a 160k -movflags +faststart "$out"
echo "wrote $out"
