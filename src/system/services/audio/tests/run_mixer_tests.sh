#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../../../.." && pwd)
binary=$(mktemp /tmp/nixel-mixer.XXXXXX)
trap 'rm -f "$binary"' EXIT HUP INT TERM
"${CC:-cc}" -O2 -Wall -Wextra -Werror -Wno-sign-compare -fno-builtin \
    -I"$root/src/include" \
    "$root/src/system/services/audio/tests/mixer_test.c" -o "$binary"
"$binary"
