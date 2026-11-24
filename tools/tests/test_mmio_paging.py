#!/usr/bin/env python3
"""Run the real paging/MMIO implementation in a small multiboot QEMU kernel."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix="nixel-mmio-") as tmp:
    p = Path(tmp)
    (p / "boot.asm").write_text('''BITS 32
section .multiboot
align 4
dd 0x1badb002, 0, -(0x1badb002)
section .text
global _start
extern test_main
_start:
cli
mov esp, stack_top
call test_main
.halt: hlt
jmp .halt
section .bss
align 16
resb 16384
stack_top:
section .note.GNU-stack noalloc noexec nowrite progbits
''')
    (p / "kernel.ld").write_text('''ENTRY(_start)
SECTIONS {
 . = 0x100000;
 .multiboot : { *(.multiboot) }
 .text : { *(.text*) }
 .rodata : { *(.rodata*) }
 .data : { *(.data*) }
 .bss : { *(COMMON) *(.bss*) }
}
''')
    subprocess.run([os.environ.get("NASM", "nasm"), "-f", "elf32", str(p / "boot.asm"), "-o", str(p / "boot.o")], check=True)
    subprocess.run([os.environ.get("CC", "cc"), "-m32", "-std=c99", "-O2", "-Wall", "-Wextra", "-Werror", "-ffreestanding", "-fno-pie", "-fno-stack-protector", "-c", str(ROOT / "src/kernel/tests/mmio_paging_test.c"), "-o", str(p / "test.o")], check=True)
    subprocess.run([os.environ.get("LD", "ld"), "-m", "elf_i386", "-T", str(p / "kernel.ld"), str(p / "boot.o"), str(p / "test.o"), "-o", str(p / "kernel.elf")], check=True)
    result = subprocess.run([os.environ.get("QEMU", "qemu-system-i386"), "-kernel", str(p / "kernel.elf"), "-m", "64M", "-display", "none", "-serial", "none", "-monitor", "none", "-no-reboot", "-device", "isa-debug-exit,iobase=0xf4,iosize=0x04"], timeout=20)
    if result.returncode != 33:
        raise SystemExit(f"MMIO paging test failed: QEMU exit {result.returncode}")
    print("MMIO paging: uncached page, RAM rejection, neighboring mappings, existing/new address spaces and table lifetime passed")
