#!/usr/bin/env python3
"""Compile in /tmp, compare C against an integer oracle, then check i386 linkage."""
import ctypes as C
import argparse
import os
from pathlib import Path
import random
import re
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
from oracle import (Oracle, FIELDS, WEIGHTS, entity, trunc, weight,
                    READY, RUNNING, BLOCKED, SLEEPING, DEAD,
                    CREATE, TICK, PICK, BLOCK, SLEEP, WAKE, EXIT, NICE, YIELD, REMOVE)

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
I64, U32, U64 = C.c_int64, C.c_uint32, C.c_uint64


class Wide(C.Structure):
    _fields_ = [('limb', U32 * 4), ('negative', U32)]


class Entity(C.Structure):
    _fields_ = [(n, I64) for n in FIELDS[:4]] + [(n, U64) for n in FIELDS[4:6]] + [
        (n, U32) for n in FIELDS[6:]]


class Queue(C.Structure):
    _fields_ = [('task', Entity * 32), ('now', U64), ('current', C.c_int32), ('need_reschedule', U32)]


def wide(x):
    return Wide((U32 * 4)(*[(abs(x) >> (32 * i)) & (2**32 - 1) for i in range(4)]), x < 0)


def integer(x):
    return sum(int(v) << (32 * i) for i, v in enumerate(x.limb)) * (-1 if x.negative else 1)


def bind(path):
    lib = C.CDLL(str(path))
    signatures = {
        'ee_from': ([I64], Wide), 'ee_mul': ([I64, U32], Wide),
        'ee_add': ([Wide, Wide, C.POINTER(Wide)], C.c_int),
        'ee_cmp': ([Wide, Wide], C.c_int),
        'ee_div': ([Wide, U32, C.c_int, C.POINTER(I64), C.POINTER(U32)], C.c_int),
        'ee_narrow': ([Wide, C.POINTER(I64)], C.c_int),
        'ee_init': ([C.POINTER(Queue)], None),
        'ee_apply': ([C.POINTER(Queue), U32, U32, C.c_int, U64, U64], C.c_int),
        'ee_eligible': ([C.POINTER(Queue), U32], C.c_int),
        'ee_weight': ([C.c_int], U32),
    }
    for name, (args, result) in signatures.items():
        fn = getattr(lib, name)
        fn.argtypes, fn.restype = args, result
    return lib


def arithmetic(lib):
    rng = random.Random(0xEE12)
    edges = [0, 1, -1, 2**31, -2**31, 2**32, -2**32, 2**63 - 1, -2**63]
    for x in edges + [rng.randrange(-2**63, 2**63) for _ in range(10000)]:
        assert integer(lib.ee_from(x)) == x
        y = rng.randrange(2**32)
        assert integer(lib.ee_mul(x, y)) == x * y
    for x in [0, -1, 1, -7, 7, -(2**63), 2**63 - 1, 2**63, -2**63 - 1,
              2**127, -(2**128 - 1)] + [rng.randrange(-2**127, 2**127) for _ in range(10000)]:
        y = rng.randrange(-2**127, 2**127)
        result = Wide()
        ok = lib.ee_add(wide(x), wide(y), C.byref(result))
        assert bool(ok) == (abs(x + y) < 2**128)
        if ok:
            assert integer(result) == x + y
            assert x + y or not result.negative
        assert lib.ee_cmp(wide(x), wide(y)) == ((x > y) - (x < y))
        n = I64(123)
        ok = lib.ee_narrow(wide(x), C.byref(n))
        assert bool(ok) == (-2**63 <= x < 2**63)
        if ok:
            assert n.value == x
        divisor = rng.choice([1, 3, 15, 88761, 2840352, 2**32 - 1])
        for floor in (0, 1):
            expected = x // divisor if floor else trunc(x, divisor)
            remainder = U32()
            ok = lib.ee_div(wide(x), divisor, floor, C.byref(n), C.byref(remainder))
            assert bool(ok) == (-2**63 <= expected < 2**63)
            if ok:
                assert n.value == expected and remainder.value == abs(x) % divisor
    result = Wide()
    assert not lib.ee_add(wide(2**128 - 1), wide(1), C.byref(result))
    n = I64()
    assert not lib.ee_div(wide(10), 0, 0, C.byref(n), None)
    for nice in range(-40, 41):
        assert lib.ee_weight(nice) == weight(nice)
    production = (ROOT / 'src/kernel/sched_policy.c').read_text()
    values = re.search(r'weights\[40\] = \{(.*?)\};', production, re.S).group(1)
    assert tuple(map(int, re.findall(r'\d+', values))) == WEIGHTS
    print('PASS wide arithmetic: signed edges, 20000 random vectors, floor/truncation, overflow')


