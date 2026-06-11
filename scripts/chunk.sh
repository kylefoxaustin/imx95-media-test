#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-3-Clause
#
# Split a large file into <=N MB parts for upload to a board with a per-upload
# size limit, and reassemble + verify on the other side. Useful for the
# cross-built binary, a kernel/rootfs, or any oversized artifact.
#
#   chunk.sh split <file> [chunk_mb]   # default 190 MB; writes <file>.partNNN + <file>.manifest
#   chunk.sh join  <file>              # reassembles <file> from <file>.part* and verifies sha256
#
# Upload <file>.part* and <file>.manifest; on the board run: chunk.sh join <file>

set -euo pipefail

sha() { sha256sum "$1" | awk '{print $1}'; }

cmd_split() {
    local f="$1" mb="${2:-190}"
    [ -f "$f" ] || { echo "no such file: $f" >&2; exit 1; }
    rm -f "$f".part* "$f".manifest
    split -b "${mb}m" -d -a 3 "$f" "$f.part"
    {
        echo "file=$(basename "$f")"
        echo "bytes=$(wc -c < "$f")"
        echo "sha256=$(sha "$f")"
        echo "chunk_mb=$mb"
        echo "parts=$(ls -1 "$f".part* | wc -l)"
    } > "$f.manifest"
    echo "Wrote $(ls -1 "$f".part* | wc -l) part(s) + $(basename "$f").manifest"
    ls -lh "$f".part* "$f".manifest
}

cmd_join() {
    local f="$1"
    [ -f "$f.manifest" ] || { echo "missing $f.manifest" >&2; exit 1; }
    # shellcheck disable=SC1090
    local want; want=$(awk -F= '/^sha256=/{print $2}' "$f.manifest")
    cat "$f".part* > "$f"
    local got; got=$(sha "$f")
    if [ "$got" = "$want" ]; then
        echo "OK  $f  sha256 verified ($got)"
    else
        echo "FAIL  $f  sha256 mismatch" >&2
        echo "  expected $want" >&2
        echo "  got      $got" >&2
        exit 1
    fi
}

case "${1:-}" in
    split) shift; cmd_split "$@";;
    join)  shift; cmd_join  "$@";;
    *) echo "usage: chunk.sh {split <file> [chunk_mb] | join <file>}" >&2; exit 2;;
esac
