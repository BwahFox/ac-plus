#!/usr/bin/env python3
"""City Folk's music, rendered for ac-plus's music player (pc/src/pc_music.c).

City Folk's background music is sequenced: note data (RSEQ) played on instrument banks (RBNK)
of recorded samples (RWAR, DSP-ADPCM), all inside Sound/rv_forest.brsar. This plays a sequence
the way the Wii's sound library does (tracks, tempo, volume, pan, the bank's key splits and
envelopes) and writes it as a WAV, with <name>.wav.loop holding the frame it loops back to.

    write_music(brsar bytes, out dir): cf_sky/music/roost.wav (BGM_CAFE, the Roost)

Needs numpy.
"""
import math
import os
import struct
import wave

import numpy as np

# sound name -> output file
SONGS = {'BGM_CAFE': 'roost'}


# --- the sound archive: names, sounds, banks, files and where they sit ---

def u8(b, o): return b[o]
def u16(b, o): return struct.unpack_from('>H', b, o)[0]
def u32(b, o): return struct.unpack_from('>I', b, o)[0]
def s32(b, o): return struct.unpack_from('>i', b, o)[0]


class Brsar:
    def __init__(self, b):
        self.b = b
        assert b[:4] == b'RSAR'
        self.symb, self.info, self.file = u32(b, 0x10), u32(b, 0x18), u32(b, 0x20)
        self.names = self._names()
        base = self.info + 8
        self.base = base
        refs = [self.ref(base + 8 * i) for i in range(6)]
        self.sounds = [self.ref_at(base, r) for r in self.table(base, refs[0])]
        self.banks = [self.ref_at(base, r) for r in self.table(base, refs[1])]
        self.files = [self.ref_at(base, r) for r in self.table(base, refs[3])]
        self.groups = [self.ref_at(base, r) for r in self.table(base, refs[4])]

    def ref(self, o):
        return u32(self.b, o + 4)

    def ref_at(self, base, off):
        return base + off

    def table(self, base, off):
        o = base + off
        n = u32(self.b, o)
        return [self.ref(o + 4 + 8 * i) for i in range(n)]

    def _names(self):
        b, o = self.b, self.symb + 8
        tbl = o + u32(b, o)
        n = u32(b, tbl)
        out = []
        for i in range(n):
            p = o + u32(b, tbl + 4 + 4 * i)
            out.append(b[p:b.index(b'\0', p)].decode())
        return out

    def sound(self, name):
        sid = self.names.index(name)
        for o in self.sounds:
            if u32(self.b, o) == sid:
                b = self.b
                d = {'file': u32(b, o + 4), 'player': u32(b, o + 8), 'volume': b[o + 0x14],
                     'type': b[o + 0x16], 'detail': self.base + self.ref(o + 0x18)}
                if d['type'] == 1:
                    de = d['detail']
                    d.update(offset=u32(b, de), bank=u32(b, de + 4), alloc=u32(b, de + 8))
                return d
        raise KeyError(name)

    def bank(self, i):
        o = self.banks[i]
        return {'name': self.names[u32(self.b, o)], 'file': u32(self.b, o + 4)}

    def file_data(self, fid):
        """(header bytes, wave data bytes) from the first group holding the file"""
        b = self.b
        fo = self.files[fid]
        hlen, dlen = u32(b, fo), u32(b, fo + 4)
        pos = self.base + self.ref(fo + 0x14)
        for r in [self.ref(pos + 4 + 8 * i) for i in range(u32(b, pos))]:
            p = self.base + r
            gid, idx = u32(b, p), u32(b, p + 4)
            g = self.groups[gid]
            goff, gwave = u32(b, g + 0x10), u32(b, g + 0x18)
            items = self.base + self.ref(g + 0x20)
            it = self.base + self.ref(items + 4 + 8 * idx)
            ioff, isize, iwoff, iwsize = u32(b, it + 4), u32(b, it + 8), u32(b, it + 12), u32(b, it + 16)
            return b[goff + ioff:goff + ioff + isize], b[gwave + iwoff:gwave + iwoff + iwsize]
        return None, None


# --- waves, banks, sequences ---

RATE = 32000
ENV_STEP = 0.003  # envelope update interval (s)


# --- waves ---

