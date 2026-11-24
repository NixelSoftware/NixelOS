#!/usr/bin/env python3
"""Run production entropy and allocator at ring 0 on a CPU without RDRAND."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix="nixel-entropy-qemu-") as tmp:
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
    subprocess.run([os.environ.get("CC", "cc"), "-DJENT_NIXEL", "-Isrc/kernel/crypto", "-Isrc/kernel/crypto/vendor/jitterentropy", "-m32", "-std=c99", "-O2", "-Wall", "-Wextra", "-Werror", "-ffreestanding", "-fno-pie", "-fno-stack-protector", "-c", str(ROOT / "src/kernel/crypto/tests/entropy_qemu_test.c"), "-o", str(p / "test.o")], check=True)
    sources = [ROOT / "src/kernel/crypto/entropy-platform.c", ROOT / "src/kernel/memory.c", ROOT / "src/system/libraries/tls/div64.c", *sorted((ROOT / "src/kernel/crypto/vendor/jitterentropy/src").glob("*.c"))]
    objects = []
    for i, source in enumerate(sources):
        obj = str(p / f"extra{i}.o")
        subprocess.run([os.environ.get("CC", "cc"), "-m32", "-O0", "-fwrapv", "-ffreestanding", "-fno-pie", "-fno-stack-protector", "-mno-sse", "-mno-sse2", "-DJENT_NIXEL", "-Isrc/kernel/crypto", "-Isrc/kernel/crypto/vendor/jitterentropy", "-Isrc/include", "-c", str(source), "-o", obj], check=True, cwd=ROOT)
        objects.append(obj)
    subprocess.run([os.environ.get("LD", "ld"), "-m", "elf_i386", "-T", str(p / "kernel.ld"), str(p / "boot.o"), str(p / "test.o"), *objects, "-o", str(p / "kernel.elf")], check=True)
    result = subprocess.run([os.environ.get("QEMU", "qemu-system-i386"), "-kernel", str(p / "kernel.elf"), "-m", "64M", "-cpu", "pentium3", "-display", "none", "-serial", "none", "-monitor", "none", "-no-reboot", "-device", "isa-debug-exit,iobase=0xf4,iosize=0x04"], timeout=30)
    if result.returncode != 33:
        raise SystemExit(f"No-RDRAND entropy test failed: QEMU exit {result.returncode}")
    print("Entropy: production jitter collector, paging allocator and two nonces passed on Pentium III without RDRAND")
