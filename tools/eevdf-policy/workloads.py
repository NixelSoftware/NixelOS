"""Deterministic workload acceptance independent of the transition oracle.

Time is simulated; these measurements are policy bounds, not host benchmarks.
Selection occurs only when the policy requests it, never on every tick by fiat.
"""
import ctypes as C
from fractions import Fraction

from oracle import CREATE, TICK, PICK, SLEEP, WAKE, NICE, READY, RUNNING, SLEEPING, weight

MS = 1000000


class Simulation:
    def __init__(self, lib, queue_type):
        self.lib, self.q = lib, queue_type()
        lib.ee_init(C.byref(self.q))
        self.idle = 0
        self.dispatches = 0

    def event(self, op, i=0, arg=0, value=0, now=None):
        now = self.q.now if now is None else now
        old_time, old_current = self.q.now, self.q.current
        before = [t.runtime for t in self.q.task]
        executing = old_current >= 0 and self.q.task[old_current].state in (READY, RUNNING)
        result = self.lib.ee_apply(C.byref(self.q), op, i, arg, value, now)
        assert result == 0, (op, i, now, result)
        elapsed = self.q.now - old_time
        if not executing:
            self.idle += elapsed
        for j, t in enumerate(self.q.task):
            assert t.runtime - before[j] == (elapsed if executing and j == old_current else 0)
            assert not (t.delayed and t.state in (READY, RUNNING))
        assert sum(t.runtime for t in self.q.task) + self.idle == self.q.now
        if op == PICK:
            assert self.q.current == -1 or self.q.task[self.q.current].state == RUNNING
            self.dispatches += self.q.current != old_current

    def settle(self):
        if self.q.need_reschedule:
            self.event(PICK)

    def create(self, i, nice=0, request=4*MS):
        self.event(CREATE, i, nice, request)

    def advance(self, now):
        self.event(TICK, now=now)
        self.settle()


def fairness(lib, queue_type):
    cases = [
        ('equal-2', [0]*2, 4000),
        ('equal-8', [0]*8, 4000),
        ('equal-32', [0]*32, 4000),
        ('weighted', [-5, 0, 5, 10], 10000),
        ('extreme-weights', [-20, 19], 40000),
    ]
    results = []
    for name, nice, ticks in cases:
        sim = Simulation(lib, queue_type)
        for i, n in enumerate(nice):
            sim.create(i, n)
        sim.settle()
        weights = [weight(n) for n in nice]
        total = sum(weights)
        maximum_error = 0
        # One request plus one timer quantum is the absolute service-error budget.
        bound = 5*MS
        for tick in range(1, ticks+1):
            sim.advance(tick*MS)
            for i, w in enumerate(weights):
                error = abs(sim.q.task[i].runtime*total - sim.q.now*w)
                assert error <= bound*total, (name, tick, i, error/total)
                maximum_error = max(maximum_error, error)
        assert sim.idle == 0 and all(sim.q.task[i].runtime > 0 for i in range(len(nice)))
        results.append(f'{name}={maximum_error/total/MS:.3f}ms')
    print('PASS fairness: maximum prefix service error vs weighted ideal: ' + ', '.join(results))

    # Effective weight changes split the fluid reference into distinct epochs.
    sim = Simulation(lib, queue_type)
    for i in range(3):
        sim.create(i)
    sim.settle()
    ideal = [Fraction(0) for _ in range(3)]
    for epoch, effective in enumerate(([0, 0, 0], [-5, 0, 5], [0, 0, 0])):
        for i, n in enumerate(effective):
            sim.event(NICE, i, arg=n)
        sim.settle()
        weights = list(map(weight, effective))
        for _ in range(4000):
            for i, w in enumerate(weights):
                ideal[i] += Fraction(MS*w, sum(weights))
            sim.advance(sim.q.now+MS)
            for i in range(3):
                assert abs(sim.q.task[i].runtime-ideal[i]) <= (epoch+1)*5*MS
    print('PASS fairness: boost/restore epochs stay within accumulated 5ms-per-epoch error budget')


def short_requests(lib, queue_type):
    sim = Simulation(lib, queue_type)
    requests = [MS, 16*MS]
    for i, request in enumerate(requests):
        sim.create(i, request=request)
    sim.settle()
    longest = [0, 0]
    renewals = [0, 0]
    streak = 0
    previous = -1
    for _ in range(10000):
        running = sim.q.current
        streak = streak+MS if running == previous else MS
        previous = running
        longest[running] = max(longest[running], streak)
        exhausted = sim.q.task[running].d-sim.q.task[running].v <= MS
        sim.event(TICK, now=sim.q.now+MS)
        if exhausted:
            renewals[running] += 1
            assert not sim.q.task[running].protected_request and sim.q.need_reschedule
            assert sim.q.task[running].d-sim.q.task[running].v == requests[running]
        sim.settle()
        for i in range(2):
            assert abs(2*sim.q.task[i].runtime-sim.q.now) <= 2*17*MS
    # A request is a latency knob, not a different long-term CPU weight.
    assert abs(sim.q.task[0].runtime-sim.q.task[1].runtime) <= 2*17*MS
    # Renewal permits reselection of the same task while its peer is in debt;
    # request size is not an upper bound on a contiguous CPU run.
    assert renewals[0] >= 8*renewals[1] > 0
    print(f'PASS short requests: equal-weight 1/16ms requests, longest runs '
          f'{longest[0]/MS:g}/{longest[1]/MS:g}ms, service '
          f'{sim.q.task[0].runtime/MS:g}/{sim.q.task[1].runtime/MS:g}ms, '
          f'renewals {renewals[0]}/{renewals[1]}')


