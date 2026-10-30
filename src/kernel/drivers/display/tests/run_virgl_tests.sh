#!/bin/sh
set -eu
repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/../../../../.." && pwd)
test_dir="$repo_dir/src/kernel/drivers/display/tests"
build_dir=$(mktemp -d /tmp/nixel-virgl-test.XXXXXX)
trap 'rm -rf "$build_dir"' EXIT HUP INT TERM
${CC:-cc} -O2 -Wall -Wextra -Werror -Wno-int-to-pointer-cast \
    -Wno-pointer-to-int-cast -DNIXEL_KERNEL=1 -ffunction-sections \
    -fdata-sections -no-pie -Wl,--gc-sections \
    "$test_dir/virtio_transport_test.c" -o "$build_dir/transport-test"
"$build_dir/transport-test"
# Linux x86-64 host: low-address allocations model i386 physical DMA pointers.
${CC:-cc} -O2 -Wall -Wextra -Werror -Wno-int-to-pointer-cast \
    -Wno-pointer-to-int-cast -c "$repo_dir/src/kernel/drivers/display/virgl.c" \
    -o "$build_dir/virgl.o"
${CC:-cc} -O2 -Wall -Wextra -Werror "$test_dir/virgl_host_test.c" \
    "$build_dir/virgl.o" -l:libvirglrenderer.so.1 -lEGL -o "$build_dir/virgl-test"
# Shader/raster tests run against Mesa software GL when physical GPU access is
# absent. These verify the actual host protocol, not hardware performance.
LIBGL_ALWAYS_SOFTWARE=1 "$build_dir/virgl-test"
"$build_dir/virgl-test" --reject-init
