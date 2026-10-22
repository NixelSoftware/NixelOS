# NixelOS EEVDF arithmetic and lifecycle specification

This is the implementation contract for the kernel policy and its harness, based on the
[Linux v6.12 reference](eevdf-policy.md). These are NixelOS design decisions;
the production scheduler now implements them using a bounded slot array. See
[the plan](linux-multitasking.md) for integration validation and remaining work.

## Units, representations and limits

All real times and virtual coordinates are integer nanoseconds. Keep the existing
nice table, clamp effective nice to `[-20, 19]`, and use weights `[15, 88761]`.
Use the effective weight for inheritance as well as ordinary nice changes;
`base_nice` remains the restoration target. Idle never participates.

| Quantity | Representation and contract |
| --- | --- |
| Real clock, sleep deadlines, runtime totals | `uint64_t`; monotonic, checked addition, no wrap interpretation |
| Virtual runtime `v`, deadline `d` | Signed 64-bit coordinates relative to the run queue origin |
| Detached saved virtual lag `l` | Signed 64-bit duration, not an absolute coordinate |
| Accounting remainder `c` | Unsigned 32-bit, `0 <= c < weight` |
| Requested real slice `r` | Unsigned 32-bit; kernel default 1,000,000 ns; supported range 1,000,000–16,000,000 ns |
| Participant and executable counts | Separate unsigned 32-bit counts, each at most `MAX_TASKS` |
| Participant and executable weights | Separate unsigned 32-bit sums, each at most `32 * 88761 = 2840352 < 2^22` |
| Weighted numerators and multiplication intermediates | Signed magnitude with four unsigned 32-bit limbs (128-bit capacity) |

The kernel defaults to one PIT tick (1 ms) after mixed-workload comparison found
that 4 ms requests delayed input behind legitimate earlier CPU deadlines. Desktop,
network and display service requests are explicitly 1 ms; nice weights continue
to determine CPU shares. `task_create_with_request()` accepts a kernel-only
request at creation. No new user-facing slice API is introduced. Initial half
requests can be shorter than one tick. The policy harness explicitly uses both
1 ms and longer requests, including 4 ms reference workloads. Invalid creation
requests are rejected without mutation, rather than silently clamped.

Use a distinct protection boolean for current, not Linux's `vlag` marker trick.
Saved lag and relative deadline must never double as current-request markers.
Only a detached entity may hold a saved relative deadline. Ordinary sleep saves
lag only; explicit temporary policy removal can also save `d - v`.

## Arithmetic primitives and accounting

GCC's i386 target does not provide a usable native `__int128` for this build.
Implement the small wide-integer primitives locally using unsigned limb carry,
borrow and multiplication; do not add unresolved libgcc arithmetic helpers.
Division proceeds a word at a time using DIVL on i386, with an incoming remainder
strictly below the divisor. Checked native 64-bit addition/subtraction handles
coordinate changes; it need not construct 128-bit temporaries. Weighted sums
and products still use the wide representation and all narrowing remains checked.
Required operations are signed addition/comparison, signed-64 by unsigned-32
multiplication, and magnitude division by a nonzero unsigned-32 divisor. Check
quotient range before narrowing. Do not use signed overflow, implementation-
defined unsigned-to-signed conversion, or signed right-shift for arithmetic.
The deterministic host oracle will use arbitrary-precision integers.

For positive `w`, define two explicitly different divisions:

```text
truncdiv(x, w) = sign(x) * floor(abs(x) / w)
floordiv(x, w) = truncdiv(x, w) - 1 if x < 0 and abs(x) % w != 0
                truncdiv(x, w)     otherwise
F(t, w)       = floor(t * 1024 / w), for nonnegative t
```

Use `floordiv` for the weighted mean. Use `truncdiv` for signed placement and
reweighting, following the reference's signed-division direction. Slice and lag
limits use `F` with no accounting remainder. Compute current's service as:

```text
n = elapsed_ns * 1024 + c
v += floor(n / w)
c  = n % w
```

Retaining `c` makes one elapsed interval equivalent to any partition of that
interval at unchanged weight. Small repeated clock samples must not erase
service. Account current exactly once with its old weight before every mutation;
sleepers and idle accrue no execution. Keep `c` across switches and sleeps.
Non-increasing clock samples account zero and cannot move `exec_start` backward.
Real totals and wake deadlines use checked addition; violating the clock/range
contract traps before publishing a partially updated policy state.

## Coordinates, eligibility and overflow proof

