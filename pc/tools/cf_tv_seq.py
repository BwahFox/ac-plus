#!/usr/bin/env python3
"""Offline renderer for City Folk's interactive TV sequences (rv_forest.brsar SE_TV_* / TV_SEQ_*).

Extends pc/tools/cf_music.py's approach with what those sequences use and cf_music's sequencer lacks:
prefix commands (0xA0 random, 0xA1 variable, 0xA2 if, 0xA3-0xA5 time), the 0xF0 variable/compare
commands, tie mode (0xC8, legato notes that re-pitch the held voice), and game-side writes to the
sequence's variables and track volumes at given ticks (what SoundTv_* does in main.dol).

cf_tv_sound.py plays them as City Folk's SoundTv_* classes do; this renders one sound.

usage: cf_tv_seq.py <rv_forest.brsar> OUTDIR [SOUND[:seconds] ...]
"""
import math
import os
import random
import struct
import sys
import wave

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cf_music import (Brsar, read_rbnk, read_rwar, env_rate, attack_mul, sustain_env, env_amp, vol2,
                      u32, RATE, ENV_STEP)


def varlen(d, p):
    v = 0
    while True:
        c = d[p]
        p += 1
        v = v << 7 | (c & 0x7F)
        if not c & 0x80:
            return v, p


class Track:
    def __init__(self, idx, pc):
        self.idx, self.pc = idx, pc
        self.wait = 0
        self.prog = 0
        self.vol = 127
        self.expr = 127
        self.pan = 64
        self.transpose = 0
        self.bend = 0
        self.bend_range = 2
        self.note_wait = True
        self.tie = False
        self.cmp = True
        self.calls = []
        self.loops = []
        self.done = False
        self.adsr = [None, None, None, None]
        self.tvars = [0] * 16
        self.held = None  # the voice a tie keeps going


