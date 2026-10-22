#include "sched_policy.h"

static const uint32_t weights[40] = {
    88761,71755,56483,46273,36291,29154,23254,18705,14949,11916,
    9548,7620,6100,4904,3906,3121,2501,1991,1586,1277,
    1024,820,655,526,423,335,272,215,172,137,
    110,87,70,56,45,36,29,23,18,15
};
uint32_t ee_weight(int nice) { return weights[(nice < -20 ? -20 : nice > 19 ? 19 : nice) + 20]; }
static int runnable(const ee_entity *t) { return t->state == EE_READY || t->state == EE_RUNNING; }
static int live(const ee_entity *t) { return t->state != EE_UNUSED && t->state != EE_DEAD; }
static int sum64(int64_t a, int64_t b, int64_t *out) {
    return !__builtin_add_overflow(a, b, out);
}
static int difference(int64_t a, int64_t b, int64_t *out) {
    return !__builtin_sub_overflow(a, b, out);
}
static int ratio(int64_t x, uint32_t n, uint32_t d, int64_t *out) {
    return ee_div(ee_mul(x, n), d, 0, out, 0);
}
static int64_t fair(uint32_t ns, uint32_t weight) {
    int64_t r = 0;
    (void)ratio(ns, 1024, weight, &r); /* bounded positive operands */
    return r;
}
static void snapshot(const ee_queue *q, ee_wide *a, uint32_t *w) {
    *a = ee_from(0); *w = 0;
    for (unsigned i = 0; i < EE_TASKS; ++i) if (q->task[i].member) {
        (void)ee_add(*a, ee_mul(q->task[i].v, q->task[i].weight), a);
        *w += q->task[i].weight;
    }
}
static int mean(const ee_queue *q, int64_t *v, uint32_t *weight) {
    ee_wide a;
    snapshot(q, &a, weight);
    if (!*weight) { *v = 0; return 1; }
    return ee_div(a, *weight, 1, v, 0);
}
int ee_eligible(const ee_queue *q, unsigned id) {
    if (id >= EE_TASKS || !q->task[id].member) return 0;
    ee_wide a; uint32_t w;
    snapshot(q, &a, &w);
    return ee_cmp(a, ee_mul(q->task[id].v, w)) >= 0;
}
/* One weighted snapshot per scan, rather than rebuilding it for each candidate.
 * At 32 static slots this is bounded O(n), with no tree allocation or fixups. */