def dsp_adpcm(data, coefs, nibbles_end, ps0=0, h1=0, h2=0):
    out = []
    frames = (nibbles_end + 15) // 16
    for f in range(frames):
        o = f * 8
        if o >= len(data):
            break
        ps = data[o]
        scale = 1 << (ps & 15)
        c1, c2 = coefs[(ps >> 4) * 2], coefs[(ps >> 4) * 2 + 1]
        for i in range(14):
            byte = data[o + 1 + i // 2]
            n = byte >> 4 if i % 2 == 0 else byte & 15
            if n >= 8:
                n -= 16
            s = (n * scale << 11) + 1024 + c1 * h1 + c2 * h2
            s >>= 11
            s = max(-32768, min(32767, s))
            h2, h1 = h1, s
            out.append(s)
    return np.array(out, dtype=np.float32) / 32768.0


def nib2sample(n):
    return n // 16 * 14 + max(n % 16 - 2, 0)


def read_rwar(w):
    tabl = u32(w, 0x10)
    n = u32(w, tabl + 8)
    data_sec = u32(w, 0x18)
    waves = []
    for i in range(n):
        off = u32(w, tabl + 12 + 12 * i + 4)
        r = data_sec + off
        info = r + u32(w, r + 0x10)
        d = info + 8
        fmt, loop, ch = w[d], w[d + 1], w[d + 2]
        rate = w[d + 3] << 16 | u16(w, d + 4)
        ls, le = u32(w, d + 8), u32(w, d + 12)
        ct = d + u32(w, d + 16)
        ci = d + u32(w, ct)  # channel 0
        doff, aoff = u32(w, ci), u32(w, ci + 4)
        a = d + aoff
        coefs = struct.unpack_from('>16h', w, a)
        data_start = r + u32(w, r + 0x18) + 8 + doff
        raw = w[data_start:]
        if fmt == 0:  # PCM8; loop points in samples
            pcm = np.frombuffer(raw[:le + 1], dtype=np.int8).astype(np.float32) / 128.0
            waves.append({'pcm': pcm, 'rate': rate, 'loop': loop, 'ls': ls, 'le': le})
            continue
        if fmt == 1:  # PCM16
            pcm = np.frombuffer(raw[:2 * (le + 1)], dtype='>i2').astype(np.float32) / 32768.0
            waves.append({'pcm': pcm, 'rate': rate, 'loop': loop, 'ls': ls, 'le': le})
            continue
        if fmt != 2:
            raise ValueError('wave format %d' % fmt)
        pcm = dsp_adpcm(raw, coefs, le + 1)
        waves.append({'pcm': pcm, 'rate': rate, 'loop': loop, 'ls': nib2sample(ls), 'le': nib2sample(le)})
    return waves


# --- bank ---

def read_rbnk(b):
    data = u32(b, 0x10)
    base = data + 8
    n = u32(b, base)
    insts = []

    def region(off):
        o = base + off
        return {'wave': s32(b, o), 'attack': b[o + 4], 'decay': b[o + 5], 'sustain': b[o + 6],
                'release': b[o + 7], 'key': b[o + 12], 'volume': b[o + 13], 'pan': b[o + 14],
                'tune': struct.unpack_from('>f', b, o + 16)[0]}

    for i in range(n):
        r = base + 4 + 8 * i
        kind, off = b[r + 1], u32(b, r + 4)
        if kind == 1:
            insts.append([(127, region(off))])
        elif kind == 2:
            o = base + off
            cnt = b[o]
            keys = list(b[o + 1:o + 1 + cnt])
            rt = o + 1 + cnt
            rt += (-rt + base) % 4  # aligned from the data base
            regs = []
            for k in range(cnt):
                rr = rt + 8 * k
                regs.append((keys[k], region(u32(b, rr + 4))))
            insts.append(regs)
        elif kind == 3:
            o = base + off
            lo, hi = b[o], b[o + 1]
            regs = []
            for k in range(hi - lo + 1):
                rr = o + 4 + 8 * k
                regs.append((lo + k, region(u32(b, rr + 4))))
            insts.append(regs)
        else:
            insts.append(None)
    return insts


# --- envelope (as the DS/Wii sound libraries) ---

def env_rate(x):
    if x == 127: return 0xFFFF
    if x == 126: return 0x3C00
    if x < 50: return x * 2 + 1
    return 0x1E00 // (126 - x)


ATTACK = [0, 1, 5, 14, 26, 38, 51, 63, 73, 84, 92, 100, 109, 116, 123, 127, 132, 137, 143]


def attack_mul(x):
    if x >= 109:
        return ATTACK[127 - x] if 127 - x < len(ATTACK) else 0
    return 255 - x


def sustain_env(s):
    if s >= 127:
        return 0
    if s == 0:
        return -92544
    return max(-92544, int(20 * math.log10((s / 127.0) ** 2) * 10 * 128))


def env_amp(env):
    return 0.0 if env <= -92544 else 10 ** (env / 1280.0 / 20.0)


def vol2(v):
    return (v / 127.0) ** 2


# --- sequencer ---

class Track:
    def __init__(self, pc):
        self.pc = pc
        self.wait = 0
        self.prog = 0
        self.vol = 127
        self.expr = 127
        self.pan = 64
        self.transpose = 0
        self.bend = 0
        self.bend_range = 2
        self.note_wait = True
        self.stack = []
        self.loops = []
        self.done = False
        self.attack = self.decay = self.sustain = self.release = None
        self.jumped = 0


def varlen(d, p):
    v = 0
    while True:
        c = d[p]
        p += 1
        v = v << 7 | (c & 0x7F)
        if not c & 0x80:
            return v, p


def sequence(seq, start, loops=1):
    """-> notes [(t_ticks, track, key, vel, dur_ticks, track state copy)], tempo changes, loop tick"""
    data = u32(seq, 0x10)
    base = data + u32(seq, data + 8)
    d = seq
    tracks = {0: Track(base + start)}
    notes, tempos = [], []
    timebase = 48
    tick = 0
    loop_tick = None
    loop_start = None
    visits = {}
    unknown = set()
    while tick < 48 * 4 * 400:
        for ti in sorted(tracks):
            t = tracks[ti]
            if t.done:
                continue
            while not t.done and t.wait == 0:
                if ti == 0:
                    visits.setdefault(t.pc, tick)
                op = d[t.pc]; t.pc += 1
                if op < 0x80:
                    vel = d[t.pc]; t.pc += 1
                    dur, t.pc = varlen(d, t.pc)
                    notes.append((tick, ti, op + t.transpose, vel, dur, dict(prog=t.prog, vol=t.vol, expr=t.expr,
                                  pan=t.pan, bend=t.bend * t.bend_range / 127.0, adsr=(t.attack, t.decay, t.sustain, t.release))))
                    if t.note_wait:
                        t.wait = dur
                elif op == 0x80:
                    t.wait, t.pc = varlen(d, t.pc)
                elif op == 0x81:
                    t.prog, t.pc = varlen(d, t.pc)
                elif op == 0x88:
                    n = d[t.pc]; off = d[t.pc + 1] << 16 | d[t.pc + 2] << 8 | d[t.pc + 3]; t.pc += 4
                    tracks[n] = Track(base + off)
                elif op == 0x89:
                    off = d[t.pc] << 16 | d[t.pc + 1] << 8 | d[t.pc + 2]
                    t.pc = base + off
                    t.jumped += 1
                    if ti == 0 and loop_tick is None:
                        loop_tick = tick
                        loop_start = visits.get(t.pc, 0)
                    if t.jumped > loops:
                        t.done = True
                elif op == 0x8A:
                    off = d[t.pc] << 16 | d[t.pc + 1] << 8 | d[t.pc + 2]
                    t.stack.append(t.pc + 3); t.pc = base + off
                elif op == 0xFD:
                    t.pc = t.stack.pop()
                elif op == 0xFE:
                    t.pc += 2
                elif op == 0xFF:
                    t.done = True
                elif op == 0xB0:
                    timebase = d[t.pc]; t.pc += 1
                elif op in (0xC0, 0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xCB, 0xCC, 0xCD,
                            0xCE, 0xCF, 0xD0, 0xD1, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0xDB,
                            0xDC, 0xDD, 0xDE, 0xDF):
                    v = d[t.pc]; t.pc += 1
                    if op == 0xC0: t.pan = v
                    elif op == 0xC1: t.vol = v
                    elif op == 0xC3: t.transpose = v - 256 if v > 127 else v
                    elif op == 0xC4: t.bend = v - 256 if v > 127 else v
                    elif op == 0xC5: t.bend_range = v
                    elif op == 0xC7: t.note_wait = v != 0
                    elif op == 0xD0: t.attack = v
                    elif op == 0xD1: t.decay = v
                    elif op == 0xD2: t.sustain = v
                    elif op == 0xD3: t.release = v
                    elif op == 0xD4: t.loops.append([t.pc, v])
                    elif op == 0xD5: t.expr = v
                    elif op not in (0xC2, 0xC6, 0xD8, 0xD9, 0xDA, 0xDB, 0xDC, 0xDD, 0xDE, 0xCA, 0xCB, 0xCC, 0xCD, 0xCE, 0xCF, 0xC8, 0xC9, 0xDF, 0xD6, 0xD7):
                        unknown.add(hex(op))
                elif op in (0xE0, 0xE1, 0xE3):
                    v = struct.unpack_from('>h', d, t.pc)[0]; t.pc += 2
                    if op == 0xE1:
                        tempos.append((tick, v))
                elif op == 0xFC:
                    if t.loops:
                        lp = t.loops[-1]
                        if lp[1] == 0:
                            t.pc = lp[0]
                        else:
                            lp[1] -= 1
                            if lp[1] > 0: t.pc = lp[0]
                            else: t.loops.pop()
                else:
                    unknown.add(hex(op))
                    t.done = True
            if t.wait > 0:
                t.wait -= 1
        if all(t.done for t in tracks.values()):
            break
        tick += 1
    return notes, tempos, timebase, loop_start, loop_tick, unknown


# --- rendering ---

def tick_time(tempos, timebase):
    """tick -> seconds"""
    tempos = tempos or [(0, 120)]
    def f(tick):
        t, last_tick, bpm = 0.0, 0, 120
        for tk, b in tempos:
            if tk > tick:
                break
            t += (tk - last_tick) * 60.0 / (bpm * timebase)
            last_tick, bpm = tk, b
        return t + (tick - last_tick) * 60.0 / (bpm * timebase)
    return f


def voice(wave, ratio, n_out, off_at, adsr):
    """one note: mono samples, envelope applied; off_at: output sample of the note-off"""
    attack, decay, sustain, release = adsr
    pcm, ls, le, looped = wave['pcm'], wave['ls'], min(wave['le'], len(wave['pcm']) - 1), wave['loop']
    pos = np.arange(n_out, dtype=np.float64) * ratio
    if looped and le > ls:
        over = pos > le
        pos[over] = ls + np.mod(pos[over] - ls, le - ls)
    else:
        pos = np.minimum(pos, len(pcm) - 2)
    i = pos.astype(np.int64)
    fr = (pos - i).astype(np.float32)
    out = pcm[i] * (1 - fr) + pcm[np.minimum(i + 1, len(pcm) - 1)] * fr
    if not looped:
        out[np.arange(n_out) * ratio >= len(pcm) - 1] = 0
    # envelope, stepped
    step = int(RATE * ENV_STEP)
    env = np.zeros(n_out, dtype=np.float32)
    e = -92544.0
    phase = 0  # 0 attack 1 decay/sustain 2 release
    sus = sustain_env(sustain)
    dr, rr = env_rate(decay), env_rate(release)
    am = attack_mul(attack)
    for k in range(0, n_out, step):
        if k >= off_at and phase < 2:
            phase = 2
        if phase == 0:
            e = e * am / 256.0 if am else 0.0
            if attack == 127 or e > -1:
                e, phase = 0.0, 1
        elif phase == 1:
            e = max(e - dr, sus)
        else:
            e -= rr
        if e <= -92544:
            env[k:] = 0
            return out * env, k
        env[k:k + step] = env_amp(e)
    return out * env, n_out


def render(seq, start, bank, waves, tail=6.0):
    notes, tempos, tb, loop_start, loop_end, unknown = sequence(seq, start, loops=2)
    tt = tick_time(tempos, tb)
    if not notes:
        return np.zeros((1, 2), dtype=np.float32), None, None, unknown
    total = int((tt(max(n[0] + n[4] for n in notes)) + tail) * RATE)
    mix = np.zeros((total, 2), dtype=np.float32)
    for tick, ti, key, vel, dur, st in notes:
        inst = bank[st['prog']] if st['prog'] < len(bank) else None
        if not inst:
            continue
        reg = next((r for k, r in inst if key <= k), inst[-1][1])
        wave = waves[reg['wave']]
        adsr = tuple(a if a is not None else reg[n] for a, n in zip(st['adsr'], ('attack', 'decay', 'sustain', 'release')))
        ratio = 2 ** ((key - reg['key'] + st['bend']) / 12.0) * reg['tune'] * wave['rate'] / RATE
        t0 = int(tt(tick) * RATE)
        off = int((tt(tick + dur) - tt(tick)) * RATE)
        if dur == 0:  # a sound effect's one-shot: the whole sample, no note-off
            off = int(len(wave['pcm']) / ratio) + 1
            total = max(total, t0 + off)
            if len(mix) < total:
                mix = np.concatenate([mix, np.zeros((total - len(mix), 2), dtype=np.float32)])
        n = min(total - t0, off + int(RATE * 8))
        if n <= 0:
            continue
        smp, used = voice(wave, ratio, n, off, adsr)
        amp = vol2(vel) * vol2(st['vol']) * vol2(st['expr']) * vol2(reg['volume'])
        pan = min(max((reg['pan'] - 64) + (st['pan'] - 64), -64), 63) / 127.0 + 0.5
        l, r = math.cos(pan * math.pi / 2), math.sin(pan * math.pi / 2)
        mix[t0:t0 + used, 0] += smp[:used] * amp * l
        mix[t0:t0 + used, 1] += smp[:used] * amp * r
    if loop_start is None or loop_end is None:  # a sound effect: played once
        return mix, None, None, unknown
    return mix, int(tt(loop_start) * RATE), int(tt(loop_end) * RATE), unknown


def write_music(brsar_bytes, out_dir):
    ar = Brsar(brsar_bytes)
    os.makedirs(out_dir, exist_ok=True)
    for sound, name in SONGS.items():
        s = ar.sound(sound)
        seq, _ = ar.file_data(s['file'])
        bank_hdr, bank_waves = ar.file_data(ar.bank(s['bank'])['file'])
        mix, ls, le, unknown = render(seq, s['offset'], read_rbnk(bank_hdr), read_rwar(bank_waves))
        if unknown:
            print('%s: sequence commands not handled: %s' % (sound, ' '.join(sorted(unknown))))
        # intro, then the loop as heard the second time round (it carries the first pass's tails)
        out = np.concatenate([mix[:ls], mix[le:le + (le - ls)]])
        out = out / max(np.abs(out).max(), 1e-6) * 0.6
        path = os.path.join(out_dir, name + '.wav')
        w = wave.open(path, 'wb')
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes((out * 32767).astype('<i2').tobytes())
        w.close()
        with open(path + '.loop', 'w', newline='\n') as f:
            f.write('%d\n' % ls)
        print('wrote %s (%.1fs, loops from %.1fs)' % (path, len(out) / RATE, ls / RATE))


# City Folk's sound effects ac-plus borrows (pc/src/pc_sfx.c): sound, file name, peak level
SFX = [
    ('SE_FloatSink', 'bite', 0.45),  # the float pulled under as a fish bites
]


def write_sfx(brsar_bytes, out_dir):
    ar = Brsar(brsar_bytes)
    os.makedirs(out_dir, exist_ok=True)
    for sound, name, peak in SFX:
        s = ar.sound(sound)
        seq, _ = ar.file_data(s['file'])
        bank_hdr, bank_waves = ar.file_data(ar.bank(s['bank'])['file'])
        mix, _ls, _le, _unknown = render(seq, s['offset'], read_rbnk(bank_hdr), read_rwar(bank_waves), tail=0.2)
        loud = np.nonzero(np.abs(mix).max(axis=1) > 1e-4)[0]
        if not len(loud):
            print('%s: silent, skipped' % sound)
            continue
        out = mix[:loud[-1] + 1]
        out = out / max(np.abs(out).max(), 1e-6) * peak
        path = os.path.join(out_dir, name + '.wav')
        w = wave.open(path, 'wb')
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes((out * 32767).astype('<i2').tobytes())
        w.close()
        print('wrote %s (%.2fs)' % (path, len(out) / RATE))


if __name__ == '__main__':
    import sys
    write_music(open(sys.argv[1], 'rb').read(), sys.argv[2])
