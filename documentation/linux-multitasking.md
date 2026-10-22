# Linux multitasking and the NixelOS implementation plan

This is the working plan for implementing Linux-like multi-tasking. Read the Linux model first, then use
the checklist below. Existing implementation details and historical measurements
remain in [scheduling.md](scheduling.md); those measurements are not validation
of changes made under this plan.

## How Linux does it

Linux schedules individual tasks (threads), represented by `task_struct`.
Processes and threads use the same scheduler machinery; clone flags determine
which address spaces, file tables and other resources they share. Creation
allocates a task and kernel stack, prepares architecture state, establishes
resource ownership, then makes the initialized child runnable. Fork normally
uses copy-on-write for private writable memory rather than copying it eagerly.

A runnable task may be executing or waiting for CPU service: Linux's
`TASK_RUNNING` covers both. Sleeping tasks wait for an event and are excluded
from execution selection. Exit releases resources in stages; neither a stack
still executing nor an address space still in use may be reclaimed. Parent
exit-status collection and final task destruction are separate concerns.

Each logical CPU has a run queue, a current task and an idle fallback. Scheduling
classes separate policy: stop, deadline, real-time, fair and idle form the
usual conceptual precedence, with optional sched_ext support depending on kernel
version/configuration. Nice changes weights within fair scheduling; it does not
promote a task into a real-time class. SMP additionally requires run-queue locks,
CPU affinity, migration, balancing and ordered acquisition of multiple locks.

Historical CFS orders runnable entities by weighted virtual runtime and favors
the least-served entity. Execution advances virtual runtime approximately by
`elapsed_ns * 1024 / weight`. Linux began transitioning fair task selection to
EEVDF in 6.6. EEVDF selects the earliest virtual deadline among eligible entities
(those owed service); virtual runtime alone is not its selection key. Sleeping
lag, request renewal, placement and preemption must be handled together. The
current Linux documentation describes deferred dequeue to prevent short sleeps
from erasing service debt. These details vary by release; a future port must
choose a source version before copying algorithms.

Timer interrupts account service and request rescheduling. Actual task selection
and switching happen at an allowed scheduling boundary. Interrupt masking,
preemption disabling and lock ownership are different concepts. A hard IRQ
handler must not acquire a sleeping mutex or block. Kernel preemption policy
and interrupt-return checks depend on configuration and entry path.

On x86, entry assembly and the hardware preserve interrupted state; a normal
kernel context switch preserves the suspended kernel execution chain on each
task's own stack. Switching includes the stack, architecture state and memory
context, updating the privilege-transition stack as required. CR3 need not
change between tasks sharing an address space. Extended floating-point state
requires its own supported save/restore mechanism. Linux does not copy all
process memory at a switch, nor save every register directly into task_struct.

Blocking must publish the waiter and sleeping state under synchronization with
the producer's condition update/wakeup, then recheck the condition as needed.
Otherwise an event can arrive between checking and sleeping and be lost. Wakeup
makes a task runnable; it does not guarantee immediate execution. Idle must
likewise avoid losing work between its runnable check and halt.

### Primary references