class Seq:
    """Ticks a sequence, collecting voices. writes: {tick: [(var, value)]}; tvol: {tick: [(mask, vol)]}"""

    def __init__(self, seq, start, writes=None, tvol=None, seed=1):
        data = u32(seq, 0x10)
        self.base = data + u32(seq, data + 8)
        self.d = seq
        self.tracks = {0: Track(0, self.base + start)}
        self.lvars = [0] * 16
        self.gvars = [0] * 16
        self.voices = []
        self.tempos = [(0, 120)]
        self.timebase = 48
        self.writes = writes or {}
        self.tvol = tvol or {}
        self.track_volume = [1.0] * 16
        self.tvol_hist = [[(0, 1.0)] for _ in range(16)]
        self.rng = random.Random(seed)
        self.unknown = set()
        self.var_log = []  # (tick, var, value) written by the sequence itself

    def getvar(self, t, i):
        if i < 16: return self.lvars[i]
        if i < 32: return self.gvars[i - 16]
        return t.tvars[i - 32]

    def setvar(self, t, i, v, tick=None):
        v = max(-32768, min(32767, int(v)))
        if i < 16: self.lvars[i] = v
        elif i < 32: self.gvars[i - 16] = v
        else: t.tvars[i - 32] = v
        if tick is not None:
            self.var_log.append((tick, i, v))

    def step(self, t, tick):
        d = self.d
        op = d[t.pc]; t.pc += 1
        mode = None
        cond = True
        while op in (0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5):
            if op == 0xA2:
                cond = t.cmp
            else:
                mode = op
            op = d[t.pc]; t.pc += 1

        def arg(kind):
            if mode == 0xA0:
                lo, hi = struct.unpack_from('>hh', d, t.pc); t.pc += 4
                return lo + self.rng.randrange(hi - lo + 1) if hi >= lo else lo
            if mode == 0xA1:
                v = self.getvar(t, d[t.pc]); t.pc += 1
                return v
            if kind == 'vl':
                v, t.pc = varlen(d, t.pc); return v
            if kind == 'u8':
                v = d[t.pc]; t.pc += 1; return v
            if kind == 's8':
                v = d[t.pc]; t.pc += 1; return v - 256 if v > 127 else v
            if kind == 's16':
                v = struct.unpack_from('>h', d, t.pc)[0]; t.pc += 2; return v
            if kind == 'u24':
                v = d[t.pc] << 16 | d[t.pc + 1] << 8 | d[t.pc + 2]; t.pc += 3; return v

        def skip_time():
            if mode == 0xA3: t.pc += 2
            elif mode == 0xA4: t.pc += 4
            elif mode == 0xA5: t.pc += 1

        if op < 0x80:
            vel = d[t.pc]; t.pc += 1
            ln = arg('vl')
            skip_time()
            if not cond:
                return
            key = op + t.transpose
            if t.tie and t.held is not None and not t.held['closed']:
                t.held['segs'].append((tick, key, vel, t.bend * t.bend_range / 127.0, t.vol, t.expr, t.pan))
                t.held['off'] = tick + ln if ln else None
            else:
                if t.held is not None and not t.held['closed']:
                    t.held['closed'] = True
                v = {'track': t.idx, 'start': tick, 'prog': t.prog, 'adsr': tuple(t.adsr), 'closed': False,
                     'segs': [(tick, key, vel, t.bend * t.bend_range / 127.0, t.vol, t.expr, t.pan)],
                     'off': tick + ln if ln else None}
                self.voices.append(v)
                t.held = v if t.tie else None
            if t.note_wait:
                t.wait = ln
            return
        if op == 0x80:
            v = arg('vl'); skip_time()
            if cond: t.wait = v
            return
        if op == 0x81:
            v = arg('vl'); skip_time()
            if cond: t.prog = v
            return
        if op == 0x88:
            n = d[t.pc]; off = d[t.pc + 1] << 16 | d[t.pc + 2] << 8 | d[t.pc + 3]; t.pc += 4
            if cond: self.tracks[n] = Track(n, self.base + off)
            return
        if op in (0x89, 0x8A):
            off = arg('u24')
            if not cond: return
            if op == 0x8A: t.calls.append(t.pc)
            t.pc = self.base + off
            return
        if 0xB0 <= op <= 0xDF:
            s8ops = (0xC3, 0xC4, 0xCC)
            v = arg('s8' if op in s8ops else 'u8'); skip_time()
            if not cond: return
            if op == 0xB0: self.timebase = v
            elif op == 0xC0: t.pan = v
            elif op == 0xC1: t.vol = v
            elif op == 0xC3: t.transpose = v
            elif op == 0xC4: t.bend = v
            elif op == 0xC5: t.bend_range = v
            elif op == 0xC7: t.note_wait = v != 0
            elif op == 0xC8:
                t.tie = v != 0
                if not t.tie and t.held is not None:
                    t.held['closed'] = True
                    if t.held['off'] is None: t.held['off'] = tick
                    t.held = None
            elif op == 0xD0: t.adsr[0] = v
            elif op == 0xD1: t.adsr[1] = v
            elif op == 0xD2: t.adsr[2] = v
            elif op == 0xD3: t.adsr[3] = v
            elif op == 0xD4: t.loops.append([t.pc, v])
            elif op == 0xD5: t.expr = v
            return
        if op in (0xE0, 0xE1, 0xE3):
            v = arg('s16'); skip_time()
            if cond and op == 0xE1: self.tempos.append((tick, v))
            return
        if op == 0xF0:
            sub = d[t.pc]; idx = d[t.pc + 1]; t.pc += 2
            if sub == 0xE0:  # userproc u16
                t.pc += 0  # idx consumed a byte of the u16
                t.pc += 1
                return
            v = arg('s16'); skip_time()
            if not cond: return
            cur = self.getvar(t, idx)
            if sub == 0x80: self.setvar(t, idx, v, tick)
            elif sub == 0x81: self.setvar(t, idx, cur + v, tick)
            elif sub == 0x82: self.setvar(t, idx, cur - v, tick)
            elif sub == 0x83: self.setvar(t, idx, cur * v, tick)
            elif sub == 0x84: self.setvar(t, idx, int(cur / v) if v else cur, tick)
            elif sub == 0x85: self.setvar(t, idx, cur << v if v >= 0 else cur >> -v, tick)
            elif sub == 0x86: self.setvar(t, idx, self.rng.randrange(v) if v > 0 else -self.rng.randrange(-v) if v < 0 else 0, tick)
            elif sub == 0x87: self.setvar(t, idx, cur & v, tick)
            elif sub == 0x88: self.setvar(t, idx, cur | v, tick)
            elif sub == 0x89: self.setvar(t, idx, cur ^ v, tick)
            elif sub == 0x8A: self.setvar(t, idx, ~v, tick)
            elif sub == 0x8B: self.setvar(t, idx, cur % v if v else cur, tick)
            elif sub == 0x90: t.cmp = cur == v
            elif sub == 0x91: t.cmp = cur >= v
            elif sub == 0x92: t.cmp = cur > v
            elif sub == 0x93: t.cmp = cur <= v
            elif sub == 0x94: t.cmp = cur < v
            elif sub == 0x95: t.cmp = cur != v
            else: self.unknown.add('f0%02x' % sub)
            return
        if op == 0xFB:
            return
        if op == 0xFC:
            if cond and t.loops:
                lp = t.loops[-1]
                if lp[1] == 0:
                    t.pc = lp[0]
                else:
                    lp[1] -= 1
                    if lp[1] > 0: t.pc = lp[0]
                    else: t.loops.pop()
            return
        if op == 0xFD:
            if cond and t.calls: t.pc = t.calls.pop()
            return
        if op == 0xFE:
            t.pc += 2
            return
        if op == 0xFF:
            if cond:
                t.done = True
                if t.held is not None:
                    t.held['closed'] = True
            return
        self.unknown.add(hex(op))
        t.done = True

    def run(self, max_ticks):
        """writes / tvol / mutes: lists of (seconds, ...) sorted by time, applied when the sequence reaches them"""
        now = 0.0
        wi = ti_ = 0
        writes = sorted(self.writes) if isinstance(self.writes, list) else \
            sorted((k / 96.0, a, b) for k, l in self.writes.items() for a, b in l)
        tvol = sorted(self.tvol) if isinstance(self.tvol, list) else \
            sorted((k / 96.0, a, b) for k, l in self.tvol.items() for a, b in l)
        self.now_of_tick = []
        for tick in range(max_ticks):
            self.now_of_tick.append(now)
            while wi < len(writes) and writes[wi][0] <= now:
                self.setvar(self.tracks[0], writes[wi][1], writes[wi][2]); wi += 1
            while ti_ < len(tvol) and tvol[ti_][0] <= now:
                _, mask, vol = tvol[ti_]; ti_ += 1
                for i in range(16):
                    if mask >> i & 1:
                        if vol == 'mute':
                            self.track_volume[i] = 0.0
                            if i in self.tracks: self.tracks[i].done = True
                        else:
                            self.track_volume[i] = vol
            for ti in sorted(self.tracks):
                t = self.tracks[ti]
                guard = 0
                while not t.done and t.wait == 0:
                    self.step(t, tick)
                    guard += 1
                    if guard > 10000:
                        t.done = True
                if t.wait > 0:
                    t.wait -= 1
            for i in range(16):
                h = self.tvol_hist[i]
                if h[-1][1] != self.track_volume[i]:
                    h.append((tick, self.track_volume[i]))
            if all(t.done for t in self.tracks.values()):
                return tick + 1
            now += 60.0 / (self.tempos[-1][1] * self.timebase)
        return max_ticks


