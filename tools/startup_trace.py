#!/usr/bin/env python3
"""Capture the single-CPU startup trace through QMP, or analyze saved dumps."""
import argparse
import bisect
import collections
import json
from pathlib import Path
import socket
import struct
import subprocess
import time

EVENTS = 'CLOCK SWITCH_OUT SWITCH_IN TIMER_IN TIMER_OUT IRQ_OFF LOCK_ACQUIRE LOCK_RELEASE LOCK_WAIT LOCK_RESUME UART_TX UART_QUEUE RTC_IO PS2_IO POLL_IN POLL_OUT TASK_STATE PHASE_IN PHASE_OUT GAP'.split()
NAMES = dict(enumerate(EVENTS, 1))


def symbols(elf):
    result = {}
    for line in subprocess.check_output(['nm', '-S', str(elf)], text=True).splitlines():
        words = line.split()
        if len(words) == 4:
            try:
                result[words[3]] = (int(words[0], 16), int(words[1], 16))
            except ValueError:
                pass
    return result


class QMP:
    def __init__(self, path):
        self.socket = socket.socket(socket.AF_UNIX)
        self.socket.settimeout(15)
        self.socket.connect(str(path))
        self.file = self.socket.makefile('rwb', buffering=0)
        json.loads(self.file.readline())
        self.call('qmp_capabilities')

    def call(self, name, **arguments):
        self.file.write((json.dumps({'execute': name, 'arguments': arguments}) + '\n').encode())
        while True:
            line = self.file.readline()
            if not line:
                raise RuntimeError('QMP disconnected')
            reply = json.loads(line)
            if 'error' in reply:
                raise RuntimeError(reply['error'])
            if 'return' in reply:
                return reply['return']

    def dump(self, address, size, path):
        # pmemsave uses the identity-mapped physical addresses in this kernel.
        self.call('pmemsave', val=address, size=size, filename=str(path.resolve()))

    def close(self):
        self.file.close()
        self.socket.close()


