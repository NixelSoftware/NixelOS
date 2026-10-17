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
gcc -O2 -m32 -ffreestanding -fno-leading-underscore -fno-pie -c src/kernel/drivers/net.c -o src/kernel/drivers/net.o
gcc -O2 -m32 -ffreestanding -fno-leading-underscore -fno-pie -c src/kernel/net/ipv4.c -o src/kernel/net/ipv4.o
gcc -O2 -m32 -ffreestanding -fno-leading-underscore -fno-pie -c src/kernel/net/etherframe.c -o src/kernel/net/etherframe.o
gcc -O2 -m32 -ffreestanding -fno-leading-underscore -fno-pie -c src/kernel/net/arp.c -o src/kernel/net/arp.o
gcc -O2 -m32 -ffreestanding -fno-leading-underscore -fno-pie -c src/kernel/net/tcp.c -o src/kernel/net/tcp.o
gcc -O2 -m32 -ffreestanding -fno-leading-underscore -fno-pie -c src/kernel/net/udp.c -o src/kernel/net/udp.o
gcc -O2 -m32 -ffreestanding -fno-leading-underscore -fno-pie -c src/kernel/net/icmp.c -o src/kernel/net/icmp.o
gcc -O2 -m32 -ffreestanding -fno-leading-underscore -fno-pie -c src/kernel/net/dhcp.c -o src/kernel/net/dhcp.o
gcc -O2 -m32 -ffreestanding -fno-leading-underscore -fno-pie -c src/kernel/fs.c -o src/kernel/fs.o
gcc -O2 -m32 -ffreestanding -fno-leading-underscore -fno-pie -c src/kernel/cursor.c -o src/kernel/cursor.o
gcc -O2 -m32 -ffreestanding -fno-leading-underscore -fno-pie -c src/kernel/font.c -o src/kernel/font.o
gcc -O2 -m32 -ffreestanding -fno-leading-underscore -fno-pie -c src/kernel/keyboard.c -o src/kernel/keyboard.o
gcc -O2 -m32 -ffreestanding -fno-leading-underscore -fno-pie -c src/kernel/desktop.c -o src/kernel/desktop.o
gcc -O2 -m32 -ffreestanding -fno-leading-underscore -fno-pie -c src/kernel/task.c -o src/kernel/task.o
gcc -O2 -m32 -ffreestanding -fno-leading-underscore -fno-pie -c src/kernel/dialog.c -o src/kernel/dialog.o

gcc -O2 -m32 -ffreestanding -fno-leading-underscore -fno-pie -c src/apps/notepad/notepad.c -o src/apps/notepad/notepad.o
gcc -O2 -m32 -ffreestanding -fno-leading-underscore -fno-pie -c src/apps/viewer/viewer.c -o src/apps/viewer/viewer.o

mkdir -p src/apps/files
mkdir -p src/apps/mines

nasm -f elf32 src/apps/files/entry.asm -o src/apps/files/entry.o
gcc -O2 -m32 -ffreestanding -fno-leading-underscore -fno-pie -c src/apps/files/main.c -o src/apps/files/main.o

ld -m elf_i386 -Ttext 0x200000 -e _start \
    src/apps/files/entry.o \
    src/apps/files/main.o \
    -o src/apps/files/files.tmp

objcopy -O binary src/apps/files/files.tmp files.bin

rm -f src/apps/files/files.tmp
rm -f src/apps/files/main.o src/apps/files/entry.o

nasm -f elf32 src/apps/mines/entry.asm -o src/apps/mines/entry.o
gcc -O2 -m32 -ffreestanding -fno-leading-underscore -fno-pie -c src/apps/mines/main.c -o src/apps/mines/main.o

ld -m elf_i386 -Ttext 0x200000 -e _start \
    src/apps/mines/entry.o \
    src/apps/mines/main.o \
    -o src/apps/mines/mines.tmp

objcopy -O binary src/apps/mines/mines.tmp mines.bin

rm -f src/apps/mines/mines.tmp
rm -f src/apps/mines/main.o src/apps/mines/entry.o

ld -m elf_i386 -Ttext 0x10000 \
    src/kernel/kernel_entry.o \
    src/kernel/idt_asm.o \
    src/kernel/task_switch.o \
    src/kernel/main.o \
    src/kernel/idt.o \
    src/kernel/task.o \
    src/kernel/dialog.o \
    src/kernel/drivers/display.o \
    src/kernel/drivers/ata.o \
    src/kernel/drivers/net.o \
    src/kernel/net/ipv4.o \
    src/kernel/net/etherframe.o \
    src/kernel/net/arp.o \
    src/kernel/net/tcp.o \
    src/kernel/net/udp.o \
    src/kernel/net/icmp.o \
    src/kernel/net/dhcp.o \
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

./inject_sfs.sh

cp -f ../hdd.img os.img

rm -f src/boot.bin
rm -f src/boot_stage2.bin
rm -f src/kernel/*.o
rm -f src/kernel/drivers/*.o
rm -f src/kernel/net/*.o
rm -f src/kernel/dialog.o
rm -f src/apps/notepad/*.o
rm -f src/apps/viewer/*.o
rm -f src/kernel/kernel.bin
rm -f files.bin
rm -f mines.bin

qemu-system-i386 -m 32M \
    -drive file=os.img,format=raw,index=0,media=disk \
    -netdev bridge,id=net0,br=br0 \
    -device pcnet,netdev=net0