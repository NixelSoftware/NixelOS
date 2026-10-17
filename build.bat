@echo off
setlocal


nasm -f bin src\boot.asm -o src\boot.bin
if %errorlevel% neq 0 exit /b %errorlevel%

nasm -f bin src\boot_stage2.asm -o src\boot_stage2.bin
if %errorlevel% neq 0 exit /b %errorlevel%


nasm -f elf32 src\kernel\kernel_entry.asm -o src\kernel\kernel_entry.o
if %errorlevel% neq 0 exit /b %errorlevel%

nasm -f elf32 src\kernel\idt_asm.asm -o src\kernel\idt_asm.o
if %errorlevel% neq 0 exit /b %errorlevel%

nasm -f elf32 src\kernel\task_switch.asm -o src\kernel\task_switch.o
if %errorlevel% neq 0 exit /b %errorlevel%


gcc -O2 -m32 -ffreestanding -fno-leading-underscore -fno-pie -c src\kernel\main.c -o src\kernel\main.o
if %errorlevel% neq 0 exit /b %errorlevel%

gcc -O2 -m32 -ffreestanding -fno-leading-underscore -fno-pie -c src\kernel\idt.c -o src\kernel\idt.o
if %errorlevel% neq 0 exit /b %errorlevel%

gcc -O2 -m32 -ffreestanding -fno-leading-underscore -fno-pie -c src\kernel\drivers\display.c -o src\kernel\drivers\display.o
if %errorlevel% neq 0 exit /b %errorlevel%

gcc -O2 -m32 -ffreestanding -fno-leading-underscore -fno-pie -c src\kernel\drivers\ata.c -o src\kernel\drivers\ata.o
if %errorlevel% neq 0 exit /b %errorlevel%

gcc -O2 -m32 -ffreestanding -fno-leading-underscore -fno-pie -c src\kernel\fs.c -o src\kernel\fs.o
if %errorlevel% neq 0 exit /b %errorlevel%

gcc -O2 -m32 -ffreestanding -fno-leading-underscore -fno-pie -c src\kernel\cursor.c -o src\kernel\cursor.o
if %errorlevel% neq 0 exit /b %errorlevel%

gcc -O2 -m32 -ffreestanding -fno-leading-underscore -fno-pie -c src\kernel\font.c -o src\kernel\font.o
if %errorlevel% neq 0 exit /b %errorlevel%

gcc -O2 -m32 -ffreestanding -fno-leading-underscore -fno-pie -c src\kernel\keyboard.c -o src\kernel\keyboard.o
if %errorlevel% neq 0 exit /b %errorlevel%

gcc -O2 -m32 -ffreestanding -fno-leading-underscore -fno-pie -c src\kernel\desktop.c -o src\kernel\desktop.o
if %errorlevel% neq 0 exit /b %errorlevel%

gcc -O2 -m32 -ffreestanding -fno-leading-underscore -fno-pie -c src\kernel\task.c -o src\kernel\task.o
if %errorlevel% neq 0 exit /b %errorlevel%


gcc -O2 -m32 -ffreestanding -fno-leading-underscore -fno-pie -c src\apps\notepad\notepad.c -o src\apps\notepad\notepad.o
if %errorlevel% neq 0 exit /b %errorlevel%

gcc -O2 -m32 -ffreestanding -fno-leading-underscore -fno-pie -c src\apps\viewer\viewer.c -o src\apps\viewer\viewer.o
if %errorlevel% neq 0 exit /b %errorlevel%


if not exist src\apps\files mkdir src\apps\files
nasm -f elf32 src\apps\files\entry.asm -o src\apps\files\entry.o
if %errorlevel% neq 0 exit /b %errorlevel%

gcc -O2 -m32 -ffreestanding -fno-leading-underscore -fno-pie -c src\apps\files\main.c -o src\apps\files\main.o
if %errorlevel% neq 0 exit /b %errorlevel%

ld -mi386pe -Ttext 0x200000 -e _start src\apps\files\entry.o src\apps\files\main.o -o src\apps\files\files.tmp
if %errorlevel% neq 0 exit /b %errorlevel%

objcopy -O binary src\apps\files\files.tmp files.bin
if %errorlevel% neq 0 exit /b %errorlevel%
del src\apps\files\files.tmp
del src\apps\files\main.o
del src\apps\files\entry.o


ld -mi386pe -Ttext 0x10000 src\kernel\kernel_entry.o src\kernel\idt_asm.o src\kernel\task_switch.o src\kernel\main.o src\kernel\idt.o src\kernel\task.o src\kernel\drivers\display.o src\kernel\drivers\ata.o src\kernel\fs.o src\kernel\cursor.o src\kernel\font.o src\kernel\keyboard.o src\kernel\desktop.o src\apps\notepad\notepad.o src\apps\viewer\viewer.o -o src\kernel\kernel.tmp
if %errorlevel% neq 0 exit /b %errorlevel%


objcopy -O binary src\kernel\kernel.tmp src\kernel\kernel.bin
if %errorlevel% neq 0 exit /b %errorlevel%
del src\kernel\kernel.tmp


powershell -Command "$b = [System.IO.File]::ReadAllBytes('src\kernel\kernel.bin'); if ($b.Length -lt 65536) { $pad = New-Object byte[] (65536 - $b.Length); [System.IO.File]::WriteAllBytes('src\kernel\kernel.bin', $b + $pad) }"


copy /b src\boot.bin+src\boot_stage2.bin+src\kernel\kernel.bin ..\hdd.img > nul
powershell -Command "$target = '..\hdd.img'; $fs = [System.IO.File]::Open($target, [System.IO.FileMode]::Open, [System.IO.FileAccess]::ReadWrite); $fs.SetLength(33554432); $fs.Close()"


powershell -ExecutionPolicy Bypass -Command "$bytes = [System.IO.File]::ReadAllBytes('files.bin'); $fs = [System.IO.File]::Open('..\hdd.img', [System.IO.FileMode]::Open, [System.IO.FileAccess]::ReadWrite); $fs.Seek(200 * 512, [System.IO.SeekOrigin]::Begin); $w = New-Object System.IO.BinaryWriter($fs); $w.Write([uint32]0x53465331); $w.Write([uint32]1); $nameBytes = [System.Text.Encoding]::ASCII.GetBytes('files.bin'); $pad = New-Object byte[](16 - $nameBytes.Length); $w.Write($nameBytes); $w.Write($pad); $w.Write([uint32]$bytes.Length); $w.Write([uint32]208); $w.Write([byte]1); $fs.Seek(208 * 512, [System.IO.SeekOrigin]::Begin); $w.Write($bytes); $w.Close(); $fs.Close()"


copy /y ..\hdd.img os.img > nul


del src\boot.bin
del src\boot_stage2.bin
del src\kernel\*.o
del src\kernel\drivers\*.o
del src\apps\notepad\*.o
del src\apps\viewer\*.o
del src\kernel\kernel.bin
del files.bin


qemu-system-i386 -m 32M -drive file=os.img,format=raw,index=0,media=disk