def capture(args):
    syms = symbols(args.elf)
    Path(str(args.output.resolve()) + '.elf').write_bytes(args.elf.read_bytes())
    qmp = QMP(args.qmp)
    prefix = args.output.resolve()
    try:
        observation = None
        if args.observe_seconds:
            clocks = []
            address, size = syms['last_clock']
            for index in range(2):
                before = time.monotonic_ns()
                path = Path(str(prefix) + f'.clock{index}.bin')
                qmp.dump(address, size, path)
                after = time.monotonic_ns()
                clocks.append(((before + after) // 2, struct.unpack('<Q', path.read_bytes())[0]))
                if not index:
                    time.sleep(args.observe_seconds)
            observation = {'host_elapsed_ns': clocks[1][0] - clocks[0][0],
                           'guest_elapsed_ns': clocks[1][1] - clocks[0][1],
                           'note': 'Last sampled guest clock; approximate, includes QMP and tick sampling error.'}
        was_running = qmp.call('query-status')['running']
        if was_running:
            qmp.call('stop')
        try:
            qmp.dump(*syms['startup_trace_state'], Path(str(prefix) + '.state.bin'))
            qmp.dump(*syms['startup_trace_pages'], Path(str(prefix) + '.pages.bin'))
            state = Path(str(prefix) + '.state.bin').read_bytes()
            magic, version, record_size, capacity = struct.unpack_from('<4I', state)
            if (magic, version, record_size) != (0x54524345, 1, 32) or not 0 < capacity <= 262144 or capacity % 128:
                raise ValueError('ELF does not match the running trace image, or trace allocation failed')
            pages_raw = Path(str(prefix) + '.pages.bin').read_bytes()
            pages = list(struct.unpack_from(f'<{capacity // 128}I', pages_raw))
            with Path(str(prefix) + '.records.bin').open('wb') as records:
                index = 0
                while index < len(pages):
                    end = index + 1
                    while end < len(pages) and pages[end] == pages[end - 1] + 4096:
                        end += 1
                    part = Path(str(prefix) + '.part.bin')
                    qmp.dump(pages[index], (end - index) * 4096, part)
                    records.write(part.read_bytes())
                    part.unlink()
                    index = end
        finally:
            if was_running:
                qmp.call('cont')
        if observation:
            Path(str(prefix) + '.host-clock.json').write_text(json.dumps(observation, indent=2) + '\n')
        return prefix
    finally:
        qmp.close()


def analyze(prefix, elf=None):
    syms = symbols(elf) if elf else {}
    def symbol_name(address):
        matches = [(base, name) for name, (base, size) in syms.items() if base <= address < base + size]
        return max(matches)[1] if matches else None
    data = Path(str(prefix) + '.state.bin').read_bytes()
    header = struct.unpack_from('<12I3Q4I', data)
    (magic, version, record_size, capacity, next_slot, count, overwritten,
     recording, triggered, source, cycles_per_us, timer_count,
     max_timer_gap, max_irq_off, max_poll_gap, polls, gaps, over_10ms, finished) = header
    if (magic, version, record_size) != (0x54524345, 1, 32):
        raise ValueError('Unknown trace ABI; use the ELF belonging to the running image')
    if not capacity:
        raise ValueError('Trace storage allocation failed; use more guest RAM')
    if not 0 <= count <= capacity or not 0 <= next_slot < capacity:
        raise ValueError('Invalid ring indices')
    hist = struct.unpack_from('<1002I', data, 88)
    raw = Path(str(prefix) + '.records.bin').read_bytes()
    if len(raw) != capacity * record_size:
        raise ValueError('Incomplete record dump')
    start = next_slot if count == capacity else 0
    records = [struct.unpack_from('<QHHIIIII', raw, ((start + i) % capacity) * record_size)[:6] for i in range(count)]
    percentile = None
    if gaps:
        threshold = (99 * gaps + 99) // 100
        cumulative = 0
        for index, value in enumerate(hist):
            cumulative += value
            if cumulative >= threshold:
                percentile = {'upper_bound_ns': (index + 1) * 100000 if index < 1001 else None,
                              'at_least_ns': 100100000 if index == 1001 else None}
                break
    transitions = [(r[0], r[2]) for r in records if r[1] in (1, 3)]
    transition_times = [t for t, _ in transitions]
    def scheduled_overlap(begin, end, target):
        position = bisect.bisect_right(transition_times, begin) - 1
        owner = transitions[position][1] if position >= 0 else None
        cursor, total, unknown = begin, 0, 0
        for stamp, new_owner in transitions[position + 1:]:
            if stamp > end:
                break
            if owner == target:
                total += stamp - cursor
            if owner is None:
                unknown += stamp - cursor
            cursor, owner = stamp, new_owner
        if owner == target:
            total += end - cursor
        if owner is None:
            unknown += end - cursor
        return {'scheduled_overlap_ns': total, 'unknown_overlap_ns': unknown}
    timer_spans = []
    timer_begin = None
    for r in records:
        if r[1] == 4:
            timer_begin = r[0]
        elif r[1] == 5 and timer_begin is not None:
            timer_spans.append((timer_begin, r[0]))
            timer_begin = None
    timer_entries = [r[0] for r in records if r[1] == 4]
    timer_gaps = [(a, b) for a, b in zip(timer_entries, timer_entries[1:]) if b-a > 10000000]
    previous_polls = {}
    details = []
    for r in records:
        now, event, task, a, b, c = r
        if event != 15:
            continue
        previous = previous_polls.get(task)
        previous_polls[task] = r
        if previous is None or now - previous[0] <= 10000000:
            continue
        begin = previous[0]
        wall = now - begin
        cpu = ((b << 32) | a) - ((previous[4] << 32) | previous[3])
        inside = [v for v in records if begin <= v[0] <= now]
        occupancy = collections.Counter()
        pos = bisect.bisect_right(transition_times, begin) - 1
        owner = transitions[pos][1] if pos >= 0 else None
        cursor = begin
        for stamp, new_owner in transitions[pos + 1:]:
            if stamp > now:
                break
            occupancy[str(owner) if owner is not None else 'unknown'] += stamp - cursor
            cursor, owner = stamp, new_owner
        occupancy[str(owner) if owner is not None else 'unknown'] += now - cursor
        irq = []
        for v in records:
            if v[1] == 6:
                duration = v[4] | (v[5] << 32)
                if v[0] > begin and v[0] - duration < now:
                    irq.append({'task': v[2], 'caller': hex(v[3]), 'caller_function': symbol_name(v[3]), 'duration_ns': duration})
        phases = []
        opened = {}
        for v in records:
            if v[2] != task:
                continue
            if v[1] == 18:
                opened[v[3]] = v[0]
            elif v[1] == 19:
                entered = opened.pop(v[3], None)
                if entered is not None and v[0] > begin and entered < now:
                    phases.append({'phase': {1: 'rtc', 2: 'input', 3: 'event', 4: 'render'}.get(v[3]),
                                   'elapsed_ns': v[0] - entered})
        details.append({'start_ns': begin, 'end_ns': now, 'task': task, 'wall_ns': wall,
                        'scheduled_runtime_ns': cpu, 'offcpu_ns': max(0, wall - cpu),
                        'cpu_occupancy_ns': dict(occupancy), 'irq_off_regions': irq,
                        'timer_delivery_gaps': [{'start_ns': a, 'end_ns': b, 'duration_ns': b-a}
                                                for a, b in timer_gaps if b > begin and a < now],
                        'long_timer_handlers': [{'start_ns': a, 'end_ns': b, 'duration_ns': b-a}
                                                for a, b in timer_spans if b > begin and a < now and b-a > 1000000],
                        'task_states': [{'at_ns': v[0], 'state': {1: 'ready', 3: 'terminated', 4: 'sleeping', 5: 'blocked'}.get(v[4], v[4])}
                                        for v in inside if v[1] == 17 and v[3] == task],
                        'lock_waits': [{'lock': hex(v[3]), 'owner': v[4]} for v in inside if v[1] == 9 and v[2] == task],
                        'sync_uart_bytes': sum(v[1] == 11 and v[4] == 0 for v in inside),
                        'queued_uart_bytes': sum(v[1] == 12 for v in inside),
                        'rtc_accesses': sum(v[1] == 13 for v in inside),
                        'ps2_accesses': sum(v[1] == 14 for v in inside),
                        'long_io': [{'kind': NAMES[v[1]], 'port_or_register': hex(v[3]), 'duration_ns': v[5],
                                     **scheduled_overlap(v[0] - v[5], v[0], v[2])}
                                    for v in inside if v[1] in (11, 13, 14) and v[5] >= 1000000],
                        'phases': phases})
    report = {'profile': next((r[5] for r in records if r[1] == 1), None), 'clock': {0: 'boot events untimed', 1: 'TSC calibrated against PIT channel 2', 2: 'PIT fallback'}.get(source),
              'cycles_per_us': cycles_per_us, 'finished': bool(finished), 'recording': bool(recording),
              'triggered': bool(triggered), 'records': count, 'overwritten_records': overwritten,
              'timer_count': timer_count, 'max_timer_gap_ns': max_timer_gap,
              'max_retained_timer_handler_ns': max((b-a for a, b in timer_spans), default=0),
              'max_irq_off_ns': max_irq_off, 'polls': polls, 'gap_samples': gaps,
              'gaps_over_10ms': over_10ms, 'max_poll_gap_ns': max_poll_gap, 'p99_gap': percentile,
              'histogram': {f'{i * 100}-{(i + 1) * 100}us' if i < 1001 else '>=100100us': v for i, v in enumerate(hist) if v},
              'retained_sync_uart_tx_bytes': sum(r[1] == 11 and r[4] == 0 for r in records),
              'retained_queued_uart_tx_bytes': sum(r[1] == 11 and r[4] == 1 for r in records),
              'retained_uart_drops': sum(r[1] == 12 and r[4] == 1 for r in records),
              'classified_gaps': len(details), 'unclassified_gaps': max(0, over_10ms-len(details)),
              'retained_gap_details': details,
              'limits': ['The fixed-size paged ring records 12 seconds; overwritten history is reported.',
                         'Missing/overwritten intervals cannot be classified; histogram covers the full capture window.',
                         'Scheduled runtime includes interrupt service and host pauses while a task is current.',
                         'Long timer-delivery gaps alone do not distinguish IRQ masking from host/emulator pauses.',
                         'IRQ-off helper traces close at switches; timer entry/exit covers the assembly IRQ path.',
                         'Boot events before scheduler clock calibration have zero timestamps.',
                         'Tracing adds overhead; compare with STARTUP_TRACE=0 using the same workload.']}
    Path(str(prefix) + '.json').write_text(json.dumps(report, indent=2) + '\n')
    with Path(str(prefix) + '.csv').open('w') as out:
        out.write('ns,event,task,a,b,c\n')
        for stamp, event, task, a, b, c in records:
            out.write(f'{stamp},{NAMES.get(event,event)},{task},{a},{b},{c}\n')
    print(json.dumps({k: v for k, v in report.items() if k not in ('histogram', 'retained_gap_details', 'limits')}, indent=2))
    for detail in details:
        print(json.dumps(detail, indent=2))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    cap = commands.add_parser('capture')
    cap.add_argument('--qmp', type=Path, required=True)
    cap.add_argument('--elf', type=Path, default=Path('src/kernel/kernel.trace.elf'))
    cap.add_argument('--output', type=Path, required=True)
    cap.add_argument('--observe-seconds', type=float, default=0)
    ana = commands.add_parser('analyze')
    ana.add_argument('prefix', type=Path)
    ana.add_argument('--elf', type=Path)
    args = parser.parse_args()
    if args.command == 'capture':
        analyze(capture(args), args.elf)
    else:
        analyze(args.prefix, args.elf)


if __name__ == '__main__':
    main()
