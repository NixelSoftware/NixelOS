@echo off
setlocal EnableExtensions

if /i "%~1"=="run" goto runvm

set "CC=gcc -O2 -m32 -ffreestanding -fno-leading-underscore -fno-pie -mno-stack-arg-probe"

call :run nasm -f bin src\boot\boot.asm -o src\boot.bin
call :run nasm -f bin src\boot\boot_stage2.asm -o src\boot_stage2.bin
call :run nasm -f elf32 src\kernel\kernel_entry.asm -o src\kernel\kernel_entry.o
call :run nasm -f elf32 src\kernel\idt_asm.asm -o src\kernel\idt_asm.o
call :run nasm -f elf32 src\kernel\task_switch.asm -o src\kernel\task_switch.o

for %%S in (
    src\kernel\main.c
    src\system\shell\commands.c
    src\system\shell\network_commands.c
    src\system\init\main.c
    src\system\graphics\wm\wm.c
    src\system\graphics\wm\presentation.c
    src\system\graphics\widgets.c
    src\system\graphics\session.c
    src\system\services\input\input.c
    src\system\loader\app_loader.c
    src\kernel\idt.c
    src\system\init\splash\startscreen.c
    src\system\init\splash\logo_data.c
    src\system\graphics\screenshot\screenshot.c
    src\system\graphics\image\png.c
    src\kernel\drivers\display.c
    src\kernel\drivers\ata.c
    src\kernel\drivers\net.c
    src\kernel\drivers\net_e1000.c
    src\kernel\drivers\serial.c
    src\kernel\drivers\floppy.c
    src\kernel\drivers\power.c
    src\kernel\drivers\ps2.c
    src\kernel\kernel_net.c
    src\kernel\net\ipv4.c
    src\kernel\net\etherframe.c
    src\kernel\net\arp.c
    src\kernel\net\tcp.c
    src\kernel\net\udp.c
    src\kernel\net\dns.c
    src\kernel\net\icmp.c
    src\kernel\net\dhcp.c
    src\kernel\fs.c
    src\system\graphics\wm\cursor.c
    src\kernel\drivers\display\font.c
    src\kernel\keyboard.c
    src\system\graphics\wm\desktop.c
    src\kernel\task.c
    src\system\graphics\dialog\dialog.c
    src\apps\notepad\notepad.c
    src\apps\viewer\viewer.c
    src\system\graphics\terminal\terminal.c
    src\apps\ping\main.c
    src\apps\netdiag\main.c
) do (
    call :run %CC% -c %%S -o %%~dpnS.o
)

for %%A in (files mines calc hexview floppy_debug ping netdiag http_test crash_debug shutdown curl) do (
    if not exist src\apps\%%A mkdir src\apps\%%A
    call :run nasm -f elf32 src\apps\%%A\entry.asm -o src\apps\%%A\entry.o
    call :run %CC% -c src\apps\%%A\main.c -o src\apps\%%A\main.o
    call :run ld -mi386pe -Ttext 0x200000 -e _start src\apps\%%A\entry.o src\apps\%%A\main.o -o src\apps\%%A\%%A.tmp
    call :run objcopy -O binary src\apps\%%A\%%A.tmp %%A.bin
    del /q src\apps\%%A\%%A.tmp src\apps\%%A\main.o src\apps\%%A\entry.o
)

if not exist src\apps\browser mkdir src\apps\browser
call :run nasm -f elf32 src\apps\browser\entry.asm -o src\apps\browser\entry.o
call :run %CC% -c src\apps\browser\main.c -o src\apps\browser\main.o
call :run %CC% -c src\apps\browser\png.c -o src\apps\browser\png.o
::call :run ld -mi386pe -Ttext 0x200000 -e _start src\apps\browser\entry.o src\apps\browser\main.o src\apps\browser\png.o -o src\apps\browser\browser.tmp
call :run ld -mi386pe --omagic -Ttext 0x200000 -e _start src\apps\browser\entry.o src\apps\browser\main.o src\apps\browser\png.o -o src\apps\browser\browser.tmp
call :run objcopy -O binary src\apps\browser\browser.tmp browser.bin
del /q src\apps\browser\browser.tmp src\apps\browser\main.o src\apps\browser\entry.o src\apps\browser\png.o