class Harness:
    def __init__(self, lib):
        self.lib, self.q, self.ref = lib, Queue(), Oracle()
        self.lib.ee_init(C.byref(self.q))
        self.history = []

    def fixture(self, tasks, current=-1, now=0):
        self.ref = Oracle()
        self.lib.ee_init(C.byref(self.q))
        self.ref.current = self.q.current = current
        self.ref.now = self.q.now = now
        for i, values in tasks.items():
            values = entity(weight=1024, slice=4000000, member=1, state=READY) | values
            self.ref.task[i] = values.copy()
            for field in FIELDS:
                setattr(self.q.task[i], field, values[field])
        self.compare()

    def compare(self):
        assert self.q.now == self.ref.now
        assert self.q.current == self.ref.current
        assert self.q.need_reschedule == self.ref.need_reschedule
        for i, expected in enumerate(self.ref.task):
            actual = {n: getattr(self.q.task[i], n) for n in FIELDS}
            assert actual == expected, (i, actual, expected, self.history[-20:])
            assert bool(self.lib.ee_eligible(C.byref(self.q), i)) == self.ref.eligible(i)
        self.invariants()

    def invariants(self):
        participants = self.ref.participants()
        executable = self.ref.executable()
        assert len(participants) <= 32 and len(executable) <= len(participants)
        assert bool(executable) == bool(participants)
        assert sum(t['weight'] for t in self.ref.task if t['member']) <= 2840352
        for i, t in enumerate(self.ref.task):
            if t['member']:
                assert t['state'] not in (0, DEAD) and t['weight'] in WEIGHTS
                assert abs(t['v']) < 2**60 and abs(t['d']) < 2**60
                assert bool(t['delayed']) == (i not in executable)
                assert not t['relative_valid']
            else:
                assert not t['delayed'] and not t['protected_request'] and i not in executable
                assert t['v'] == t['d'] == 0
            if t['weight']:
                assert t['remainder'] < t['weight']
            if t['protected_request']:
                assert i == self.q.current and t['state'] == RUNNING
            if t['state'] == RUNNING:
                assert i == self.q.current
            if t['state'] != SLEEPING:
                assert t['wake_at'] == 0

    def apply(self, op, i=0, arg=0, value=0, now=None, expect=0):
        now = self.q.now if now is None else now
        self.history.append((op, i, arg, value, now))
        before = bytes(self.q)
        actual = self.lib.ee_apply(C.byref(self.q), op, i, arg, value, now)
        expected = self.ref.apply(op, i, arg, value, now)
        assert actual == expected == expect, (actual, expected, self.history[-20:])
        if actual:
            assert bytes(self.q) == before, 'rejected transaction changed state'
        self.compare()

    def create(self, i, nice=0, slice_ns=4000000):
        self.apply(CREATE, i, nice, slice_ns)


