# i386 architecture boundary checks

Run `python3 tools/architecture-boundary/run.py` from the repository root.
Requires GCC with i386 output, NASM, GNU ld and QEMU. Builds live in a temporary
directory; the OS image and production objects are untouched.

The test links production GDT/TSS setup, ring-3 entry assembly, interrupt/syscall
entry and return assembly, task switching, EEVDF and mutex code. Three tasks use
distinct directories with shared read-only user code, private data and private
user stacks at identical virtual addresses. Kernel memory is supervisor-only.
Both TSC and PIT-only runs must pass. The extended-state matrix also covers
Pentium II (legacy), Pentium III (SSE1), FXSR/SSE-masked qemu32 models,
an AVX-capable `max` CPU, and a CPU reporting no hardware FPU.

Checks cover user timer frames, preemption in userspace and syscall work, TSS
selection of each task's kernel stack, CR3 and its software mirror, independent
same-address counters, saved registers, user DF restoration/kernel DF clearing,
IOPL zero, suspended syscall resumption, normal syscall exit, and recovery from
user page faults and invalid opcodes. Two rounds reuse the retired task slots and verify clean initial
x87 control/tag state, MXCSR and all eight XMM registers. Each user task then
keeps distinct x87/MMX payloads, x87 rounding, XMM0..7 and MXCSR values across
interrupts and suspended syscalls. Unsupported SIMD paths are skipped; CR0/CR4
gates and the active save format are checked explicitly. A VZEROUPPER attempt
must fault even on the AVX-capable model, while absence of x87 is rejected at init. The page-fault case attempts to write
supervisor kernel memory and verifies the hardware error code.

Page tables are owned by the test. Syscall and exception C handlers validate
frames and request return through the production assembly, rather than linking
the application loader, paging allocator or full syscall dispatcher. This test
therefore validates the architecture boundary, not every service or allocation
failure path. Production `arch_fpu.c` is linked for feature detection and actual
FXSAVE/FXRSTOR or FNSAVE/FRSTOR execution. No XSAVE/AVX state is enabled.
