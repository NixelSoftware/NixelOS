#!/bin/sh
set -eu
cd "$(dirname "$0")/../../../.."
test_binary=$(mktemp /tmp/nixel-irc-test.XXXXXX)
trap 'rm -f "$test_binary"' EXIT HUP INT TERM
${CC:-cc} -std=c99 -Wall -Wextra -Werror -fsanitize=address,undefined src/apps/irc/tests/test_irc.c -o "$test_binary"
ASAN_OPTIONS=detect_leaks=0 "$test_binary"
