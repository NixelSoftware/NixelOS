"""Arbitrary-precision policy oracle. No C arithmetic or queue code is imported."""
from copy import deepcopy
from fractions import Fraction

UNUSED, READY, RUNNING, BLOCKED, SLEEPING, DEAD = range(6)
CREATE, TICK, PICK, BLOCK, SLEEP, WAKE, EXIT, NICE, YIELD, REMOVE = range(1, 11)
WEIGHTS = (88761, 71755, 56483, 46273, 36291, 29154, 23254, 18705, 14949, 11916,
           9548, 7620, 6100, 4904, 3906, 3121, 2501, 1991, 1586, 1277,
           1024, 820, 655, 526, 423, 335, 272, 215, 172, 137,
           110, 87, 70, 56, 45, 36, 29, 23, 18, 15)
FIELDS = ('v d lag relative runtime wake_at state weight slice remainder '
          'member delayed protected_request relative_valid').split()


def entity(**kwargs):
    return dict.fromkeys(FIELDS, 0) | kwargs


def weight(nice):
    return WEIGHTS[max(-20, min(nice, 19)) + 20]


def trunc(x, divisor):
    return (1 if x >= 0 else -1) * (abs(x) // divisor)


class Oracle:
    def __init__(self):
        self.task = [entity() for _ in range(32)]
        self.now = 0
        self.current = -1
        self.need_reschedule = 0

    def participants(self):
        return [i for i, t in enumerate(self.task) if t['member']]

    def executable(self):
        return [i for i, t in enumerate(self.task) if t['state'] in (READY, RUNNING)]

    def virtual_time(self):
        p = self.participants()
        return (Fraction(sum(self.task[i]['weight'] * self.task[i]['v'] for i in p),
                         sum(self.task[i]['weight'] for i in p)) if p else Fraction(0))

    def eligible(self, i):
        return bool(self.task[i]['member'] and self.task[i]['v'] <= self.virtual_time())

    def rebase(self):
        shift = self.virtual_time().__floor__()
        for i in self.participants():
            for key in ('v', 'd'):
                self.task[i][key] -= shift
                if abs(self.task[i][key]) >= 2**60:
                    raise OverflowError('coordinate')

    @staticmethod
    def fair(t):
        return t['slice'] * 1024 // t['weight']

    def lag(self, i, v=None):
        t = self.task[i]
        lag = (self.virtual_time().__floor__() if v is None else v) - t['v']
        limit = 2 * t['slice'] * 1024 // t['weight']
        return max(-limit, min(lag, limit))

    def detach(self, i):
        self.task[i].update(member=0, delayed=0, protected_request=0, v=0, d=0)

    def empty(self):
        if not self.executable():
            v = self.virtual_time().__floor__()
            for i in self.participants():
                self.task[i]['lag'] = min(0, self.lag(i, v))
                self.detach(i)

    def place(self, i, initial=False):
        t = self.task[i]
        p = self.participants()
        v = self.virtual_time().__floor__()
        w = sum(self.task[j]['weight'] for j in p)
        t['v'] = v - (trunc(t['lag'] * (w + t['weight']), w) if w else 0)
        request = t['relative'] if t['relative_valid'] else self.fair(t)
        t['d'] = t['v'] + (request // 2 if initial else request)
        t.update(relative=0, relative_valid=0, lag=0, member=1, delayed=0, protected_request=0)

    def winner(self):
        c = self.current
        if (c in self.executable() and self.task[c]['protected_request'] and self.eligible(c)):
            return c
        candidates = [i for i in self.participants() if self.eligible(i)]
        if not candidates:
            return -1
        best = min(candidates, key=lambda i: (self.task[i]['d'], i))
        if c in self.executable() and self.eligible(c) and self.task[c]['d'] == self.task[best]['d']:
            return c
        return best

    def arrival(self, i):
        c = self.current
        if c not in self.executable():
            self.need_reschedule = 1
        elif c != i:
            t, current = self.task[i], self.task[c]
            if (t['slice'] < current['slice'] and self.eligible(i)
                    and (t['d'] < current['d'] or not self.eligible(c))):
                current['protected_request'] = 0
            if self.winner() == i:
                self.need_reschedule = 1

    def wake(self, i):
        t = self.task[i]
        if t['state'] not in (BLOCKED, SLEEPING):
            return
        if t['delayed']:
            if self.lag(i) > 0:
                self.detach(i)
                t['lag'] = 0
                self.place(i)
            t['delayed'] = 0
        else:
            self.place(i)
        t.update(state=RUNNING if i == self.current else READY, wake_at=0)
        self.arrival(i)

    def update(self, now):
        now = max(self.now, now)
        dt = now - self.now
        if dt >= 2**48:
            raise OverflowError('elapsed')
        if self.current in self.executable():
            t = self.task[self.current]
            # Fraction independently expresses the quotient/remainder split.
            service = Fraction(dt * 1024 + t['remainder'], t['weight'])
            whole = service.__floor__()
            t['v'] += whole
            t['remainder'] = int((service - whole) * t['weight'])
            t['runtime'] += dt
            if t['runtime'] > 2**64 - 1:
                raise OverflowError('runtime')
            if t['v'] >= t['d']:
                t.update(d=t['v'] + self.fair(t), protected_request=0)
                if len(self.participants()) > 1:
                    self.need_reschedule = 1
            if not t['protected_request'] and not self.eligible(self.current) and len(self.participants()) > 1:
                self.need_reschedule = 1
        self.now = now
        self.rebase()
        for i, t in enumerate(self.task):
            if t['state'] == SLEEPING and t['wake_at'] <= now:
                self.wake(i)
                self.rebase()

    def block(self, i, remove=False):
        t = self.task[i]
        if t['state'] in (UNUSED, DEAD):
            return
        t.update(wake_at=0, protected_request=0)
        if t['member'] and (i in self.executable() or remove):
            t.update(relative_valid=0, relative=0)
            if remove or self.eligible(i):
                t['lag'] = self.lag(i)
                if remove:
                    t.update(relative=t['d'] - t['v'], relative_valid=1)
                self.detach(i)
            else:
                t['delayed'] = 1
        t['state'] = BLOCKED
        if self.current == i:
            self.need_reschedule = 1
        self.empty()

    def pick(self):
        for _ in range(33):
            best = self.winner()
            if best >= 0 and self.task[best]['delayed']:
                self.task[best]['lag'] = min(0, self.lag(best))
                self.detach(best)
                self.rebase()
                continue
            if self.current >= 0:
                old = self.task[self.current]
                old['protected_request'] = 0
                if old['state'] == RUNNING:
                    old['state'] = READY
            self.current = best
            if best >= 0:
                self.task[best].update(state=RUNNING, protected_request=1)
            self.need_reschedule = 0
            return
        raise AssertionError('selection failed to converge')

    def nice(self, i, new_weight):
        t = self.task[i]
        old = t['weight']
        if t['state'] in (UNUSED, DEAD) or old == new_weight:
            return
        remainder = t['remainder'] * new_weight // old
        if t['member']:
            v = self.virtual_time().__floor__()
            lag = self.lag(i)
            t['v'] = v - trunc(lag * old, new_weight)
            t['d'] = v + trunc((t['d'] - v) * old, new_weight)
            t['protected_request'] = 0
        else:
            limit = 2 * t['slice'] * 1024 // new_weight
            t['lag'] = max(-limit, min(trunc(t['lag'] * old, new_weight), limit))
            if t['relative_valid']:
                t['relative'] = trunc(t['relative'] * old, new_weight)
        t.update(weight=new_weight, remainder=remainder)
        self.need_reschedule = 1

    def apply(self, op, i=0, arg=0, value=0, now=0):
        if op not in range(1, 11) or i not in range(32):
            return 1
        if op == CREATE and not 1000000 <= value <= 16000000:
            return 1
        saved = deepcopy(self.__dict__)
        try:
            self.update(now)
            t = self.task[i]
            if op == CREATE:
                if t['state'] in (UNUSED, DEAD) and not t['member'] and self.current != i:
                    self.task[i] = entity(weight=weight(arg), slice=value)
                    self.place(i, True)
                    self.task[i]['state'] = READY
                    self.arrival(i)
            elif op == PICK:
                self.pick()
            elif op in (BLOCK, REMOVE):
                self.block(i, op == REMOVE)
            elif op == SLEEP:
                if t['state'] not in (UNUSED, DEAD):
                    duration = value or 1000000
                    if self.now + duration > 2**64 - 1:
                        raise OverflowError('deadline')
                    self.block(i)
                    t.update(state=SLEEPING, wake_at=self.now + duration)
            elif op == WAKE:
                self.wake(i)
            elif op == EXIT:
                if t['state'] not in (UNUSED, DEAD):
                    if self.current == i:
                        self.need_reschedule = 1
                    self.task[i] = entity(state=DEAD)
                    self.empty()
            elif op == NICE:
                self.nice(i, weight(arg))
            elif op == YIELD:
                if self.current in self.executable() and len(self.executable()) > 1:
                    t = self.task[self.current]
                    t['d'] += self.fair(t)
                    t['protected_request'] = 0
                    self.need_reschedule = 1
            self.rebase()
        except OverflowError:
            self.__dict__ = saved
            return 2
        return 0