def sleep_debt(lib, queue_type):
    sim = Simulation(lib, queue_type)
    sim.create(0)
    sim.create(1)
    sim.settle()
    # Exhaust the first half-request without taking the requested switch yet.
    sim.event(TICK, now=2*MS)
    assert sim.q.current == 0 and not lib.ee_eligible(C.byref(sim.q), 0)
    sim.event(SLEEP, 0, value=MS//10)
    assert sim.q.task[0].delayed
    sim.settle()
    asleep_runtime = sim.q.task[0].runtime
    sim.advance(sim.q.now+MS//10)
    assert sim.q.task[0].runtime == asleep_runtime
    assert not lib.ee_eligible(C.byref(sim.q), 0) and sim.q.current == 1
    # Repeated immediate sleep/wake cannot erase the outstanding virtual debt.
    gap = sim.q.task[0].v-sim.q.task[1].v
    for _ in range(100):
        sim.event(SLEEP, 0, value=MS//10)
        assert sim.q.task[0].delayed
        sim.event(WAKE, 0)
        sim.event(PICK)
        assert sim.q.current == 1 and not lib.ee_eligible(C.byref(sim.q), 0)
        assert sim.q.task[0].v-sim.q.task[1].v == gap

    # Repeated actual bursts followed by sub-tick sleeps, with a fluid-service
    # reference that excludes the sleeper during each known 100us wait interval.
    ideal = [Fraction(t.runtime) for t in sim.q.task[:2]]
    sleeps = 0
    for _ in range(5000):
        running = sim.q.current
        for i in (0, 1):
            ideal[i] += Fraction(MS, 2)
        sim.advance(sim.q.now+MS)
        if running == 0:
            sim.event(SLEEP, 0, value=MS//10)
            sim.settle()
            ideal[1] += MS//10
            sim.advance(sim.q.now+MS//10)
            sleeps += 1
        for i in (0, 1):
            assert abs(sim.q.task[i].runtime-ideal[i]) <= 5*MS
    assert sleeps > 100
    print(f'PASS sleep debt: 100 immediate retries preserve debt; {sleeps} burst/sleep cycles '
          'stay within 5ms of available-task ideal')

    # While the debtor remains asleep, peer service eventually completes delayed
    # dequeue. Sleep then earns no positive credit, regardless of wall duration.
    sim = Simulation(lib, queue_type)
    sim.create(0)
    sim.create(1)
    sim.settle()
    sim.event(TICK, now=2*MS)
    sim.event(SLEEP, 0, value=100*MS)
    sim.settle()
    before = sim.q.task[0].runtime
    for _ in range(99):
        sim.advance(sim.q.now+MS)
    assert not sim.q.task[0].member and sim.q.task[0].lag == 0
    assert sim.q.task[0].runtime == before and sim.q.task[0].state == SLEEPING
    sim.advance(102*MS)
    assert sim.q.task[0].state in (READY, RUNNING)
    assert sim.q.task[0].v == sim.q.task[1].v, 'long sleep must not acquire a credit bonus'
    print('PASS sleep debt: delayed dequeue completes without executing sleeper or granting sleep credit')


def wake_latency(lib, queue_type):
    worst = 0
    samples = 0
    results = []
    for busy_count in (1, 4, 16):
        for phase in (0, MS//8, MS-1):
            sim = Simulation(lib, queue_type)
            for i in range(busy_count):
                sim.create(i)
            interactive = (busy_count, busy_count+1)
            for i in interactive:
                sim.create(i, request=MS)
                sim.event(SLEEP, i, value=5*MS+phase)
            sim.settle()
            pending = {}
            started = {}
            scenario_samples = 0
            scenario_worst = 0
            # Acceptance envelope for these traces: one 4ms service budget per
            # participant plus timer quantization. Earlier background deadlines
            # and retained debt can legitimately delay a short request.
            bound = (4*(busy_count+len(interactive))+1)*MS
            for _ in range(2000):
                old_current = sim.q.current
                old_runtime = sim.q.task[old_current].runtime if old_current >= 0 else 0
                due = {i: sim.q.task[i].wake_at for i in interactive
                       if sim.q.task[i].state == SLEEPING and sim.q.task[i].wake_at <= sim.q.now+MS}
                sim.event(TICK, now=sim.q.now+MS)
                pending.update(due)
                # A one-tick interactive burst completes at this boundary.
                if old_current in interactive and sim.q.task[old_current].runtime-old_runtime == MS:
                    assert old_current in started
                    sim.event(SLEEP, old_current, value=10*MS+phase)
                    del started[old_current]
                sim.settle()
                if sim.q.current in pending:
                    i = sim.q.current
                    latency = sim.q.now-pending.pop(i)
                    assert 0 <= latency <= bound, (busy_count, phase, i, latency)
                    worst = max(worst, latency)
                    scenario_worst = max(scenario_worst, latency)
                    samples += 1
                    scenario_samples += 1
                    started[i] = sim.q.now
                assert all(sim.q.now-release <= bound for release in pending.values()), (busy_count, phase, pending)
            assert scenario_samples >= 50 and not sim.idle
            results.append(f'{busy_count}peers/{phase}ns={scenario_worst/MS:.6f}ms')
    assert samples >= 1000
    print(f'PASS wake latency: {samples} periodic 1ms bursts with 1/4/16 CPU peers; '
          f'worst deadline-to-dispatch {worst/MS:.6f}ms; ' + ', '.join(results))


def run(lib, queue_type):
    fairness(lib, queue_type)
    short_requests(lib, queue_type)
    sleep_debt(lib, queue_type)
    wake_latency(lib, queue_type)
