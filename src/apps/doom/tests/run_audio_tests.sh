#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../../.." && pwd)
binary=$(mktemp /tmp/nixel-doom-audio.XXXXXX)
trap 'rm -f "$binary"' EXIT HUP INT TERM
"${CC:-cc}" -O2 -fno-builtin -DFEATURE_SOUND -I"$root/src/include" \
    -I"$root/src/apps/doom/compat" "$root/src/apps/doom/tests/audio_test.c" -o "$binary"
"$binary"