def tick_time(tempos, timebase):
    tempos = sorted(tempos)
    def f(tick):
        t, last, bpm = 0.0, 0, 120
        for tk, b in tempos:
            if tk > tick: break
            t += (tk - last) * 60.0 / (bpm * timebase)
            last, bpm = tk, b
        return t + (tick - last) * 60.0 / (bpm * timebase)
    return f


def envelope(n, off_at, adsr):
    attack, decay, sustain, release = adsr
    step = int(RATE * ENV_STEP)
    env = np.zeros(n, dtype=np.float32)
    e = -92544.0
    phase = 0
    sus = sustain_env(sustain)
    dr, rr = env_rate(decay), env_rate(release)
    am = attack_mul(attack)
    k = 0
    while k < n:
        if off_at is not None and k >= off_at and phase < 2:
            phase = 2
        if phase == 0:
            e = e * am / 256.0 if am else 0.0
            if attack == 127 or e > -1:
                e, phase = 0.0, 1
        elif phase == 1:
            e = max(e - dr, sus)
            if e == sus:  # held at the sustain level until the note is let go: fill it in one go
                end = n if off_at is None else min(n, max(off_at, k + step))
                end = k + (end - k + step - 1) // step * step
                env[k:end] = env_amp(e)
                k = end
                continue
        else:
            e -= rr
        if e <= -92544:
            return env, k
        env[k:k + step] = env_amp(e)
        k += step
    return env, n


def sequence(name, seconds=120, writes=None, tvol=None, seed=1, ar=None):
    """Only the sequencer run (no audio): the Seq and the ticks it ran"""
    s = ar.sound(name)
    seq, _ = ar.file_data(s['file'])
    sq = Seq(seq, s['offset'], writes, tvol, seed)
    return sq, sq.run(int(seconds * 96 * 4))