At each transaction boundary, all participating `v` and `d` coordinates must
satisfy `abs(coordinate) < B`, where `B = 2^60`. Detached tasks store durations,
not stale absolute coordinates. Let `P` contain executable entities plus retained
delayed sleepers, including current once. Compute:

```text
W = sum(w_j for j in P)
A = sum(w_j * v_j for j in P)
V = floordiv(A, W) if W != 0 else 0
eligible(i) <=> A >= v_i * W
```

Eligibility uses the wide cross product, including equality. Do not test a
rounded mean or saved lag instead. At least one participant is eligible when
`W > 0`; it may need delayed cleanup before an executable entity can be chosen.

Rebase after accounting and membership/weight changes: compute `V`, subtract it
from every participating `v` and `d`, and rebuild weighted/queue summaries in
the same IRQ-disabled transaction. Current's protection boolean and accounting
remainder survive; detached lag, relative deadlines and real sleep deadlines do
not move. New `A` is `A - V * W`; it need not be zero because of integer division.
With no participants, reset the virtual origin to zero. These local coordinates
are not the existing monotonic CFS `min_vruntime` statistic; an eventual diagnostic
ABI must expose them distinctly rather than label them a monotonic floor.

Rebasing preserves eligibility and relative deadline order exactly but does not
bound a divergent spread by itself. Before committing, check the new coordinate
bound. A violation is an explicit kernel invariant failure, never saturation,
wrapping, or a silent debt reset. Real elapsed time per transaction must be less
than `2^48` ns (about 3.25 days); a larger unaccounted interval is also a contract
failure. This bound is independent of interrupt frequency and allows long IRQ
masking without assuming every PIT tick was delivered.

The bounds leave ample intermediate capacity:

- Stable weighted sums and eligibility products are below `2^82` in magnitude.
- One accounting step adds less than `2^58 / 15 + 1 < 2^55` virtual ns, so
  pre-rebase coordinates stay below `2^61`.
- Differences from `V` before reweighting are below `2^62`; multiplying by
  `88761 < 2^17` remains below `2^79`. Summing at most 32 such weighted terms
  is comfortably within signed magnitude 128-bit capacity, even before rebasing.
- A signed 64-bit result is range-checked before storage; successful wide
  arithmetic alone does not imply that a coordinate is valid.

Rebase in bounded passes over at most 32 slots. This does not select a production
queue; its cached keys/augmentation must be rebuilt or updated coherently.

## Requests and weight changes

New tasks start with zero lag and remainder. Place at existing `V`, or zero if
empty, and set `d = v + floor(F(r, w) / 2)` for their first request. Later renewal
at `v >= d` sets `d = v + F(r, w)` and removes protection. Sleep/wake is not task
creation. Do not halve requests on slot reuse until the old entity is fully
removed and initialization creates a genuinely new task.

Renewal and unprotected-ineligibility reschedule checks count policy participants,
including retained delayed sleepers. One executable task with a retained sleeper
still needs selection boundaries so that delayed dequeue can finish. This is
distinct from voluntary yield's requirement for an executable competitor.

Eligible protected current remains selected until renewal, blocking, explicit
voluntary yield, or shorter-request preemption removes protection. An ordinary
reschedule boundary does not impose a yield penalty. Existing `task_yield()`
callers remain boundary-only; a separate explicit policy operation must represent
voluntary yield in the harness and any later API. Explicit voluntary yield
adds one `F(r, w)` to `d`, removes protection and requests selection; it does not
change `v`. With no executable competitor it is a no-op after accounting.
For unprotected equal deadlines, prefer eligible current, otherwise the lowest
task ID. These explicit choices avoid a wrapping sequence counter and preserve
stable ties. They are NixelOS rules, not Linux's unspecified equal-deadline order.

All effective weight changes, including mutex inheritance/restoration, use one
atomic operation. For a participant, snapshot `V` with its old contribution still
included, then use old weight `w` and replacement `w2`:

```text
limit = F(max(2*r, 1000000), w)
l     = clamp(V - v, -limit, limit)
v2    = V - truncdiv(l * w, w2)
d2    = V + truncdiv((d - V) * w, w2)
c2    = floor(c * w2 / w)
```

Account first, update keys/sums after replacing weight and coordinates, clear
current protection, rebase, then request rescheduling. This follows the pinned
source's clamped live lag and deadline scaling; clamping and integer rounding
mean the weighted mean and lag are not guaranteed perfectly invariant. `c2`
preserves fractional virtual service within less than one new remainder unit.
Never issue a fresh request merely because nice changed. Retained delayed tasks
use the same participant operation and stay asleep.

