#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-3-Clause
#
# Generate the Big Buck Bunny H.264 Annex-B test clips that get baked into the
# binary (see CMakeLists.txt). Downloads the 1080p source once and transcodes
# three native-resolution clips. Clips land in assets/clips/ and are NOT
# committed (.gitignore). Re-run only when you want to refresh them.
#
# Big Buck Bunny (c) Blender Foundation, Creative Commons Attribution 3.0.
# Requires: ffmpeg (with libx264), curl.

set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
dest="$here/assets/clips"
src="${BBB_SRC:-/tmp/bbb_src.mov}"
url="https://download.blender.org/peach/bigbuckbunny_movies/big_buck_bunny_1080p_h264.mov"
seg_start="${SEG_START:-330}"   # high-motion segment -> not trivially compressible
seg_len="${SEG_LEN:-5}"

command -v ffmpeg >/dev/null || { echo "ffmpeg not found" >&2; exit 1; }
mkdir -p "$dest"

if [ ! -f "$src" ]; then
    echo "Downloading BBB 1080p source (~700 MB) to $src ..."
    curl -L --fail -o "$src" "$url"
fi

common=(-ss "$seg_start" -t "$seg_len" -an -c:v libx264 -pix_fmt yuv420p
        -profile:v high -g 48 -keyint_min 48 -f h264 -y)

echo "Transcoding 720p ..."
ffmpeg -hide_banner -loglevel error -i "$src" -vf scale=1280:720 \
       -b:v 6M  -maxrate 6M  -bufsize 12M "${common[@]}" "$dest/bbb_720p.h264"
echo "Transcoding 1080p ..."
ffmpeg -hide_banner -loglevel error -i "$src" -vf scale=1920:1080 \
       -b:v 12M -maxrate 12M -bufsize 24M "${common[@]}" "$dest/bbb_1080p.h264"
echo "Transcoding 4K (upscale) ..."
ffmpeg -hide_banner -loglevel error -i "$src" -vf scale=3840:2160 \
       -b:v 25M -maxrate 25M -bufsize 50M "${common[@]}" "$dest/bbb_4k.h264"

echo
echo "Done. Clips in $dest (will be embedded on next build):"
ls -lh "$dest"/*.h264
