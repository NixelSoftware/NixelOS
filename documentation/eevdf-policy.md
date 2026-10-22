# EEVDF policy reference for NixelOS

This specification pins **upstream Linux tag `v6.12`**, not the moving `linux-6.12.y`
stable branch or the latest scheduler documentation. It completes the source-policy
audit in [the multitasking plan](linux-multitasking.md). The running NixelOS
scheduler uses the port described in the [NixelOS arithmetic and lifecycle
specification](eevdf-arithmetic.md), which defines integer bounds and port-specific
placement. Production sources are `src/kernel/sched_policy.c` and
`src/kernel/scheduler.c`.

## Source and scope

The authority is Linux v6.12
[fair.c](https://github.com/torvalds/linux/blob/v6.12/kernel/sched/fair.c), with
[features.h](https://github.com/torvalds/linux/blob/v6.12/kernel/sched/features.h)
for default feature switches. The downloaded `fair.c` has SHA-256
`48c485811e4d20cca0204ea75a24da71777e42a66b6ce158f8ece09132635d7d`.
Function names below identify the audited rules without relying on line numbers.
The [versioned overview](https://docs.kernel.org/6.12/scheduler/sched-eevdf.html)
provides context; source resolves algorithm details.

The reference domain is one fair run queue of ordinary tasks, with no group
scheduling, bandwidth throttling, migration, or other scheduling classes. Linux
also supports those features; they are outside this single-CPU port increment.
Its relevant defaults are:

| Feature | v6.12 default | Effect |
| --- | --- | --- |
| `PLACE_LAG` | enabled | Preserve saved lag during placement |
| `PLACE_DEADLINE_INITIAL` | enabled | Shorten the first virtual request |
| `PLACE_REL_DEADLINE` | enabled | Preserve relative deadlines across non-sleep removal |
| `RUN_TO_PARITY` | enabled | Protect the selected current request |
| `PREEMPT_SHORT` | enabled | Let a shorter request remove that protection |
| `DELAY_DEQUEUE`, `DELAY_ZERO` | enabled | Retain sleep debt and discard accrued positive delayed lag |
| `WAKEUP_PREEMPTION` | enabled | Permit wakeup reschedule requests |
| `NEXT_BUDDY` | disabled | No default next-buddy preference |

## Accounting and eligibility

For entity `i`, let `w_i` be its positive nice weight, `v_i` its virtual runtime,
`r_i` its requested execution slice in real nanoseconds, and `d_i` its virtual
deadline. Define `F_i(t) = calc_delta_fair(t, i)`, conceptually `t * 1024 / w_i`
using i386 nice-0 weights. Linux uses reciprocal fixed-point arithmetic for
non-default weights; the conceptual quotient is not a promise of bit-identical
rounding. `update_curr()` adds `F_i(elapsed_execution)` to `v_i`.

`avg_vruntime()` derives virtual time `V` from the weighted mean of participating
entities. Current is outside the tree but contributes while `on_rq`; delayed
sleepers still on the run queue contribute too. The CPU idle fallback does not.
With origin `m = cfs_rq->min_vruntime`, define:

```text
A = sum(w_j * (v_j - m))
W = sum(w_j)
V = m + floor(A / W)       when W > 0
V = m                     when W = 0
```

The origin is not `V`: it supplies a stable coordinate system. The source
compensates a negative numerator before signed division to obtain the floor.
`vruntime_eligible()` avoids division entirely and tests
`A >= (v_i - m) * W`, including equality. Thus eligibility means nonnegative
service lag, conceptually `w_i * (V - v_i) >= 0`. A saved `vlag` value is not the
live eligibility predicate.

For example, equal-weight entities at virtual times 8 and 12 have `V = 10`:
only the entity at 8 is eligible. With eligible candidates at 8 and 9 and an
equal-weight third entity at 13, `V = 10`; deadlines 14 and 11 favor the entity
at 9 in an unprotected selection. Minimum virtual runtime alone cannot select it.

## Requests, deadlines and selection

`update_deadline()` leaves an unconsumed request intact while signed
`v_i - d_i < 0`. At exhaustion it sets `d_i = v_i + F_i(r_i)` and reports renewal.
It does not repeatedly add slices to the old deadline to catch up after an
overshoot. Default `r_i` is refreshed from `sysctl_sched_base_slice`; v6.12's
unscaled default is 750,000 ns, with CPU-count scaling factor one on one CPU.
Custom slices survive this default refresh. This is a reference value, not yet
a NixelOS tuning decision for its 1 kHz timer.

`pick_eevdf()` implements these ordered rules:

1. With one participating entity, return current if on the run queue, otherwise
   the first tree entity.
2. Exclude current from consideration if off the run queue or ineligible.
3. With `RUN_TO_PARITY`, retain eligible current if its selection-time deadline
   marker still matches its current deadline.
4. Otherwise find the eligible tree entity with earliest deadline, then replace
   it with eligible current only if current has a strictly earlier deadline.

`set_next_entity()` stores the deadline marker in `vlag`. That field is reused
for saved lag at dequeue; it must not be interpreted as lag while it contains
the protection marker. Renewal changes the deadline and ends protection;
ineligibility also prevents retaining current. Equal deadlines do not define a
portable FIFO guarantee: Linux's deadline comparator has no sequence tiebreak.
A NixelOS deterministic tie rule must be stated separately.

Linux uses a deadline-ordered augmented RB tree with subtree minimum virtual
runtime to prune ineligible subtrees. This records the reference algorithm;
choosing the NixelOS queue representation remains a later checklist item.

`check_preempt_wakeup_fair()` first accounts current. For ordinary tasks,
`do_preempt_short()` removes current's protection when the arriving task has a
strictly shorter slice, is eligible, and either has an earlier deadline or
current is ineligible. The wakeup requests rescheduling only if `pick_eevdf()`
then selects that arriving entity. An earlier deadline by itself cannot bypass
protected eligible current. `update_curr()` requests rescheduling on renewal or
the `did_preempt_short()` condition when there is competition. These are requests,
not permission to switch inside an arbitrary IRQ/critical section.

Linux's voluntary `yield_task_fair()` accounts current and advances its deadline
by one virtual slice when there is competition. NixelOS also uses `task_yield()`
internally for deferred preemption and blocking. A future port must distinguish
those reasons before applying the voluntary-yield deadline penalty.

## Saved lag, placement and sleeping debt

`entity_lag()` captures virtual lag `V - v_i`, clamped symmetrically to
`F_i(max(2 * r_i, TICK_NSEC))`; `TICK_NSEC` is the pinned kernel configuration's
tick duration. `update_entity_lag()` captures this before final removal.
This bound applies to saved lag, not a continual clamp of all runtime values.

`place_entity()` starts from the existing run queue's `V`. With `PLACE_LAG` and
existing participants of weight `W`, saved virtual lag `l_i` becomes the placement
offset `l_i * (W + w_i) / W`; the entity is placed at `V - offset`. Compensation
is necessary because adding the entity also moves the mean. For example, an
equal-weight arrival with saved lag 4 joining one entity at 100 is placed at 92;
the new mean is 96, preserving lag 4. Without existing participants the offset
is zero. Signed division and integer boundary details belong to the next step.

After placement, `PLACE_REL_DEADLINE` restores a saved relative deadline when
present and returns. Otherwise the deadline is `v_i + F_i(r_i)`, except that
`ENQUEUE_INITIAL` halves the virtual slice with `PLACE_DEADLINE_INITIAL`.
The stored real slice is not halved. Non-sleep dequeue records `d_i - v_i` as a
relative deadline; an ordinary fully dequeued sleeper receives a new deadline
on placement. Waking a still-delayed entity follows a different path:

| Event | v6.12 action |
| --- | --- |
| Ordinary sleep while ineligible | `dequeue_entity()` marks `sched_delayed` and leaves the entity participating |
| Ordinary sleep while eligible | Finish dequeue and retain bounded saved lag |
| Selection reaches a delayed entity | `pick_next_entity()` finishes its dequeue and returns no executable entity; selection retries |
| Wake before delayed dequeue completes | `requeue_delayed_entity()` cancels delayed state without a duplicate enqueue |
| Special-state sleep (`DEQUEUE_SPECIAL`) | Bypass delayed dequeue |

Delayed entities must never execute while asleep. While others run, their
stationary virtual runtime approaches eligibility as the mean advances; this
removes debt by virtual service progress, not a wall-clock sleep timer. With
`DELAY_ZERO`, final delayed dequeue clips positive saved lag to zero. Waking a
still-delayed entity with positive lag removes and repositions it with zero lag
before clearing delayed state; negative lag is retained. Fully dequeued sleepers
retain their bounded saved lag until placement: sleep alone does not universally
reset lag to zero or earn unlimited credit.

## Port specification and implementation requirements

The [arithmetic and lifecycle specification](eevdf-arithmetic.md) defines i386
representations, rounding, bounds, reweighting and sleeper/deferred-dequeue
transitions alongside timed waits, mutex waits, inheritance, exit and slot reuse.
These rules must be implemented and validated before adding EEVDF to production. A retained
policy entity must not make a blocked task executable or keep a dead slot reusable
while referenced. Distinguish executable count/load from retained policy weight.

The policy harness must eventually cover equality at eligibility, current's
contribution, request renewal overshoot, protection/short-request preemption,
placement compensation, delayed wake/dequeue, last executable task blocking,
and weight changes. The port specification records arithmetic choices, new-task
defaults, tie order and departures from this reference for those tests.