call :run ld -mi386pe -Ttext 0x10000 ^
    src\kernel\kernel_entry.o src\kernel\idt_asm.o src\kernel\task_switch.o ^
    src\kernel\main.o src\kernel\idt.o src\kernel\task.o ^
    src\system\shell\commands.o ^
    src\system\shell\network_commands.o ^
    src\system\init\main.o ^
    src\system\graphics\wm\wm.o ^
    src\system\graphics\wm\presentation.o src\system\graphics\widgets.o ^
    src\system\graphics\session.o ^
    src\system\services\input\input.o ^
    src\system\loader\app_loader.o ^
    src\kernel\drivers\display.o src\kernel\drivers\ata.o src\kernel\drivers\net.o ^
    src\kernel\net\ipv4.o src\kernel\net\etherframe.o src\kernel\net\arp.o ^
    src\kernel\net\tcp.o src\kernel\net\udp.o src\kernel\net\icmp.o src\kernel\net\dhcp.o ^
    src\kernel\fs.o src\system\graphics\wm\cursor.o src\kernel\drivers\display\font.o src\kernel\keyboard.o ^
    src\system\graphics\wm\desktop.o src\system\graphics\dialog\dialog.o ^
    src\apps\notepad\notepad.o src\apps\viewer\viewer.o ^
    src\system\graphics\terminal\terminal.o ^
    src\kernel\drivers\serial.o src\kernel\drivers\floppy.o src\kernel\kernel_net.o src\kernel\drivers\net_e1000.o ^
    src\system\init\splash\startscreen.o src\system\graphics\screenshot\screenshot.o src\kernel\drivers\power.o src\kernel\drivers\ps2.o ^
    src\system\graphics\image\png.o src\kernel\net\dns.o src\system\init\splash\logo_data.o ^
    -o src\kernel\kernel.tmp
call :run objcopy -O binary src\kernel\kernel.tmp src\kernel\kernel.bin
del /q src\kernel\kernel.tmp

if not exist "src\boot.bin" ( echo [ERROR] Missing src\boot.bin & exit /b 1 )
if not exist "src\boot_stage2.bin" ( echo [ERROR] Missing src\boot_stage2.bin & exit /b 1 )
if not exist "src\kernel\kernel.bin" ( echo [ERROR] Missing src\kernel\kernel.bin & exit /b 1 )

powershell -NoProfile -Command "$p='src\kernel\kernel.bin';$b=[IO.File]::ReadAllBytes($p);if($b.Length -lt 98304){[IO.File]::WriteAllBytes($p,$b+(New-Object byte[](98304-$b.Length)))}"
::copy /b src\boot.bin+src\boot_stage2.bin+src\kernel\kernel.bin ..\hdd.img >nul
copy /b "src\boot.bin" + "src\boot_stage2.bin" + "src\kernel\kernel.bin" "..\hdd.img" 1>nul
powershell -NoProfile -Command "$p='..\hdd.img';$f=[IO.File]::Open($p,'Open','ReadWrite');$f.SetLength(33554432);$f.Close()"
::call :run powershell -NoProfile -ExecutionPolicy Bypass -File inject_sfs.ps1
call :run python inject_sfs.py ..\hdd.img
copy /y ..\hdd.img os.img >nul

del /q src\boot.bin src\boot_stage2.bin src\kernel\*.o src\kernel\drivers\*.o src\kernel\drivers\display\*.o src\kernel\net\*.o src\system\shell\*.o src\system\init\*.o src\system\init\splash\*.o src\system\graphics\*.o src\system\graphics\wm\*.o src\system\graphics\dialog\*.o src\system\graphics\screenshot\*.o src\system\graphics\image\*.o src\system\graphics\session.o src\system\services\input\*.o src\system\loader\*.o
del /q src\apps\notepad\*.o src\apps\viewer\*.o src\system\graphics\terminal\*.o src\apps\ping\*.o src\apps\netdiag\*.o src\kernel\kernel.bin
del /q files.bin mines.bin calc.bin hexview.bin floppy_debug.bin ping.bin netdiag.bin http_test.bin browser.bin crash_debug.bin shutdown.bin curl.bin

goto runvm

:runvm
start qemu-system-i386 -m 32M -drive file=os.img,format=raw,index=0,media=disk ^
    -netdev user,id=net0,net=10.0.1.0/24,dhcpstart=10.0.1.227 ^
    -device e1000,netdev=net0 -serial stdio
exit /b 0

:run
%*
if errorlevel 1 exit /b %errorlevel%
exit /b 0