def targeted(lib):
    # At unchanged weight, clock sampling frequency cannot erase virtual service.
    for nice in range(-20, 20):
        whole, split = Harness(lib), Harness(lib)
        for sample in (whole, split):
            sample.create(0, nice)
            sample.create(1, nice)
            sample.apply(PICK)
        whole.apply(TICK, now=17)
        for now in range(1, 18):
            split.apply(TICK, now=now)
        for i in (0, 1):
            assert whole.q.task[i].v == split.q.task[i].v
            assert whole.q.task[i].remainder == split.q.task[i].remainder
            assert whole.q.task[i].runtime == split.q.task[i].runtime
    h = Harness(lib)
    h.apply(PICK)
    assert h.q.current == -1
    h.fixture({0: dict(v=8, d=14), 1: dict(v=9, d=11), 2: dict(v=13, d=15)})
    h.apply(PICK)
    assert h.q.current == 1, 'eligible earliest deadline differs from minimum vruntime'
    h.fixture({0: dict(v=-1, d=9, weight=15), 1: dict(v=0, d=8, weight=88761)})
    assert h.lib.ee_eligible(C.byref(h.q), 0) and not h.lib.ee_eligible(C.byref(h.q), 1)
    h.apply(PICK)
    assert h.q.current == 0

    # Current contributes to eligibility and wins unprotected deadline equality.
    h.fixture({0: dict(v=0, d=10), 1: dict(v=0, d=10, state=RUNNING)}, current=1)
    h.apply(PICK)
    assert h.q.current == 1
    h.fixture({0: dict(v=0, d=10), 1: dict(v=0, d=10)})
    h.apply(PICK)
    assert h.q.current == 0
    h.fixture({0: dict(v=0, d=100, state=RUNNING, protected_request=1),
               1: dict(v=0, d=1)}, current=0)
    h.apply(PICK)
    assert h.q.current == 0, 'protected current'

    h = Harness(lib)
    h.create(0)
    h.apply(PICK)
    h.create(1, slice_ns=1000000)
    assert h.q.need_reschedule and not h.q.task[0].protected_request
    h.apply(PICK)
    assert h.q.current == 1, 'short request cancels protection'
    before = h.q.task[1].d - h.q.task[1].v
    h.apply(TICK, now=10000000)
    assert h.q.task[1].d - h.q.task[1].v == 1000000 and before == 500000
    assert not h.q.task[1].protected_request, 'renew from actual vruntime after overshoot'

    h.fixture({0: dict(v=0, d=10000000, state=RUNNING, protected_request=1),
               1: dict(v=0, d=10000000)}, current=0)
    h.apply(NICE, 0, 0)
    assert h.q.task[0].protected_request, 'same weight preserves protection'
    deadline = h.q.task[0].d
    h.apply(YIELD)
    assert h.q.task[0].d == deadline + 4000000 and not h.q.task[0].protected_request
    h.apply(PICK)
    assert h.q.current == 1

    h.fixture({0: dict(v=2**60 - 2, d=2**60 - 1)})
    h.apply(PICK)
    assert h.q.task[0].v == 0 and h.q.task[0].d == 1, 'large origin rebase'

    h = Harness(lib)
    h.create(0, 19)
    h.apply(PICK)
    h.apply(TICK, now=1)
    assert h.q.task[0].remainder == 4
    h.apply(NICE, 0, -20)
    assert h.q.task[0].remainder == 4 * 88761 // 15
    for _ in range(20):
        h.apply(NICE, 0, 19)
        h.apply(NICE, 0, -20)
    h.apply(SLEEP, 0, value=10000000)
    h.apply(NICE, 0, 19)
    assert h.q.task[0].state == SLEEPING and not h.q.task[0].member
    h.apply(WAKE, 0)
    assert h.q.task[0].state == RUNNING, 'early wake current'

    # Negative debt stays resident and does not execute while blocked.
    h.fixture({0: dict(v=100, d=200, state=RUNNING), 1: dict(v=0, d=500)}, current=0)
    h.apply(SLEEP, 0, value=10000000)
    assert h.q.task[0].delayed and not h.q.task[0].protected_request
    saved = h.q.task[0].v, h.q.task[0].d
    h.apply(WAKE, 0)
    assert not h.q.task[0].delayed and (h.q.task[0].v, h.q.task[0].d) == saved
    h.apply(BLOCK, 0)
    h.apply(NICE, 0, -5)
    assert h.q.task[0].delayed and h.q.task[0].state == BLOCKED
    h.apply(PICK)
    assert h.q.current == 1
    h.apply(TICK, now=1000)
    h.apply(PICK)
    assert not h.q.task[0].member and h.q.task[0].state == BLOCKED

    # Positive delayed lag on early wake discards credit and repositions current.
    h.fixture({0: dict(v=-100, d=100, state=BLOCKED, delayed=1),
               1: dict(v=100, d=200)}, current=0)
    h.apply(WAKE, 0)
    assert h.q.task[0].v == h.q.task[1].v and h.q.task[0].state == RUNNING
    h.apply(BLOCK, 0)
    h.apply(BLOCK, 1)
    h.apply(PICK)
    assert h.q.current == -1 and not h.ref.participants()
    h.apply(WAKE, 0)
    assert h.q.task[0].v == 0

