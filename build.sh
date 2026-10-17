#!/usr/bin/env bash
set -e

nasm -f bin src/boot.asm -o src/boot.bin
nasm -f bin src/boot_stage2.asm -o src/boot_stage2.bin

nasm -f elf32 src/kernel/kernel_entry.asm -o src/kernel/kernel_entry.o
nasm -f elf32 src/kernel/idt_asm.asm -o src/kernel/idt_asm.o
nasm -f elf32 src/kernel/task_switch.asm -o src/kernel/task_switch.o

gcc -O2 -m32 -ffreestanding -fno-leading-underscore -fno-pie -c src/kernel/main.c -o src/kernel/main.o
gcc -O2 -m32 -ffreestanding -fno-leading-underscore -fno-pie -c src/kernel/idt.c -o src/kernel/idt.o
gcc -O2 -m32 -ffreestanding -fno-leading-underscore -fno-pie -c src/kernel/drivers/display.c -o src/kernel/drivers/display.o
gcc -O2 -m32 -ffreestanding -fno-leading-underscore -fno-pie -c src/kernel/drivers/ata.c -o src/kernel/drivers/ata.o
gcc -O2 -m32 -ffreestanding -fno-leading-underscore -fno-pie -c src/kernel/fs.c -o src/kernel/fs.o
gcc -O2 -m32 -ffreestanding -fno-leading-underscore -fno-pie -c src/kernel/cursor.c -o src/kernel/cursor.o
gcc -O2 -m32 -ffreestanding -fno-leading-underscore -fno-pie -c src/kernel/font.c -o src/kernel/font.o
gcc -O2 -m32 -ffreestanding -fno-leading-underscore -fno-pie -c src/kernel/keyboard.c -o src/kernel/keyboard.o
gcc -O2 -m32 -ffreestanding -fno-leading-underscore -fno-pie -c src/kernel/desktop.c -o src/kernel/desktop.o
gcc -O2 -m32 -ffreestanding -fno-leading-underscore -fno-pie -c src/kernel/task.c -o src/kernel/task.o

gcc -O2 -m32 -ffreestanding -fno-leading-underscore -fno-pie -c src/apps/notepad/notepad.c -o src/apps/notepad/notepad.o
gcc -O2 -m32 -ffreestanding -fno-leading-underscore -fno-pie -c src/apps/viewer/viewer.c -o src/apps/viewer/viewer.o

mkdir -p src/apps/files

nasm -f elf32 src/apps/files/entry.asm -o src/apps/files/entry.o
gcc -O2 -m32 -ffreestanding -fno-leading-underscore -fno-pie -c src/apps/files/main.c -o src/apps/files/main.o

ld -m elf_i386 -Ttext 0x200000 -e _start \
    src/apps/files/entry.o \
    src/apps/files/main.o \
    -o src/apps/files/files.tmp

objcopy -O binary src/apps/files/files.tmp files.bin

rm -f src/apps/files/files.tmp
rm -f src/apps/files/main.o src/apps/files/entry.o

ld -m elf_i386 -Ttext 0x10000 \
    src/kernel/kernel_entry.o \
    src/kernel/idt_asm.o \
    src/kernel/task_switch.o \
    src/kernel/main.o \
    src/kernel/idt.o \
    src/kernel/task.o \
    src/kernel/drivers/display.o \
    src/kernel/drivers/ata.o \
    src/kernel/fs.o \
    src/kernel/cursor.o \
    src/kernel/font.o \
    src/kernel/keyboard.o \
    src/kernel/desktop.o \
    src/apps/notepad/notepad.o \
    src/apps/viewer/viewer.o \
    -o src/kernel/kernel.tmp

objcopy -O binary src/kernel/kernel.tmp src/kernel/kernel.bin

rm -f src/kernel/kernel.tmp

python3 - <<'PY'
from pathlib import Path

path = Path("src/kernel/kernel.bin")
data = path.read_bytes()

if len(data) < 65536:
    data += bytes(65536 - len(data))
    path.write_bytes(data)
PY

cat src/boot.bin src/boot_stage2.bin src/kernel/kernel.bin > ../hdd.img

truncate -s 33554432 ../hdd.img

python3 - <<'PY'
from pathlib import Path
import struct

source = Path("files.bin").read_bytes()
image_path = Path("../hdd.img")

with image_path.open("r+b") as f:
    # Filesystem header at sector 200 (offset 102400).
    f.seek(200 * 512)

    name = b"files.bin"
    name = name.ljust(16, b"\0")

    f.write(struct.pack(
        "<II16sIIB",
        0x53465331,
        1,
        name,
        len(source),
        208,
        1
    ))

    # File contents at sector 208 (offset 106496).
    f.seek(208 * 512)
    f.write(source)
PY

cp -f ../hdd.img os.img

rm -f src/boot.bin
rm -f src/boot_stage2.bin
rm -f src/kernel/*.o
rm -f src/kernel/drivers/*.o
rm -f src/apps/notepad/*.o
rm -f src/apps/viewer/*.o
rm -f src/kernel/kernel.bin
rm -f files.bin

qemu-system-i386 -m 32M -drive file=os.img,format=raw,index=0,media=disk