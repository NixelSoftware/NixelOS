#!/usr/bin/env bash
set -euo pipefail

if [[ "${1:-}" == "run" ]]; then
    exec qemu-system-i386 -m 32M \
        -drive file=os.img,format=raw,index=0,media=disk \
        -netdev user,id=net0,net=10.0.1.0/24,dhcpstart=10.0.1.227 \
        -device e1000,netdev=net0 -serial stdio
fi

debug_subsystems="${DEBUG_SUBSYSTEMS:-0}"
if [[ "$debug_subsystems" != "0" && "$debug_subsystems" != "1" ]]; then
    echo "DEBUG_SUBSYSTEMS must be 0 or 1" >&2
    exit 1
fi

cc=(gcc -O2 -m32 -ffreestanding -fno-leading-underscore -fno-pie -mno-stack-arg-probe
    -DDEBUG_SUBSYSTEMS="$debug_subsystems")

nasm -f bin src/boot/boot.asm -o src/boot.bin

nasm -f elf32 src/kernel/kernel_entry.asm -o src/kernel/kernel_entry.o
nasm -f elf32 src/kernel/idt_asm.asm -o src/kernel/idt_asm.o
nasm -f elf32 src/kernel/task_switch.asm -o src/kernel/task_switch.o

kernel_sources=(
    src/kernel/main.c
    src/system/shell/commands.c
    src/system/shell/network_commands.c
    src/system/init/main.c
    src/system/graphics/wm/wm.c
    src/system/graphics/wm/presentation.c
    src/system/graphics/widgets.c
    src/system/graphics/session.c
    src/system/services/input/input.c
    src/system/loader/app_loader.c
    src/kernel/idt.c
    src/system/init/splash/startscreen.c
    src/system/init/splash/logo_data.c
    src/system/graphics/screenshot/screenshot.c
    src/system/graphics/image/png.c
    src/kernel/drivers/display.c
    src/kernel/drivers/ata.c
    src/kernel/drivers/net.c
    src/kernel/drivers/net_e1000.c
    src/kernel/drivers/net_tg3.c
    src/kernel/drivers/serial.c
    src/kernel/drivers/floppy.c
    src/kernel/drivers/power.c
    src/kernel/drivers/acpi.c
    src/kernel/drivers/ps2.c
    src/kernel/kernel_net.c
    src/kernel/net/ipv4.c
    src/kernel/net/etherframe.c
    src/kernel/net/arp.c
    src/kernel/net/tcp.c
    src/kernel/net/udp.c
    src/kernel/net/icmp.c
    src/kernel/net/dhcp.c
    src/kernel/net/dns.c
    src/kernel/fs.c
    src/system/graphics/wm/cursor.c
    src/kernel/drivers/display/font.c
    src/kernel/keyboard.c
    src/system/graphics/fonts/univers85ebo_16.c
    src/system/graphics/wm/desktop.c
    src/kernel/task.c
    src/system/graphics/dialog/dialog.c
    src/apps/notepad/notepad.c
    src/apps/viewer/viewer.c
    src/system/graphics/terminal/terminal.c
    src/apps/ping/main.c
    src/apps/netdiag/main.c
)

for source in "${kernel_sources[@]}"; do
    "${cc[@]}" -c "$source" -o "${source%.c}.o"
done

for app in files mines calc hexview floppy_debug ping netdiag http_test curl crash_debug shutdown; do
    mkdir -p "src/apps/$app"
    nasm -f elf32 "src/apps/$app/entry.asm" -o "src/apps/$app/entry.o"
    "${cc[@]}" -c "src/apps/$app/main.c" -o "src/apps/$app/main.o"
    ld -m elf_i386 -Ttext 0x200000 -e _start \
        "src/apps/$app/entry.o" "src/apps/$app/main.o" \
        -o "src/apps/$app/$app.tmp"
    objcopy -O binary "src/apps/$app/$app.tmp" "$app.bin"
    rm -f "src/apps/$app/$app.tmp" "src/apps/$app/main.o" "src/apps/$app/entry.o"
done

mkdir -p src/apps/browser
nasm -f elf32 src/apps/browser/entry.asm -o src/apps/browser/entry.o
"${cc[@]}" -c src/apps/browser/main.c -o src/apps/browser/main.o
"${cc[@]}" -c src/apps/browser/png.c -o src/apps/browser/png.o
ld -m elf_i386 --omagic -Ttext 0x200000 -e _start \
    src/apps/browser/entry.o src/apps/browser/main.o src/apps/browser/png.o \
    -o src/apps/browser/browser.tmp
objcopy -O binary src/apps/browser/browser.tmp browser.bin
rm -f src/apps/browser/browser.tmp src/apps/browser/main.o \
    src/apps/browser/entry.o src/apps/browser/png.o

