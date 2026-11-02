#!/usr/bin/env bash
# Renders a short synthetic clip with every preset through the CLI.
# Skips (exit 0) when ffmpeg is not installed.
set -euo pipefail
CLI="$1"
command -v ffmpeg >/dev/null || { echo "ffmpeg not found, skipping"; exit 0; }
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
ffmpeg -v error -f lavfi -i testsrc2=size=640x360:rate=25:duration=0.4 "$TMP/in.mp4"
"$CLI" --list | grep -q '"Living-Room TV"'
while IFS= read -r preset; do
  "$CLI" "$TMP/in.mp4" "$TMP/out.mp4" --preset "$preset" 2>/dev/null
  frames=$(ffprobe -v error -count_frames -select_streams v:0 -show_entries stream=nb_read_frames -of csv=p=0 "$TMP/out.mp4")
  [ "$frames" -ge 9 ] || { echo "FAIL: $preset produced $frames frames"; exit 1; }
  echo "ok: $preset"
done < <("$CLI" --list | sed -n 's/^  "\(.*\)"$/\1/p')
"$CLI" "$TMP/in.mp4" "$TMP/still.png" --set screen=2 --set zoom=5 --set lookAt=0.3,0.6 2>/dev/null
[ -s "$TMP/still.png" ] && echo "ok: still + --set"
