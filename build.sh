#!/usr/bin/env bash
set -euo pipefail

cc="gcc -O2 -m32 -ffreestanding -fno-leading-underscore -fno-pie"

nasm -f bin src/boot.asm -o src/boot.bin
nasm -f bin src/boot_stage2.asm -o src/boot_stage2.bin

nasm -f elf32 src/kernel/kernel_entry.asm -o src/kernel/kernel_entry.o
nasm -f elf32 src/kernel/idt_asm.asm -o src/kernel/idt_asm.o
nasm -f elf32 src/kernel/task_switch.asm -o src/kernel/task_switch.o

${cc} -c src/kernel/main.c -o src/kernel/main.o
${cc} -c src/kernel/idt.c -o src/kernel/idt.o
${cc} -c src/kernel/drivers/display.c -o src/kernel/drivers/display.o
${cc} -c src/kernel/drivers/ata.c -o src/kernel/drivers/ata.o
for source in src/kernel/drivers/net.c src/kernel/net/ipv4.c src/kernel/net/etherframe.c \
              src/kernel/net/arp.c src/kernel/net/tcp.c src/kernel/net/udp.c \
              src/kernel/net/icmp.c src/kernel/net/dhcp.c src/kernel/fs.c \
              src/kernel/cursor.c src/kernel/font.c src/kernel/keyboard.c \
              src/kernel/desktop.c src/kernel/task.c src/kernel/dialog.c \
              src/kernel/drivers/serial.c; do
    ${cc} -c "$source" -o "${source%.c}.o"
done

mkdir -p src/apps/files

${cc} -c src/apps/notepad/notepad.c -o src/apps/notepad/notepad.o
${cc} -c src/apps/viewer/viewer.c -o src/apps/viewer/viewer.o

nasm -f elf32 src/apps/files/entry.asm -o src/apps/files/entry.o
${cc} -c src/apps/files/main.c -o src/apps/files/main.o
ld -m elf_i386 -Ttext 0x200000 -e _start \
    src/apps/files/entry.o \
    src/apps/files/main.o \
    -o src/apps/files/files.tmp
objcopy -O binary src/apps/files/files.tmp files.bin
rm -f src/apps/files/files.tmp src/apps/files/main.o src/apps/files/entry.o

for app in mines calc hexview; do
    mkdir -p "src/apps/$app"
    nasm -f elf32 "src/apps/$app/entry.asm" -o "src/apps/$app/entry.o"
    ${cc} -c "src/apps/$app/main.c" -o "src/apps/$app/main.o"
    ld -m elf_i386 -Ttext 0x200000 -e _start \
        "src/apps/$app/entry.o" "src/apps/$app/main.o" \
        -o "src/apps/$app/$app.tmp"
    objcopy -O binary "src/apps/$app/$app.tmp" "$app.bin"
    rm -f "src/apps/$app/$app.tmp" "src/apps/$app/main.o" "src/apps/$app/entry.o"
done

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
    src/kernel/drivers/serial.o \
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
rm -f calc.bin
rm -f hexview.bin

qemu-system-i386 -m 32M \
    -drive file=os.img,format=raw,index=0,media=disk \
    -netdev user,id=net0,net=10.0.0.0/24,dhcpstart=10.0.0.227 \
    -device pcnet,netdev=net0 \
    -serial stdio \