- [Linux EEVDF documentation](https://docs.kernel.org/scheduler/sched-eevdf.html)
- [Linux CFS documentation](https://docs.kernel.org/scheduler/sched-design-CFS.html)
- [Linux v6.12 scheduler core](https://github.com/torvalds/linux/blob/v6.12/kernel/sched/core.c): scheduling, wakeups and context-switch coordination.
- [Linux v6.12 task creation](https://github.com/torvalds/linux/blob/v6.12/kernel/fork.c) and [exit](https://github.com/torvalds/linux/blob/v6.12/kernel/exit.c).
- [Linux v6.12 x86 switching](https://github.com/torvalds/linux/blob/v6.12/arch/x86/include/asm/switch_to.h) and [32-bit entry](https://github.com/torvalds/linux/blob/v6.12/arch/x86/entry/entry_32.S).

## NixelOS baseline and design decisions

The existing single-CPU kernel already has a weighted CFS red-black tree,
32 static task slots, 32 KiB task stacks, nanosecond accounting, a 1 kHz PIT,
sleep deadlines, wakeup preemption, sleeping mutexes with priority inheritance,
an idle task, per-task x87 state, CR3 switching and TSS stack updates.
Keep these working while improving the boundaries. Starting again with
round-robin would discard existing functionality.

The current switch mechanism restores a complete saved interrupt-style frame.
This is a deliberate small-kernel design, not Linux's exact switch ABI.
`task_switch.asm` saves GS/FS/ES/DS, PUSHAD registers and EIP/CS/EFLAGS;
user ESP/SS follow only on a privilege transition. Exception/syscall frames
add vector/error slots and must not be cast to this timer/yield frame.
For a new kernel task, a return address and argument follow the ring-0 frame.
The saved task ESP is at byte 8 of task_t. These are ABI contracts to check
at compile time, not assumptions to leave implicit.

## Implementation checklist

Checked baseline items below were found in source; they are not claims that
new runtime testing has been performed.

### Phase 1: explicit context and scheduling boundary (first increment)

- [x] Document Linux's model, CFS versus EEVDF, and the existing NixelOS baseline.
- [x] Audit timer/yield save and restore order and kernel-task bootstrap layout.
- [x] Define the timer/yield C frame with compile-time size/offset assertions.
- [x] Construct new kernel tasks through that frame instead of anonymous pushes.
- [x] Make the timer callback account/request only; select at IRQ return after tracing.
- [x] Verify deferred preemption, generated i386 code, build and existing policy checks.

### Phase 2: lifecycle and waiting correctness

- [x] Audit duplicate enqueue/block/sleep transitions and sleeper-list membership.
- [x] Audit condition/wait publication against IRQ and task wakeups; exercise lost-wakeup boundaries.
- [x] Guard task-slot reuse and enforce the current-task exit ownership contract.
- [x] Defer collection while active or suspended live task contexts reference the directory.
- [x] Complete temporary/nested callback mapping and reclamation failure-path checks.
- [x] Validate idle transitions and nested preemption/IRQ-disabled boundaries under timer interrupts.

### Phase 3: modern fair policy

- [ ] Pin a Linux source version and document exact EEVDF eligibility/deadline/lag rules.
- [ ] Specify integer arithmetic, overflow bounds, weight changes and sleeper placement.
- [ ] Implement eligibility and virtual deadlines with a deterministic policy harness.
- [ ] Cover equal/weighted fairness, short requests, sleep debt and wakeup latency.
- [ ] Select and validate a queue representation before replacing CFS selection.
- [ ] Compare mixed CPU, input, network and audio workloads against the CFS baseline.

### Phase 4: architecture and expansion

- [ ] Audit user/kernel frame transitions, TSS/CR3 state and private-memory isolation.
- [ ] Decide supported extended CPU state and implement feature-gated save/restore.
- [ ] Specify process/thread resource ownership before introducing clone/fork semantics.
- [ ] Add real-time classes only with explicit starvation/budget behavior.
- [ ] Design per-CPU state, IRQ-safe locks, migration and balancing before enabling SMP.

## Validation log

Implementation results and exact commands belong here as work proceeds. Temporary
harnesses may live in /tmp, consistent with the existing scheduler workflow.
A successful compile is not proof of correct user/kernel context switching.

### First increment — 2026-10-22

Implemented `task_switch_frame_t` with compile-time checks for the 60-byte
ring-0 frame, PUSHAD/IRET offsets and task ESP offset. Bootstrap uses the typed
frame while preserving the existing return-address, argument and stack alignment.
`task_timer_tick()` now only updates time/accounting and requests rescheduling.
Assembly calls `task_timer_irq_exit()` after timer tracing finishes; that helper
honors preemption depth before selecting a task. Explicit yield still uses its
existing saved-frame path. Selection changes `current_task` before the assembly
restore changes ESP; this remains safe only with local IRQs disabled and no
further task-dependent work between selection and restore.

Passed:

- `make -j4 src/kernel/kernel.bin` and `make -j4 os.img`.
- `python3 /tmp/nixel-multitasking-boundary.py`: production timer callbacks with
  stubbed platform dependencies; no selection during tick, nested preemption
  deferral, inactive paths, and assembly ordering with tracing both off and on.
- `python3 /tmp/nixel-cfs-policy-test.py`: 200,000 queue mutations, equal/weighted
  shares, sleep/wakeup, idle and monotonic virtual-runtime floor.
- `python3 /tmp/nixel-cfs-mutex-test.py`: blocking, handoff, recursion,
  inherited/restored priorities and cleanup guards.
- `git diff --check`.

A headless one-CPU QEMU TCG smoke test used a temporary disk snapshot, 128 MiB,
e1000 and SB16 with the silent audio backend. Serial output reached scheduler
startup and repeated input polling. This checks boot and scheduled service
progress, not graphical interaction, user-mode isolation, audio fidelity or
mixed-workload latency. The linker
continues to report executable-stack/RWX warnings.

### Phase 2, increment 1 — task transitions and sleeper membership

Repeated `scheduler_sleep()` could insert the same task twice, forming a cycle.
`scheduler_block()` on a sleeping task changed its state without unlinking its
timed wait. A subsequent deadline tick could repeatedly process that stale
entry without making progress. A targeted host regression reproduced duplicate
membership before the fix.

Blocking now unlinks an existing timed wait before changing state. Sleeping
again replaces the previous deadline; explicitly blocking a sleeper cancels
its timeout. Exit shares that unlink path. Creation enqueue accepts only
unused/terminated slots, excluding the current and idle tasks; duplicate enqueue
cannot reset accounting or corrupt the run tree. Wake accepts only blocked or
sleeping tasks. Block/sleep/exit reject unused, terminated and idle slots.
Invalid transitions return without modifying task or queue state. These APIs
still require local IRQs disabled; the guards do not provide synchronization.

Validation:

- `python3 /tmp/nixel-phase2-transitions.py`: reproduced the failure before the
  fix, then passed repeated sleep, timeout cancellation, duplicate enqueue/wake,
  dead/unused/idle guards, slot re-enqueue, and 20,000 mixed transitions. Checks
  enforce unique sorted sleeper membership, task states, tree invariants and
  runnable/load accounting. The harness also reruns the existing 200,000 CFS
  mutations, fairness, idle and virtual-time checks.
- `python3 /tmp/nixel-cfs-mutex-test.py`: existing mutex checks passed.
- `make -j4 src/kernel/kernel.bin`: passed; existing linker warnings remain.
- `git diff --check`: passed.

This increment changes scheduler lifecycle bookkeeping only.
Wait publication/lost-wakeup boundaries, task-slot resource
ownership and deferred reclamation remain the next audit steps; guarding enqueue
does not by itself make caller-side stack reuse safe.

### Phase 2, increment 2 — wait publication and early wakeup

Audited all current scheduler block/wake call sites: mutex contention is the
only event-driven blocked wait; timer expiry wakes timed sleeps. Mutex owner
check, waiter publication, blocked-state publication and yield execute with
local IRQs disabled. On the current single CPU, an unlock cannot run between
these operations. Unlock removes the waiter and assigns ownership before
publishing runnable state, preventing a newly arriving contender from taking
that ownership. Added comments recording this synchronization contract; SMP
will require shared locking rather than local interrupt masking.

Other consumers (ATA readiness, serial, shell queue, presentation, DNS and service
loops) use timed polling, not event-driven indefinite sleeps. An event arriving
after their condition check can delay observation until a later poll but does
not remove their timer wakeup. This audit does not convert polling into wait
queues or promise a maximum scheduling latency.

A targeted regression found that `scheduler_wake(current)` after blocking but
before selection inserted current into the run tree. The next selection could
insert it a second time. Existing mutex callers exclude that interleaving with
IRQ masking, but the scheduler API now handles it correctly: waking current
cancels its wait, restores RUNNING state and counts its load without inserting
it in the tree. Normal wakeups remain READY and queued. Early timed expiry uses
the same path. This preserves the invariant that current stays outside the tree.

Validation:

- `python3 /tmp/nixel-phase2-wakeup.py`: early wake and early timer expiry before
  selection reproduced the failure before the fix and passed after it; includes
  the previous transition, sleeper-membership and CFS policy regressions.
- `python3 /tmp/nixel-phase2-wait-publication.py`: uses production mutex and
  scheduler code with simulated interrupt flags and scheduling. Checks release
  before owner check, release at the first yield after waiter publication,
  IRQ exclusion at block/wake, ownership before wake, duplicate notification,
  waiter resumption and restoration of interrupt flags.
- `python3 /tmp/nixel-cfs-mutex-test.py`: existing mutex regressions passed.
- `make -j4 src/kernel/kernel.bin` and `git diff --check`: passed.

These deterministic host checks model interleavings; they do not execute real
interrupt entry/return.

### Phase 2, increment 3 — task-slot reuse and exit ownership

Task stacks live in the static task table, so task exit never frees the active
stack. Creation previously selected any UNUSED/TERMINATED slot before resetting
its mutex links and stack. Selection now additionally excludes current and
requires no owned mutexes, mutex wait links, runnable/tree membership or sleeper
link. A dirty slot stays unavailable rather than overwriting references retained
by the old context. This is a defensive reuse contract, not a resource reaper.

`task_exit()` now traps with UD2 if current owns a mutex, retains mutex wait
links, or has preemption disabled. A kernel invalid-opcode exception enters the
existing fatal exception path. Automatically releasing those locks would expose
potentially incomplete shared-state updates; silently abandoning them would
strand waiters. Clean exit still marks terminated and yields with IRQs disabled.
There is no asynchronous task-kill API or robust owner-death mutex protocol.
`scheduler_exit()` remains the policy bookkeeping primitive, not a replacement
for the current-task exit/resource contract.

Initial reclamation review: app_run switches back to kernel mappings before
clearing its running pin; collection also requires pending_release, zero window
references and a different active directory under kernel_mutex. Suspended
callbacks, saved directory references and failure paths still need focused
reclamation checks, so that checklist item remains open.

Validation:

- `python3 /tmp/nixel-phase2-reuse.py`: production slot predicate/creation and
  exit control flow with host platform stubs. Covers current-stack exclusion,
  every stale-link rejection, slot exhaustion, clean reuse, nice clamping,
  null entry, dirty-exit traps and clean exit selection. Privileged CLI/UD2 and
  final yield are modeled; this does not execute the actual exception handler.
- Previous wakeup/transition, wait-publication and mutex harnesses passed.
- `make -j4 src/kernel/kernel.bin` and `git diff --check`: passed.

### Phase 2, increment 4 — suspended address-space references

Collection previously checked only the collector CPU's active directory after
checking the app running/window pins. Added `task_address_space_in_use()` to
sample active CR3's software mirror and every suspended live task directory
with local IRQs disabled. The collector now defers cleanup for those references
before forgetting syscalls/handlers or freeing pages. READY, RUNNING, BLOCKED
and SLEEPING contexts count; UNUSED/TERMINATED contexts cannot resume and do
not count. Current's saved directory may be stale after an explicit mapping
switch, so current is checked through the actual active directory instead.
The helper preserves the caller's interrupt state, including an already-masked
caller. It is a snapshot, not a reference acquisition API.

The collector retains kernel_mutex while checking and destroying. That lock
serializes the existing app lifecycle/callback operations; the helper alone
must not be used to justify freeing mappings without serializing new references.
Task stacks remain static and are not freed by this collector.

Validation:

- `python3 /tmp/nixel-phase2-address-space.py`: production query and collector
  control flow with simulated tasks, IRQ flags, mutex scope and paging cleanup.
  Covers active CR3, suspended task states, dead/unused references, stale current
  metadata, running/window/release pins, cleanup after the last context clears,
  exactly-once destruction and interrupt-state preservation.
- Previous reuse/exit, wakeup/transition, publication and mutex harnesses passed.
- `make -j4 src/kernel/kernel.bin` and `git diff --check`: passed.

This is one reclamation safeguard, not the completed audit. Temporary callback
contexts can also retain a previous directory on a kernel stack, especially
with nested dispatch. Those saved return mappings and loader failure paths are
next.

### Phase 2, increment 5 — nested callback mappings and loader failures

Added balanced `app_loader_context_enter/leave` mapping scopes. Enter retains
an app-owned return directory before switching away; leave restores it before
dropping that reference. Collection now also requires zero context references.
This covers return directories held only in kernel stack locals during nested
callbacks, including repeated entry into the same directory. The active/suspended
task checks continue to protect the current execution mapping. Scope reference
updates and collection share kernel_mutex.

Window callback scopes, mouse/focus dispatch, IPv4 protocol callbacks and loader
preparation now use these helpers. IPv4 handlers without an explicit owner keep
the caller's mapping, preserving their previous behavior. Loader preparation
retains its return directory across disk reads with the core lock released.
Read/header/argument failures restore it before collecting the failed instance;
task-creation failure occurs after restoration. Allocation failures happen
before entering a mapping scope and release their partial address space directly.
Successful preparation publishes the running pin before queuing execution.

Validation:

- `python3 /tmp/nixel-phase2-contexts.py`: production scope helpers, query and
  collector with host platform stubs; nested app-to-app-to-kernel scopes,
  same-directory nesting, balanced references, restoration and delayed cleanup.
- `python3 /tmp/nixel-phase2-loader-failures.py`: production preparation control
  flow with injected directory/page allocation, read, header, argument and
  task-creation failures. Checks return codes, mapping restoration, balanced
  scope references, cleanup and successful running-pin publication. Reentrant
  collection during simulated disk reads retains the caller's return mapping.
- Existing task reuse, wakeup, wait publication and mutex checks passed.
- `make -j4 src/kernel/kernel.bin` and `git diff --check`: passed.

These scopes must be balanced in LIFO order. They protect managed app directories;
they do not introduce asynchronous task cancellation or recover abandoned kernel
call chains. The next Phase 2 step is idle and nested preemption boundary validation.

### Phase 2, increment 6 — idle and interrupt boundaries — 2026-10-23

Added the repeatable `tools/multitasking-boundary/run.py` QEMU regression. It
builds an isolated Multiboot test kernel in a temporary directory using the
production `task.c`, `scheduler.c` (including its hardware clock), and
`task_switch.asm`. A minimal GDT, IDT and PIC setup delivers real 1 kHz PIT
interrupts through the production save/restore and IRET path. No OS disk image
is modified. Requires GCC with i386 support, NASM, GNU ld and qemu-system-i386.

The test checks two nested preemption disables while timer interrupts continue
accounting and request rescheduling. The inner enable cannot switch; the outer
enable services the request. It also checks nested IRQ save/restore, enabling
preemption with IRQs masked, retention of the pending request until timer return,
and a voluntary sleep that resumes with IF still disabled. Switch hooks assert
that local IRQs are masked, preemption depth is zero and timer tracing has ended.

After the worker exits, 128 alternating zero/one/two-millisecond sleeps exercise
the production idle `sti; hlt` loop and timer wakeup. Every sleep must produce
exactly one idle entry and exit, restore the boot context and IF, and leave
current runnable outside the run tree. Idle time must increase while runnable
count and load include only the boot task. The existing idle loop requires no
change: timer-return selection handles runnable work before returning to idle;
adjacent STI/HLT prevents an interrupt between enabling IRQs and halting.

Validation:

- `python3 tools/multitasking-boundary/run.py`: passed on one-CPU QEMU TCG with
  `qemu32` (TSC accounting) and `qemu32,-tsc` (PIT clock fallback). Both runs check
  the selected clock source and exit through the debug-exit device; a crash,
  assertion failure or 30-second timeout fails the runner.
- `make -j4 src/kernel/kernel.bin`: passed.
- `git diff --check`: passed.

Tracing hooks are test assertions, rather than the production trace recorder.
Paging and TSS operations are stubbed: this validates ring-0 stacks, real timer
delivery, idle progress and deferred scheduling on one CPU, not user transitions,
CR3 isolation, other IRQ handlers, SMP or mixed-device workload latency.
Phase 2 is complete; the next step is pinning and specifying the Phase 3 EEVDF
policy before changing task selection.