def lifecycle(lib):
    h = Harness(lib)
    h.fixture({0: dict(v=0, d=0, state=BLOCKED, member=0, lag=4),
               1: dict(v=100, d=200)})
    h.apply(WAKE, 0)
    assert h.q.task[1].v - h.q.task[0].v == 8
    assert h.ref.virtual_time() - h.q.task[0].v == 4
    h.apply(SLEEP, 0, value=1000)
    h.apply(SLEEP, 0, value=2000)
    assert h.q.task[0].wake_at == 2000
    h.apply(BLOCK, 0)
    h.apply(TICK, now=3000)
    assert h.q.task[0].state == BLOCKED and h.q.task[0].wake_at == 0
    h.apply(WAKE, 0)
    h.apply(WAKE, 0)
    h.apply(REMOVE, 0)
    relative = h.q.task[0].relative
    assert h.q.task[0].relative_valid
    h.apply(REMOVE, 0)
    h.apply(BLOCK, 0)
    assert h.q.task[0].relative_valid and h.q.task[0].relative == relative
    h.apply(NICE, 0, 19)
    h.apply(WAKE, 0)
    assert h.q.task[0].d - h.q.task[0].v == trunc(relative * 1024, 15)
    h.apply(EXIT, 0)
    h.create(0)
    h.apply(PICK)
    current = h.q.current
    h.apply(EXIT, current)
    h.create(current)
    assert h.q.task[current].state == DEAD, 'cannot overwrite suspended current stack slot'
    h.apply(PICK)
    h.create(current)
    assert h.q.task[current].state == READY

    h = Harness(lib)
    h.create(0)
    h.apply(PICK)
    h.apply(SLEEP, 0, value=0)
    h.apply(TICK, now=999999)
    assert h.q.task[0].state == SLEEPING
    h.apply(TICK, now=1000000)
    assert h.q.task[0].state == RUNNING
    before = h.q.task[0].runtime
    h.apply(TICK, now=1)
    assert h.q.now == 1000000 and h.q.task[0].runtime == before
    h.apply(YIELD)
    assert h.q.task[0].d - h.q.task[0].v == 4000000


def failures(lib):
    h = Harness(lib)
    h.create(0)
    h.apply(PICK)
    h.apply(CREATE, 1, value=999999, now=100, expect=1)
    h.apply(CREATE, 1, value=16000001, expect=1)
    h.apply(TICK, i=32, expect=1)
    h.apply(99, expect=1)
    h.apply(TICK, now=2**48, expect=2)
    h.apply(SLEEP, 0, value=2**64 - 1, now=1, expect=2)
    h.fixture({0: dict(v=0, d=10000000, state=RUNNING, runtime=2**64 - 2)}, current=0)
    h.apply(TICK, now=2, expect=2)
    h.fixture({0: dict(v=0, d=2**60 - 1, state=RUNNING)}, current=0)
    h.create(1)
    h.apply(YIELD, expect=2)
    # A valid state whose reweighting cannot store the scaled signed result.
    h.fixture({0: dict(v=0, d=2**60 - 1, weight=88761), 1: dict(v=0, d=100)})
    h.apply(NICE, 0, 19, expect=2)
    print('PASS targeted policy: eligibility, deadlines, protection, sleep/wake, reweight, rollback')