def render(name, seconds=None, writes=None, tvol=None, seed=1, ar=None, cache={}):
    s = ar.sound(name)
    seq, _ = ar.file_data(s['file'])
    bk = ar.bank(s['bank'])
    if bk['file'] not in cache:
        h, w = ar.file_data(bk['file'])
        cache[bk['file']] = (read_rbnk(h), read_rwar(w))
    bank, waves = cache[bk['file']]
    limit_s = seconds or 120
    sq = Seq(seq, s['offset'], writes, tvol, seed)
    ticks = sq.run(int(limit_s * 96 * 4))  # generous tick budget (tempo can exceed 120)
    tt = tick_time(sq.tempos, sq.timebase)
    end_t = tt(ticks) if seconds is None else seconds
    total = int((end_t + 8) * RATE)
    mix = np.zeros((total, 2), dtype=np.float32)
    for v in sq.voices:
        inst = bank[v['prog']] if v['prog'] < len(bank) else None
        if not inst:
            continue
        key0 = v['segs'][0][1]
        reg = next((r for k, r in inst if key0 <= k), inst[-1][1])
        wv = waves[reg['wave']]
        adsr = tuple(a if a is not None else reg[n] for a, n in zip(v['adsr'], ('attack', 'decay', 'sustain', 'release')))
        t0 = int(tt(v['start']) * RATE)
        if t0 >= total:
            continue
        pcm, ls, le, looped = wv['pcm'], wv['ls'], min(wv['le'], len(wv['pcm']) - 1), wv['loop']
        if v['off'] is None and not v['closed']:
            off = total - t0  # held to the end (len 0 on a looped wave, or an open tie)
        elif v['off'] is None:
            off = None  # one-shot: whole sample
        else:
            off = int(tt(v['off']) * RATE) - t0
        n = total - t0 if off is None else min(total - t0, off + int(RATE * 4))
        # per-sample pitch ratio and amplitude from the segments
        ratio = np.empty(n, dtype=np.float64)
        amp = np.empty(n, dtype=np.float32)
        panv = np.empty(n, dtype=np.float32)
        segs = v['segs']
        for i, (tk, key, vel, bend, vol, expr, pan) in enumerate(segs):
            a = max(0, int(tt(tk) * RATE) - t0)
            b = n if i + 1 == len(segs) else max(0, int(tt(segs[i + 1][0]) * RATE) - t0)
            if a >= n: break
            ratio[a:b] = 2 ** ((key - reg['key'] + bend) / 12.0) * reg['tune'] * wv['rate'] / RATE
            amp[a:b] = vol2(vel) * vol2(vol) * vol2(expr) * vol2(reg['volume'])
            panv[a:b] = min(max((reg['pan'] - 64) + (pan - 64), -64), 63) / 127.0 + 0.5
        hist = sq.tvol_hist[v['track']]
        cur = [x for x in hist if x[0] <= v['start']][-1][1]
        tvg = np.full(n, cur, dtype=np.float32)
        for tk, tv in hist:
            if tk <= v['start']: continue
            a = max(0, int(tt(tk) * RATE) - t0)
            if a >= n: break
            tvg[a:] = tv
        amp *= tvg
        pos = np.concatenate([[0.0], np.cumsum(ratio[:-1])])
        if looped and le > ls:
            over = pos > le
            pos[over] = ls + np.mod(pos[over] - ls, le - ls)
            ended = np.zeros(n, dtype=bool)
        else:
            ended = pos >= len(pcm) - 1
            pos = np.minimum(pos, len(pcm) - 2)
        i = pos.astype(np.int64)
        fr = (pos - i).astype(np.float32)
        out = pcm[i] * (1 - fr) + pcm[np.minimum(i + 1, len(pcm) - 1)] * fr
        out[ended] = 0
        env, used = envelope(n, off, adsr)
        out = out * env * amp
        l, r = np.cos(panv * math.pi / 2), np.sin(panv * math.pi / 2)
        mix[t0:t0 + used, 0] += out[:used] * l[:used]
        mix[t0:t0 + used, 1] += out[:used] * r[:used]
    if seconds is not None:
        mix = mix[:int(seconds * RATE)]
    else:
        loud = np.nonzero(np.abs(mix).max(axis=1) > 1e-4)[0]
        mix = mix[:loud[-1] + 1] if len(loud) else mix[:1]
    return mix * (s['volume'] / 127.0), sq


def write_wav(path, mix, gain=1.0):
    out = np.clip(mix * gain, -1, 1)
    w = wave.open(path, 'wb')
    w.setnchannels(2); w.setsampwidth(2); w.setframerate(RATE)
    w.writeframes((out * 32767).astype('<i2').tobytes())
    w.close()


if __name__ == '__main__':
    ar = Brsar(open(sys.argv[1], 'rb').read())
    out = sys.argv[2]
    os.makedirs(out, exist_ok=True)
    for spec in sys.argv[3:]:
        name, _, secs = spec.partition(':')
        mix, sq = render(name, float(secs) if secs else None, ar=ar)
        peak = float(np.abs(mix).max()) if len(mix) else 0
        write_wav(os.path.join(out, name + '.wav'), mix, 0.6 / max(peak, 1e-6))
        print('%-22s %6.2fs peak %.3f voices %d %s' % (name, len(mix) / RATE, peak, len(sq.voices),
                                                        ' '.join(sorted(sq.unknown))))
