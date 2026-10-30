#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../../../.." && pwd)
output=${1:-/tmp/mixprobe.bin}
build=$(mktemp -d /tmp/nixel-mixprobe.XXXXXX)
trap 'rm -rf "$build"' EXIT HUP INT TERM
cat > "$build/entry.asm" <<'ASM'
bits 32
section .text
global _start
extern user_main
dd 0x31505041
db 3,0,0,0
_start:
    push dword [esp+4]
    call user_main
    add esp,4
    mov eax,3
    xor ebx,ebx
    int 0x80
    jmp $
ASM
${NASM:-nasm} -f elf32 "$build/entry.asm" -o "$build/entry.o"
${CC:-cc} -m32 -O2 -ffreestanding -fno-pie -fno-stack-protector -mno-sse -mno-sse2 \
    -I"$root/src/include" -c "$root/src/system/services/audio/tests/mixprobe.c" -o "$build/main.o"
${LD:-ld} -m elf_i386 -Ttext 0xC00000 -e _start "$build/entry.o" "$build/main.o" -o "$build/app.elf"
${OBJCOPY:-objcopy} -O binary --remove-section=.bss "$build/app.elf" "$output"
printf 'Built %s\n' "$output"