def randomized(lib):
    for seed in (12, 23, 612, 20261023):
        rng = random.Random(seed)
        h = Harness(lib)
        for i in range(8):
            h.create(i, rng.randrange(-20, 20), rng.choice((1000000, 4000000, 16000000)))
        h.apply(PICK)
        for _ in range(2500):
            op = rng.randrange(1, 11)
            h.apply(op, rng.randrange(8), arg=rng.randrange(-25, 25),
                    value=(rng.choice((1000000, 2000000, 4000000, 16000000)) if op == CREATE
                           else rng.randrange(0, 10000001)),
                    now=h.q.now + rng.randrange(0, 500001))
            if h.q.need_reschedule:
                h.apply(PICK)
    print('PASS 10000 seeded lifecycle events: full C/oracle state and eligibility after every transaction')


def build_and_run(native=False):
    with tempfile.TemporaryDirectory(prefix='nixel-eevdf-') as directory:
        build = Path(directory)
        sources = [str(ROOT / 'src/kernel' / name) for name in ('sched_wide.c', 'sched_policy.c')]
        common = ['-std=c11', '-O2', '-Wall', '-Wextra', '-Werror']
        shared = build / 'policy.so'
        subprocess.run(['gcc', *common, '-fPIC', '-shared', '-fsanitize=undefined',
                        '-fno-sanitize-recover=all', *sources, '-o', str(shared)], check=True)
        lib = bind(shared)
        arithmetic(lib)
        targeted(lib)
        lifecycle(lib)
        failures(lib)
        randomized(lib)
        from workloads import run
        run(lib, Queue)
        sanitized = build / 'sanitized'
        subprocess.run(['gcc', *common, '-fsanitize=address,undefined',
                        '-fno-sanitize-recover=all', '-fno-omit-frame-pointer',
                        *sources, str(HERE / 'native.c'), '-o', str(sanitized)], check=True)
        # No dynamic allocation is used. LeakSanitizer cannot run under the
        # sandbox debugger; address and undefined-behavior instrumentation stay on.
        environment = os.environ.copy()
        environment['ASAN_OPTIONS'] = 'detect_leaks=0'
        subprocess.run([str(sanitized)], env=environment, check=True)
        print('PASS ASan/UBSan: 20000 additional C lifecycle events across all 32 slots')
        objects = []
        for source in sources:
            obj = build / (Path(source).stem + '.o')
            subprocess.run(['gcc', *common, '-m32', '-ffreestanding', '-fno-pie',
                            '-fno-stack-protector', '-fstack-usage', '-c', source, '-o', str(obj)], check=True)
            objects.append(str(obj))
        combined = build / 'policy-i386.o'
        subprocess.run(['ld', '-m', 'elf_i386', '-r', *objects, '-o', str(combined)], check=True)
        undefined = subprocess.check_output(['nm', '-u', str(combined)], text=True)
        assert not undefined.strip(), undefined
        for path in build.glob('*.su'):
            for line in path.read_text().splitlines():
                assert int(line.split('\t')[1]) < 16384, line
        print('PASS freestanding i386: no undefined symbols; individual stack frames below 16 KiB')
        if native:
            executable = build / 'native-i386'
            subprocess.run(['gcc', *common, '-m32', '-ffreestanding', '-fno-pie',
                            '-fno-stack-protector', '-nostdlib', '-static', '-no-pie',
                            '-DEE_NATIVE', *sources, str(HERE / 'native.c'),
                            '-o', str(executable)], check=True)
            subprocess.run([str(executable)], check=True)
            print('PASS native i386: libc-free execution of arithmetic edges and 20000 lifecycle events')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--native', action='store_true',
                        help='also execute a libc-free i386 ELF; requires Linux IA32 support and int 0x80 permission')
    build_and_run(parser.parse_args().native)