static int candidate(const ee_queue *q) {
    ee_wide a; uint32_t w;
    snapshot(q, &a, &w);
    int c = q->current, best = -1;
    int current_eligible = c >= 0 && runnable(&q->task[c]) && q->task[c].member &&
        ee_cmp(a, ee_mul(q->task[c].v, w)) >= 0;
    if (current_eligible && q->task[c].protected_request) return c;
    for (unsigned i = 0; i < EE_TASKS; ++i) {
        const ee_entity *t = &q->task[i];
        if (!t->member || ee_cmp(a, ee_mul(t->v, w)) < 0) continue;
        if (best < 0 || t->d < q->task[best].d ||
            (t->d == q->task[best].d && i < (unsigned)best)) best = (int)i;
    }
    if (best >= 0 && current_eligible && q->task[c].d == q->task[best].d) best = c;
    return best;
}
static int rebase(ee_queue *q) {
    int64_t v; uint32_t w;
    if (!mean(q, &v, &w)) return 0;
    for (unsigned i = 0; i < EE_TASKS; ++i) if (q->task[i].member) {
        ee_entity *t = &q->task[i];
        if (v && (!difference(t->v, v, &t->v) || !difference(t->d, v, &t->d))) return 0;
        if (t->v <= -EE_BOUND || t->v >= EE_BOUND || t->d <= -EE_BOUND || t->d >= EE_BOUND) return 0;
    }
    return 1;
}
static int saved_lag(const ee_entity *t, int64_t v, int64_t *lag) {
    if (!difference(v, t->v, lag)) return 0;
    int64_t limit = fair(2 * t->slice, t->weight);
    if (*lag > limit) *lag = limit;
    if (*lag < -limit) *lag = -limit;
    return 1;
}
static unsigned executable(const ee_queue *q) {
    unsigned n = 0;
    for (unsigned i = 0; i < EE_TASKS; ++i) n += runnable(&q->task[i]);
    return n;
}
static unsigned participants(const ee_queue *q) {
    unsigned n = 0;
    for (unsigned i = 0; i < EE_TASKS; ++i) n += q->task[i].member;
    return n;
}
static void detach(ee_entity *t) {
    t->member = t->delayed = t->protected_request = 0;
    t->v = t->d = 0; /* detached entities retain durations only */
}
static int empty_cleanup(ee_queue *q) {
    if (executable(q)) return 1;
    int64_t v; uint32_t w;
    if (!mean(q, &v, &w)) return 0;
    for (unsigned i = 0; i < EE_TASKS; ++i) if (q->task[i].member) {
        ee_entity *t = &q->task[i];
        if (!saved_lag(t, v, &t->lag)) return 0;
        if (t->lag > 0) t->lag = 0;
        detach(t);
    }
    return 1;
}
static int place(ee_queue *q, unsigned id, int initial) {
    ee_entity *t = &q->task[id];
    int64_t v, offset = 0; uint32_t w;
    if (!mean(q, &v, &w)) return 0;
    if (w && !ratio(t->lag, w + t->weight, w, &offset)) return 0;
    if (!difference(v, offset, &t->v)) return 0;
    int64_t request = t->relative_valid ? t->relative : fair(t->slice, t->weight);
    if (initial) request /= 2;
    if (!sum64(t->v, request, &t->d)) return 0;
    t->relative = t->relative_valid = 0;
    t->lag = 0; t->member = 1; t->delayed = 0; t->protected_request = 0;
    return 1;
}
static void wake_preempt(ee_queue *q, unsigned id) {
    if (q->current < 0 || !runnable(&q->task[q->current])) { q->need_reschedule = 1; return; }
    ee_entity *c = &q->task[q->current], *t = &q->task[id];
    if (id == (unsigned)q->current) return;
    if (t->slice < c->slice && ee_eligible(q, id) &&
        (t->d < c->d || !ee_eligible(q, q->current))) c->protected_request = 0;
    /* Request if the arriving task is the actual winner, without dequeuing
     * delayed sleepers or installing a new protection marker in this query. */
    if (candidate(q) == (int)id) q->need_reschedule = 1;
}
static int wake(ee_queue *q, unsigned id) {
    ee_entity *t = &q->task[id];
    if (t->state != EE_SLEEPING && t->state != EE_BLOCKED) return 1;
    if (t->delayed) {
        int64_t v, lag; uint32_t w;
        if (!mean(q, &v, &w) || !saved_lag(t, v, &lag)) return 0;
        if (lag > 0) { detach(t); t->lag = 0; if (!place(q, id, 0)) return 0; }
        t->delayed = 0;
    } else if (!place(q, id, 0)) return 0;
    t->state = q->current == (int)id ? EE_RUNNING : EE_READY;
    t->wake_at = 0;
    wake_preempt(q, id);
    return 1;
}
static int update(ee_queue *q, uint64_t now) {
    if (now < q->now) now = q->now;
    uint64_t delta = now - q->now;
    if (delta >= (EE_U64_C(1) << 48)) return 0;
    if (q->current >= 0 && runnable(&q->task[q->current])) {
        ee_entity *t = &q->task[q->current];
        ee_wide n;
        int64_t service;
        if (delta > EE_U64_MAX - t->runtime ||
            !ee_add(ee_mul((int64_t)delta, 1024), ee_from(t->remainder), &n) ||
            !ee_div(n, t->weight, 0, &service, &t->remainder) ||
            !sum64(t->v, service, &t->v)) return 0;
        t->runtime += delta;
        if (t->v >= t->d) {
            if (!sum64(t->v, fair(t->slice, t->weight), &t->d)) return 0;
            t->protected_request = 0;
            /* Retained sleepers must reach selection to finish delayed dequeue,
             * even with just one executable task left. They compete in policy. */
            if (participants(q) > 1) q->need_reschedule = 1;
        }
        if (!t->protected_request && !ee_eligible(q, q->current) && participants(q) > 1)
            q->need_reschedule = 1;
    }
    q->now = now;
    if (!rebase(q)) return 0;
    for (unsigned i = 0; i < EE_TASKS; ++i)
        if (q->task[i].state == EE_SLEEPING && q->task[i].wake_at <= now)
            if (!wake(q, i) || !rebase(q)) return 0;
    return 1;
}
static int block(ee_queue *q, unsigned id, int remove) {
    ee_entity *t = &q->task[id];
    if (!live(t)) return 1;
    t->wake_at = 0; t->protected_request = 0;
    if (t->member && (runnable(t) || remove)) {
        t->relative_valid = 0; t->relative = 0;
        if (remove || ee_eligible(q, id)) {
            int64_t v; uint32_t w;
            if (!mean(q, &v, &w) || !saved_lag(t, v, &t->lag)) return 0;
            if (remove) {
                if (!difference(t->d, t->v, &t->relative)) return 0;
                t->relative_valid = 1;
            }
            detach(t);
        } else t->delayed = 1;
    }
    t->state = EE_BLOCKED;
    if (q->current == (int)id) q->need_reschedule = 1;
    return empty_cleanup(q);
}
static int pick(ee_queue *q) {
    for (unsigned retries = 0; retries <= EE_TASKS; ++retries) {
        int best = candidate(q), current = q->current;
        if (best >= 0 && q->task[best].delayed) {
            int64_t v; uint32_t w;
            ee_entity *t = &q->task[best];
            if (!mean(q, &v, &w) || !saved_lag(t, v, &t->lag)) return 0;
            if (t->lag > 0) t->lag = 0;
            detach(t);
            if (!rebase(q)) return 0;
            continue;
        }
        if (current >= 0) {
            q->task[current].protected_request = 0;
            if (q->task[current].state == EE_RUNNING) q->task[current].state = EE_READY;
        }
        q->current = best;
        if (best >= 0) { q->task[best].state = EE_RUNNING; q->task[best].protected_request = 1; }
        q->need_reschedule = 0;
        return 1;
    }
    return 0;
}
static int reweight(ee_queue *q, unsigned id, uint32_t w2) {
    ee_entity *t = &q->task[id];
    if (!live(t) || t->weight == w2) return 1;
    int64_t c2, v, lag, d2; uint32_t w;
    if (!ratio(t->remainder, w2, t->weight, &c2)) return 0;
    if (t->member) {
        if (!mean(q, &v, &w) || !saved_lag(t, v, &lag) ||
            !ratio(lag, t->weight, w2, &lag) || !difference(v, lag, &t->v) ||
            !difference(t->d, v, &d2) || !ratio(d2, t->weight, w2, &d2) ||
            !sum64(v, d2, &t->d)) return 0;
        t->protected_request = 0;
    } else {
        if (!ratio(t->lag, t->weight, w2, &t->lag)) return 0;
        int64_t limit = fair(2 * t->slice, w2);
        if (t->lag > limit) t->lag = limit;
        if (t->lag < -limit) t->lag = -limit;
        if (t->relative_valid && !ratio(t->relative, t->weight, w2, &t->relative)) return 0;
    }
    t->remainder = (uint32_t)c2; t->weight = w2; q->need_reschedule = 1;
    return 1;
}
void ee_init(ee_queue *q) { *q = (ee_queue){.current = -1}; }
int ee_apply(ee_queue *q, unsigned op, unsigned id, int arg, uint64_t value, uint64_t now) {
    if (op < EE_CREATE || op > EE_REMOVE || id >= EE_TASKS) return EE_INVALID;
    if (op == EE_CREATE && (value < 1000000 || value > 16000000)) return EE_INVALID;
    ee_queue copy = *q;
    if (!update(&copy, now)) return EE_RANGE;
    ee_entity *t = &copy.task[id];
    int ok = 1;
    switch (op) {
    case EE_CREATE:
        if (!live(t) && !t->member && copy.current != (int)id) {
            *t = (ee_entity){.weight = ee_weight(arg), .slice = (uint32_t)value};
            ok = place(&copy, id, 1);
            t->state = EE_READY;
            wake_preempt(&copy, id);
        }
        break;
    case EE_TICK: break;
    case EE_PICK: ok = pick(&copy); break;
    case EE_BLOCK: case EE_REMOVE: ok = block(&copy, id, op == EE_REMOVE); break;
    case EE_SLEEP:
        if (live(t)) {
            if (!value) value = 1000000;
            if (value > EE_U64_MAX - copy.now) return EE_RANGE;
            ok = block(&copy, id, 0);
            t->state = EE_SLEEPING; t->wake_at = copy.now + value;
        }
        break;
    case EE_WAKE: ok = wake(&copy, id); break;
    case EE_EXIT:
        if (live(t)) {
            if (copy.current == (int)id) copy.need_reschedule = 1;
            *t = (ee_entity){.state = EE_DEAD};
            ok = empty_cleanup(&copy);
        }
        break;
    case EE_NICE: ok = reweight(&copy, id, ee_weight(arg)); break;
    case EE_YIELD:
        if (copy.current >= 0 && executable(&copy) > 1) {
            ee_entity *c = &copy.task[copy.current];
            if (runnable(c)) {
                ok = sum64(c->d, fair(c->slice, c->weight), &c->d);
                c->protected_request = 0; copy.need_reschedule = 1;
            }
        }
        break;
    }
    if (!ok || !rebase(&copy)) return EE_RANGE;
    *q = copy;
    return EE_OK;
}
