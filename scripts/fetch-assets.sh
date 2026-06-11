#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-3-Clause
#
# Fetch open test media for the VPU decode workloads. Clips are downloaded into
# assets/clips/ and are NOT committed to the repo (see .gitignore).
#
# Big Buck Bunny (c) Blender Foundation, Creative Commons Attribution 3.0.
# https://peach.blender.org/
#
# NOTE: the exact source URLs / per-resolution encodes used by the real VPU
# decode backend will be pinned here once that backend lands. For now this
# script documents intent and sets up the directory layout.

set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
dest="$here/assets/clips"
mkdir -p "$dest"

echo "Asset destination: $dest"
echo
echo "TODO: pin Big Buck Bunny encodes (H.264/H.265 @ 720p/1080p/4k) here when"
echo "the V4L2 decode backend lands. Placeholder for now."
echo
echo "Suggested sources to evaluate:"
echo "  - https://peach.blender.org/download/        (originals)"
echo "  - https://test-videos.co.uk/big-buck-bunny   (per-res H.264/H.265 clips)"
