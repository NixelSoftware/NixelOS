#!/usr/bin/env python3
"""Build an isolated test kernel and exercise production switching in QEMU.

Requires gcc (i386 target), nasm, ld and qemu-system-i386. No OS image is changed.
"""
from pathlib import Path
import subprocess
import tempfile


def main():
    root = Path(__file__).resolve().parents[2]
    here = Path(__file__).resolve().parent
    with tempfile.TemporaryDirectory(prefix="nixel-architecture-") as directory:
        build = Path(directory)
        objects = []
        for source in (here / "boot.asm", here / "user.asm",
                       root / "src/kernel/gdt_asm.asm", root / "src/kernel/idt_asm.asm",
                       root / "src/kernel/arch_i386.c", root / "src/kernel/task_switch.asm",
                       here / "test.c", root / "src/kernel/task.c", root / "src/kernel/arch_fpu.c",
                       root / "src/kernel/scheduler.c", root / "src/kernel/sched_policy.c",
                       root / "src/kernel/sched_wide.c", root / "src/kernel/mutex.c"):
            obj = build / (source.stem + ".o")
            if source.suffix == ".asm":
                command = ["nasm", "-f", "elf32", "-DSTARTUP_TRACE=1", str(source)]
            else:
                command = ["gcc", "-O2", "-m32", "-ffreestanding", "-fno-pie",
                           "-fno-stack-protector", "-mno-sse", "-mno-sse2", "-mno-mmx", "-DSTARTUP_TRACE=1",
                           "-I" + str(root / "src/kernel"), "-c", str(source)]
            subprocess.run(command + ["-o", str(obj)], check=True)
            objects.append(str(obj))
        kernel = build / "test.elf"
        subprocess.run(["ld", "-m", "elf_i386", "-T", str(here / "link.ld"),
                        *objects, "-o", str(kernel)], check=True)
        # TSC accounting and the PIT-only clock fallback use the same IRQ path.
        for cpu in ("qemu32", "qemu32,-tsc", "qemu32,-fxsr,-sse,-sse2",
                    "qemu32,-sse,-sse2", "pentium2", "pentium3", "max", "qemu32,-fpu"):
            command = ["qemu-system-i386", "-accel", "tcg", "-smp", "1",
                       "-m", "32M", "-cpu", cpu, "-kernel", str(kernel),
                       "-display", "none", "-serial", "none", "-monitor", "none",
                       "-no-reboot", "-debugcon", "stdio",
                       "-d", "int", "-D", str(build / "interrupts.log"),
                       "-device", "isa-debug-exit,iobase=0xf4,iosize=0x04"]
            try:
                result = subprocess.run(command, capture_output=True, text=True, timeout=30)
            except subprocess.TimeoutExpired as error:
                print((error.stdout or b"").decode(errors="replace"))
                print((build / "interrupts.log").read_text()[-6000:])
                raise SystemExit(f"QEMU timed out: {cpu}") from error
            print(f"CPU {cpu}:\n{result.stdout}", end="", flush=True)

            if "-fpu" in cpu:
                if result.returncode != 1 or "PASS hardware FPU absence rejected\n" not in result.stdout:
                    raise SystemExit("FPU absence was not rejected: " + result.stderr)
                continue
            expected = "State: x87\n" if "-sse" in cpu or cpu == "pentium2" else "State: FXSAVE\n"
            simd = "SIMD: disabled\n" if "-sse" in cpu or cpu == "pentium2" else "SIMD: SSE\n"
            if (expected not in result.stdout or simd not in result.stdout or result.returncode != 1 or "PASS architecture boundaries\n" not in result.stdout):
                raise SystemExit(f"QEMU failed ({result.returncode}): {result.stderr}")


if __name__ == "__main__":
    main()