For a detached sleeper, rescale saved `l` by `truncdiv(l*w, w2)` and clamp it to
the new `F(max(2*r, 1000000), w2)`. Rescale any valid saved relative deadline
by the same ratio, with range checks. Transform `c` as above. Do not place or
wake the task. This post-scaling lag clamp is an explicit NixelOS bound; Linux's
detached rescaling alone does not impose it. Changing to the same weight is a
no-op after accounting and does not discard protection or repeatedly clamp lag.

## Sleeper placement and lifecycle

Track `policy_member` and `delayed` separately from executable `runnable`, task
state, and timed-list membership. `in_tree` describes actual queue membership;
current remains outside the queue. All mutations run with local IRQs disabled.
Existing sleeper deadline order and mutex ownership-before-wake contracts remain.

For detached wake, use the saved bounded lag and the existing participants'
`V, W`, excluding the arriving entity:

```text
offset = truncdiv(l * (W + w), W) if W != 0 else 0
v = V - offset
d = v + F(r, w)                 # ordinary sleeper, full fresh request
```

A valid saved relative deadline instead restores `d = v + relative_deadline`.
Recompute membership sums after placement; never clamp the placement offset
back to `l`, which would erase the compensation. For the maximum slice, saved
lag magnitude is at most `floor(32000000 * 1024 / 15) = 2184533333 < 2^32`.
Even compensation with `W = 15` and a weight of 88761 is below `2^45`; its
wide numerator is below `2^54`. Placements therefore fit the coordinate bound.

| Transition | Required operation |
| --- | --- |
| Executable task blocks/sleeps | Account; drop executable count/load; clear protection; retain policy membership as delayed only if ineligible, otherwise save bounded lag and detach |
| Timed sleep | Perform block transition, then insert one deadline-list entry; replacement sleep unlinks the old deadline first |
| Explicit block of timed sleeper | Cancel timed-list membership; preserve existing policy debt, without a second block/decrement |
| Selection encounters eligible delayed task | Save bounded lag with positive values clipped to zero, detach without execution, then retry selection |
| Wake retained delayed task | Cancel timeout; if live bounded lag is positive, detach and re-place with zero lag; otherwise retain coordinates; clear delayed and publish executable state once |
| Wake detached sleeper | Cancel timeout; place using saved lag, then publish executable state once |
| Wake current before it switches away | Restore RUNNING, not READY; keep it outside the queue, including the positive-lag re-placement case |
| Exit | Force policy removal even when ineligible; cancel timeout, clear saved state/protection/remainder; terminate through the existing exit ownership contract |

Mutex waits also retain negative debt without executing or generating a spurious
wake. Delayed cleanup never touches `waiting_mutex`, waiter links or task state;
only the existing producer/handoff path can publish runnable state. This adapts
Linux's delayed mechanism to the current NixelOS wait contract.

If executable count reaches zero, finish all delayed removals immediately using
the same pre-drain `V` for their saved bounded lag, clipping positive values to
zero, then reset the empty origin. Idle advances no virtual service. The next
arrival with `W = 0` starts at zero; saved debt has no effect without competition.
This explicit empty-queue rule prevents retained sleepers from stranding idle
or pinning the clock. No wall-clock sleep bonus or CFS sleeper-credit floor is
added. Duplicate wake/block and invalid/dead/idle transitions keep the existing
no-op guards. Slot reuse additionally requires no policy/delayed membership or
valid saved relative deadline; static stack ownership checks still apply.

## Implementation acceptance

The [deterministic harness](../tools/eevdf-policy/README.md) compiles the kernel
policy directly. Its checks cover negative floor versus truncation,
extreme weights, partitioned accounting, remainder rescaling, negative coordinates,
rebasing, overflow rejection, zero/one participant, placement rounding, repeated
inheritance/restoration, delayed wake/cleanup, empty queues and current early wake.
Validate freestanding i386 code has no unresolved arithmetic runtime helpers.
The queue is a fixed array with a single weighted snapshot per ordinary candidate
scan. Kernel task states mirror the policy state under masked IRQs; idle is not a
policy participant. The old linked sleeper list and CFS tree are removed, and
slot reuse also queries authoritative policy ownership. The internal monotonic
CFS floor statistic is removed rather than relabeled as local virtual time.
Controlled mixed-workload comparison against CFS remains outstanding.