ld -m elf_i386 -Ttext 0x100000 \
    src/kernel/kernel_entry.o \
    src/kernel/idt_asm.o \
    src/kernel/task_switch.o \
    src/kernel/main.o \
    src/system/shell/commands.o \
    src/system/shell/network_commands.o \
    src/system/init/main.o \
    src/system/graphics/wm/wm.o \
    src/system/graphics/wm/presentation.o \
    src/system/graphics/widgets.o \
    src/system/graphics/session.o \
    src/system/services/input/input.o \
    src/system/loader/app_loader.o \
    src/kernel/idt.o \
    src/kernel/task.o \
    src/system/graphics/dialog/dialog.o \
    src/kernel/drivers/display.o \
    src/kernel/drivers/ata.o \
    src/kernel/drivers/net.o \
    src/kernel/drivers/net_tg3.o \
    src/kernel/net/ipv4.o \
    src/kernel/net/etherframe.o \
    src/kernel/net/arp.o \
    src/kernel/net/tcp.o \
    src/kernel/net/udp.o \
    src/kernel/net/icmp.o \
    src/kernel/net/dhcp.o \
    src/kernel/net/dns.o \
    src/kernel/fs.o \
    src/system/graphics/wm/cursor.o \
    src/kernel/drivers/display/font.o \
    src/kernel/keyboard.o \
    src/system/graphics/wm/desktop.o \
    src/apps/notepad/notepad.o \
    src/apps/viewer/viewer.o \
    src/system/graphics/terminal/terminal.o \
    src/kernel/drivers/serial.o \
    src/kernel/drivers/floppy.o \
    src/kernel/kernel_net.o \
    src/kernel/drivers/net_e1000.o \
    src/system/init/splash/startscreen.o \
    src/system/graphics/screenshot/screenshot.o \
    src/system/graphics/image/png.o \
    src/kernel/drivers/power.o \
    src/kernel/drivers/acpi.o \
    src/kernel/drivers/ps2.o \
    src/system/init/splash/logo_data.o \
    src/system/graphics/fonts/univers85ebo_16.o \
    -o src/kernel/kernel.tmp

objcopy -O binary src/kernel/kernel.tmp src/kernel/kernel.bin

kernel_size_bytes=$(wc -c < src/kernel/kernel.bin)
kernel_sector_count=$(((kernel_size_bytes + 511) / 512))
max_kernel_sectors=$((500 - 5))
if (( kernel_sector_count == 0 || kernel_sector_count > max_kernel_sectors )); then
    echo "Kernel image requires ${kernel_sector_count} sectors; raw boot area supports at most ${max_kernel_sectors}." >&2
    exit 1
fi
nasm -f bin -D KERNEL_SECTORS="$kernel_sector_count" \
    -D KERNEL_SIZE_BYTES="$kernel_size_bytes" \
    src/boot/boot_stage2.asm -o src/boot_stage2.bin

rm -f src/kernel/kernel.tmp

[[ -f src/boot.bin ]]
[[ -f src/boot_stage2.bin ]]
[[ -f src/kernel/kernel.bin ]]

python3 mkfat32.py ../hdd.img

python3 - <<'PY'
from pathlib import Path

image_path = Path("../hdd.img")
boot_path = Path("src/boot.bin")
image = bytearray(image_path.read_bytes())
boot = boot_path.read_bytes()
image[0:3] = boot[0:3]
image[90:512] = boot[90:512]
image_path.write_bytes(image)
PY

cp -f ../hdd.img os.img

rm -f src/boot.bin
rm -f src/boot_stage2.bin
rm -f src/kernel/*.o
rm -f src/system/shell/*.o
rm -f src/system/init/*.o
rm -f src/system/graphics/wm/*.o
rm -f src/system/graphics/session.o
rm -f src/system/graphics/widgets.o
rm -f src/system/services/input/*.o
rm -f src/system/loader/*.o
rm -f src/kernel/drivers/*.o
rm -f src/kernel/drivers/display/*.o
rm -f src/kernel/net/*.o
rm -f src/system/graphics/dialog/*.o
rm -f src/system/graphics/screenshot/*.o
rm -f src/system/graphics/image/*.o
rm -f src/system/init/splash/*.o
rm -f src/system/graphics/fonts/*.o
rm -f src/apps/notepad/*.o
rm -f src/apps/viewer/*.o
rm -f src/system/graphics/terminal/*.o
rm -f src/apps/ping/*.o
rm -f src/apps/netdiag/*.o
rm -f src/system/graphics/fonts/*.o
rm -f src/kernel/kernel.bin
rm -f files.bin
rm -f mines.bin
rm -f calc.bin
rm -f hexview.bin
rm -f floppy_debug.bin
rm -f ping.bin
rm -f netdiag.bin
rm -f http_test.bin
rm -f curl.bin
rm -f browser.bin
rm -f crash_debug.bin
rm -f shutdown.bin

qemu-system-i386 -m 32M \
    -drive file=os.img,format=raw,index=0,media=disk \
    -netdev user,id=net0,net=10.0.1.0/24,dhcpstart=10.0.1.227 \
    -device e1000,netdev=net0 -serial stdio
