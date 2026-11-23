#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../../../.." && pwd)
binary=$(mktemp /tmp/nixel-mixer.XXXXXX)
trap 'rm -f "$binary"' EXIT HUP INT TERM
"${CC:-cc}" -O2 -Wall -Wextra -Werror -Wno-sign-compare -fno-builtin \
    -I"$root/src/include" \
    "$root/src/system/services/audio/tests/mixer_test.c" -o "$binary"
"$binary"
"${CC:-cc}" -O2 -Wall -Wextra -Werror -fno-pie -no-pie \
    -Wno-int-to-pointer-cast -Wno-pointer-to-int-cast \
    -ffunction-sections -fdata-sections -Wl,--gc-sections \
    "$root/src/system/services/audio/tests/hda_test.c" -o "$binary"
"$binary"
