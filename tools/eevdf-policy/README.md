# Kernel EEVDF policy harness

Kernel task creation defaults to a 1 ms request; `task_create_with_request()`
supports explicit 1–16 ms kernel service requests. These tests retain explicit
4 ms reference workloads as well as shorter/longer requests, so changing the
production default does not silently weaken the existing workload assertions.
The wide arithmetic tests also cover the word-at-a-time divider used on i386;
the real-interrupt test executes its signed-floor and multiword cases in QEMU.

Run from the repository root:

```sh
python3 tools/eevdf-policy/test.py
```

Requires Python 3.9+, GCC with an i386 target and host AddressSanitizer/UBSan,
GNU ld and nm. Generated libraries, binaries and stack reports live in a temporary
directory and are removed after the run. The tests compile the actual kernel
policy sources; running the tests changes no OS image or production object.

The implementation follows [the arithmetic and lifecycle specification](../../documentation/eevdf-arithmetic.md):

- [sched_wide.c](../../src/kernel/sched_wide.c) implements signed magnitude arithmetic with four 32-bit limbs.
- [sched_policy.c](../../src/kernel/sched_policy.c) implements accounting, eligibility, deadline renewal, protected
  current selection, short-request wakeup preemption, placement, reweighting,
  explicit voluntary yield and delayed-sleeper cleanup.
- `oracle.py` independently calculates the policy with Python integers and
  rational virtual time. `test.py` compares every field and eligibility result
  after every transaction, in addition to hand-checked cases and invariants.
- `native.c` exercises arithmetic edges and 20,000 C lifecycle events across all
  32 slots under host ASan/UBSan. Leak detection is disabled because the test
  allocates no heap memory and sandbox debuggers cannot run LeakSanitizer.
- `workloads.py` drives the C policy with simulated time and checks analytic
  service budgets, time conservation and dispatch latency independently of the
  transition oracle. It selects only on policy reschedule requests.

The default command also compiles and combines the policy for freestanding
i386, rejects unresolved symbols (including compiler arithmetic helpers), and
checks individual compiler-reported stack frames stay below 16 KiB. Its policy
transaction copies about 2.6 KiB of queue state before committing, so a rejected
operation cannot publish partial accounting or queue changes. This is a reference
harness and kernel implementation; the fixed slot array matches the 32-task limit.
`EE_RANGE` reports a checked invariant failure to the test instead of trapping
the host process. The kernel scheduler routes that failure to its fatal invariant
path; `EE_INVALID` denotes a rejected API input.

On Linux with IA32 execution support, optionally run the same C stress test as a
static libc-free i386 executable:

```sh
python3 tools/eevdf-policy/test.py --native
```

That executable exits through `int 0x80`; some sandboxes block this syscall entry
and require executing the test outside the sandbox. The optional check executes
policy arithmetic on the i386 ABI, not kernel privilege transitions.

`ee_apply()` takes an explicit clock for each event. Selection is a separate
`EE_PICK` event, so tests can examine accounting, early wake and deferred requests
before selecting. `EE_SLEEP` accepts a relative duration in ns (zero means 1 ms);
timeouts are processed on later events. `EE_REMOVE` tests temporary non-sleep
removal and relative deadline restoration through `EE_WAKE`. There are no actual
mutexes, interrupt handlers or linked sleeper lists here. State and timeout fields
model their policy-facing effects. `EE_UNUSED` and `EE_DEAD` are not executable;
idle is represented by current ID `-1`, outside the 32 entity slots.

## Workload acceptance

The default test command runs these fixed workloads with a 1 ms simulated timer:

| Workload | Acceptance condition |
| --- | --- |
| 2, 8 and 32 equal-weight CPU tasks, 4 s each | Every task's service at every tick is within 5 ms of equal share; no idle time; all tasks receive service |
| Nice -5/0/5/10 for 10 s, and -20/19 for 40 s | Every tick stays within 5 ms of `elapsed * weight / total_weight`; all tasks receive service |
| Three tasks over equal, boost, restore epochs of 4 s each | Compare with the piecewise weighted fluid ideal; allow 5 ms accumulated error per epoch |
| Equal-weight 1 ms and 16 ms CPU requests for 10 s | Prefix service error at most 17 ms; short requests renew at least eight times as often; every exhaustion removes protection and requests selection |
| Negative-debt task, 100 immediate sleep/wake retries | Virtual debt remains unchanged and the ineligible task cannot displace its peer |
| 5,000 burst steps with 100 us sleep events | Service stays within 5 ms of the ideal that excludes the sleeper during its wait intervals |
| 100 ms sleep after overserving | Delayed membership is removed through requested selection, sleeper runtime stays fixed, and wake receives no positive sleep credit |
| Two periodic 1 ms interactive bursts with 1/4/16 CPU peers | At least 50 dispatches per scenario; completed and pending waits stay within the stated scenario envelope |

The periodic traces last 2 s each and start with both interactive tasks asleep.
Their initial release is `5 ms + phase`; subsequent waits are `10 ms + phase`.
Phases are 0, 125,000 and 999,999 ns, so latency includes timer quantization.
Measure from the actual sleep deadline to first dispatch, not merely from its
observation at a tick. The acceptance envelopes are 13/25/73 ms for 1/4/16 CPU
peers: one 4 ms service budget per participant plus one timer tick. These are
explicit regression thresholds for these workloads, not a general policy theorem.
Pending waits are checked every tick so a starved task cannot disappear from
the results just because it never completes.

The current traces produce 2,663 interactive dispatch samples with worst latency
18 ms. Fairness prefix error stays below 4 ms; the unequal-request CPU pair
receives 5,000 ms each. Both can run contiguously for 16 ms: renewal may reselect
the same task while its peer is in debt, so request size is not a contiguous-run
cap. Sub-tick sleep-debt events represent explicit producer/timer boundaries in
the simulator; they do not change the kernel PIT frequency.

The kernel now uses the fixed slot array, with one weighted snapshot per candidate
scan. [scheduler.c](../../src/kernel/scheduler.c) maps real task lifecycle and
priority-inheritance calls to this policy; actual context switching remains in
`task.c` and `task_switch.asm`. Sleep deadlines live in the policy slots rather
than a second linked list. The kernel has 31 executable slots and one idle slot;
the pure policy checks can use all 32 slots because they have no kernel idle task.

These tests cover simulated policy workloads. The separate
`python3 tools/multitasking-boundary/run.py` command executes the production
scheduler/task/mutex paths under real timer interrupts in QEMU, including 30 CPU
workers and a boot task. Controlled comparison with CFS under CPU/input/network/
audio workloads remains outstanding. No host timing or real-device latency is
measured by the Python policy workloads.
