#!/usr/bin/env python3
"""City Folk's TV program sound (pc/src/pc_tv.c), for cf_sky/tv/: each program's sound over a whole cycle
of its picture, as City Folk's SoundTv_* classes (main.dol 0x801d0550-0x801d4200) play it. Their
sequences (rv_forest.brsar SE_TV_* / TV_SEQ_*) are rendered by cf_tv_seq.py.

usage: cf_tv_sound.py <City Folk files folder> OUTDIR      (extract_cf_sky.py calls write_tv_sound)

Two sound slots like the game's SoundHandle pair: L (this+0x2c) and S (this+0x30); each is also its own
one-voice player (PLAYER_TV_L / PLAYER_TV_S), so a new sound on a slot replaces the old one. The
programs that roll dice (hero, variety, soccer) are one take each.
"""
import os
import random
import sys
import wave

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cf_tv_seq import render, sequence, RATE, Brsar  # noqa: E402

FPS = 60.0
AR = None  # the City Folk sound archive, set by write_tv_sound
_natural = {}


def natural_length(name):
    """seconds a sound lasts by itself, or None if it never ends (loops / waits on variables)"""
    if name not in _natural:
        sq, _ = sequence(name, ar=AR)  # synthesizing a sound that never ends would take minutes
        ends = all(t.done for t in sq.tracks.values())
        if ends:
            mix, sq = render(name, None, ar=AR)
        _natural[name] = (len(mix) / RATE if ends else None, sq)
    return _natural[name][0]


class Inst:
    def __init__(self, name, t):
        self.name, self.t0, self.t1 = name, t, None
        self.writes, self.tvol, self.vols = [], [], []  # times relative to t0
        self.stop_fade = 0.0

    def playing(self, t):
        if self.t1 is not None and t >= self.t1:
            return False
        n = natural_length(self.name)
        return n is None or t < self.t0 + n


class TV:
    def __init__(self, seed=1):
        self.slot = {'L': None, 'S': None}
        self.done = []
        self.rng = random.Random(seed)
        self.t = 0.0

    def rand(self, n):  # FUN_801cae00(rng, n): 0..n-1
        return self.rng.randrange(n)

    def play(self, slot, name):
        self.stop(slot, 0)
        self.slot[slot] = Inst(name, self.t)

    def stop(self, slot, frames):
        i = self.slot[slot]
        if i is not None:
            i.t1 = self.t + frames / FPS
            i.stop_fade = frames / FPS
            self.done.append(i)
            self.slot[slot] = None

    def has(self, slot):  # handle non-null: the sound is still playing
        i = self.slot[slot]
        if i is not None and not i.playing(self.t):
            self.done.append(i)
            self.slot[slot] = None
        return self.slot[slot] is not None

    def write(self, slot, var, val):
        if self.has(slot):
            i = self.slot[slot]
            i.writes.append((self.t - i.t0, var, val))

    def track_volume(self, slot, mask, vol):
        if self.has(slot):
            i = self.slot[slot]
            if not i.tvol or i.tvol[-1][1:] != (mask, vol):
                i.tvol.append((self.t - i.t0, mask, vol))

    def mute(self, slot, mask):
        if self.has(slot):
            i = self.slot[slot]
            i.tvol.append((self.t - i.t0, mask, 'mute'))

    def set_volume(self, slot, vol, frames):
        if self.has(slot):
            i = self.slot[slot]
            i.vols.append((self.t - i.t0, vol, frames / FPS))

    def read_var(self, slot, var):
        """the game reading a variable the sequence writes (SoundTv_04_song)"""
        if not self.has(slot):
            return -1
        i = self.slot[slot]
        _, sq = _natural[i.name]
        val = 0
        rel = self.t - i.t0
        for tick, v, x in sq.var_log:
            if v == var and sq.now_of_tick[tick] <= rel:
                val = x
        return val

    def mix(self, seconds):
        out = np.zeros((int(seconds * RATE) + 1, 2), dtype=np.float32)
        for s in self.slot:
            if self.slot[s] is not None:
                self.done.append(self.slot[s])
        for k, i in enumerate(self.done):
            end = seconds if i.t1 is None else min(i.t1, seconds)
            if end <= i.t0:
                continue
            m, _ = render(i.name, end - i.t0, writes=i.writes, tvol=i.tvol, seed=k + 1, ar=AR)
            g = np.ones(len(m), dtype=np.float32)
            cur = 1.0
            for tr, vol, fade in i.vols:  # SoundHandle::SetVolume(vol, frames)
                a = int(tr * RATE); b = int((tr + fade) * RATE)
                if a >= len(g): break
                ramp = np.linspace(cur, vol, max(b - a, 1), dtype=np.float32)
                g[a:a + len(ramp)] = ramp[:len(g) - a]
                if b < len(g): g[b:] = vol
                cur = vol
            if i.t1 is not None and i.stop_fade > 0:  # Stop(frames): fade out
                n = int(i.stop_fade * RATE)
                a = max(0, len(g) - n)
                g[a:] *= np.linspace(1, 0, len(g) - a, dtype=np.float32)
            a = int(i.t0 * RATE)
            n = min(len(m), len(out) - a)
            out[a:a + n] += m[:n] * g[:n, None]
        return out


# --- the programs: start() = vtable +0x38, start_sounds() = +0x54, update(frame) = +0x40 ---

class Program:
    frames = 1200
    def __init__(self, tv): self.tv = tv; self.first = 0
    def start(self): self.start_sounds()
    def start_sounds(self): pass
    def update(self, f): pass


class Storm(Program):  # 0: static
    frames = 12
    def start_sounds(self): self.tv.play('L', 'SE_TV_00')


class Color(Program):  # 1: colour bars
    frames = 1
    def start_sounds(self): self.tv.play('L', 'SE_TV_01')


class News(Program):  # 2
    def start(self): self.state = -1; self.start_sounds()
    def start_sounds(self): self.tv.play('S', 'SE_TV_02_v00')
    def update(self, f):
        tv = self.tv
        if f == 1:
            if not tv.has('S'): tv.play('S', 'SE_TV_02_v00')
            self.state = (self.state + 1) % 3
            if self.state == 0: tv.play('L', 'SE_TV_02_b0')
            elif self.state == 1: tv.play('L', 'TV_SEQ_KK_DRUMN')
            elif tv.has('L'): tv.set_volume('L', 40 / 127.0, 45)
        elif f == 120: tv.write('L', 1, 1)
        elif f == 150: tv.write('L', 0, 2)
        elif f == 450: tv.play('S', 'SE_TV_02_v10'); tv.write('L', 0, 0)
        elif f == 600: tv.play('S', 'SE_TV_02_v20'); tv.write('L', 1, 2)
        elif f == 900: tv.write('L', 0, 3)
        elif f == 1125: tv.play('S', 'SE_TV_02_v30')
        elif f == 1155 and self.state == 2: tv.stop('L', 45)
        if self.state == 0: tv.track_volume('L', 0xfff, 1.0); tv.track_volume('S', 0xfff, 1.0)
        elif self.state == 1: tv.track_volume('L', 0xfff, 100 / 127.0); tv.track_volume('S', 0xfff, 0.0)
        elif self.state == 2: tv.track_volume('S', 0xfff, 100 / 127.0)


class Drama(Program):  # 3
    frames = 1080
    def start_sounds(self): self.tv.play('L', 'TV_SEQ_HATO'); self.state = 0
    def update(self, f):
        tv, s = self.tv, self.state
        if f == 1 and s == 0: tv.play('S', 'SE_TV_03_v0')
        elif f == 360 and s == 1: tv.play('S', 'SE_TV_03_v1')
        elif f == 570 and s == 0: tv.play('S', 'SE_TV_03_v2')
        elif f == 630 and s == 1: tv.play('S', 'SE_TV_03_v4')
        elif f == 900 and s == 0: tv.play('S', 'SE_TV_03_v3')
        elif f == 1047: self.state ^= 1


class Song(Program):  # 4: KK_JAZZ drives the vocals through its variable 0
    frames = 1080
    def start_sounds(self): self.tv.play('S', 'TV_SEQ_KK_JAZZ'); self.last = -1; self.alt = 0
    def update(self, f):
        tv = self.tv
        v = tv.read_var('S', 0)
        cue = v if v != self.last else -1
        self.last = v
        if 1 <= cue <= 4:
            tv.play('L', 'SE_TV_04_v%d%s' % (cue - 1, '_2' if self.alt else ''))
            if cue == 4: self.alt ^= 1


class Quiz(Program):  # 5
    frames = 1440
    def start_sounds(self): self.state = 0
    def update(self, f):
        tv, s = self.tv, self.state
        if not tv.has('S'):
            tv.play('S', 'SE_TV_05_BG')
            tv.write('S', 1, 1 if 210 <= f < 660 or 840 <= f < 1110 else 0)
        if f == 1: tv.play('L', 'SE_TV_05_MC1' if s else 'SE_TV_05_MC0')
        elif f == 210: tv.write('S', 1, 1)
        elif f == 630: tv.write('S', 2, 1)
        elif f == 660: tv.write('S', 1, 0)
        elif f == 690: tv.play('L', 'SE_TV_05_RES1' if s else 'SE_TV_05_RES0')
        elif f == 759 and s == 1: tv.write('S', 2, 2)
        elif f == 774 and s == 0: tv.write('S', 2, 2)
        elif f == 840: tv.write('S', 1, 1)
        elif f == 1110: tv.write('S', 1, 0); tv.write('S', 2, 1)
        elif f == 1149 and s == 1: tv.write('L', 0, 2)
        elif f == 1170 and s == 0: tv.write('L', 0, 2)
        elif f == 1215 and s == 1: tv.write('S', 2, 3)
        elif f == 1275 and s == 0: tv.write('S', 2, 3)
        elif f == 1439: self.state ^= 1


class Travel(Program):  # 6
    frames = 1560
    MIX = {0: [(1, 70), (2, 80), (0xc, 120), (0x10, 110)],
           270: [(1, 75), (0xe, 110), (0x10, 110)],
           480: [(1, 80), (0xe, 110), (0x10, 110)],
           780: [(1, 80), (0xe, 110), (0x10, 100)],
           1113: [(1, 75), (2, 0), (0xc, 9), (0x10, 75)],
           1353: [(1, 85), (0xe, 0), (0x10, 70)]}
    VOICE = {0: 0, 270: 1, 480: 2, 780: 3, 1113: 4}
    def start_sounds(self):
        self.tv.play('S', 'SE_TV_06_BGM0'); self.tv.play('L', 'SE_TV_06_VOICE0'); self.state = 1
    def update(self, f):
        tv = self.tv
        if f in self.MIX:
            for mask, v in self.MIX[f]: tv.track_volume('S', mask, v / 127.0)
            if f in self.VOICE and self.state:
                tv.play('L', 'SE_TV_06_VOICE%d' % self.VOICE[f])
            if f == 1353: self.state ^= 1


class Hero(Program):  # 7
    frames = 714
    def start(self): self.mute_bit = 0; self.cnt = 0; self.start_sounds()
    def start_sounds(self): self.tv.play('S', 'SE_TV_07_BGM1'); self.cur = self.last = 3
    def update(self, f):
        tv = self.tv
        if self.first:
            self.first = 0
            tv.play('S', 'SE_TV_07_BGM0'); tv.play('L', 'SE_TV_07_SE_LAUGH')
            self.cur = self.last = self.mute_bit = self.cnt = 0
        elif f == 0:
            if tv.rand(100) < 40: tv.play('S', 'SE_TV_07_BGM0s'); tv.play('L', 'SE_TV_07_SE_LAUGH'); self.cur = 1
            else: tv.play('S', 'SE_TV_07_BGM2'); tv.play('L', 'SE_TV_07_SE_POWERUP'); self.cur = 2
        elif f == 270 and self.cur != 0:
            r = tv.rand(3)
            bgm, se, self.cur = [('BGM1', 'RUNBOMB', 3), ('BGM1', 'RUNWIND', 4), ('BGM3', 'RUNBOMB', 5)][r]
            tv.play('S', 'SE_TV_07_' + bgm); tv.play('L', 'SE_TV_07_SE_' + se)
        elif f == 360 and self.cur == 0:
            se, self.cur = [('RUNBOMB', 3), ('RUNWIND', 4)][tv.rand(2)]
            tv.play('S', 'SE_TV_07_BGM1'); tv.play('L', 'SE_TV_07_SE_' + se)
        if self.last != self.cur:  # the music drops out now and then
            if self.cnt == 0: self.cnt = 1
            elif self.cnt == 1:
                if tv.rand(2) == 0: self.cnt += 1
                else: self.cnt = 0; self.mute_bit ^= 1
            else: self.cnt = 0; self.mute_bit ^= 1
            if self.mute_bit: tv.mute('S', 0x3f)
            self.last = self.cur


class Variety(Program):  # 8
    frames = 900
    def start(self): self.cnt = 0; self.start_sounds()
    def start_sounds(self): self.tv.play('L', 'SE_TV_08_BGM1'); self.st = 0
    def voice1(self):
        r = self.tv.rand(100)
        self.tv.play('S', 'SE_TV_08_VOICE1%d' % (0 if r < 33 else 1 if r < 66 else 2))
    def update(self, f):
        tv = self.tv
        if self.first:
            self.first = 0; tv.play('L', 'SE_TV_08_BGM0'); self.cnt = 0
        elif f == 0:
            if tv.rand(100) < 70: tv.write('L', 0, 1)
            tv.play('S', 'SE_TV_08_VOICE0')
        elif f == 270:
            if tv.rand(100) < 60: tv.write('L', 1, 1); self.st = 1
            self.voice1()
        elif f == 435:
            self.voice1()
        elif f == 540:
            if tv.rand(100) < 70 and self.st != 1: tv.write('L', 1, 1); self.st = 2
            if tv.rand(100) < 60 and self.st != 1: tv.write('L', 0, 1); self.st = 2
        elif f == 750:
            r = tv.rand(100)
            if self.st != 2:
                if r < 70: tv.write('L', 2, 1); self.st = 3
                else: tv.write('L', 0, 1); self.st = 4
            tv.play('S', 'SE_TV_08_VOICE2')
        elif self.cnt == 309: tv.play('L', 'SE_TV_08_BGM1')
        elif self.cnt == 2100: tv.play('L', 'SE_TV_08_BGM2')
        elif self.cnt == 2700: tv.play('L', 'SE_TV_08_BGM0'); self.cnt = 0
        self.cnt += 1


class Movie(Program):  # 9
    frames = 900
    def start_sounds(self): self.tv.play('L', 'SE_TV_09_L0'); self.tv.play('S', 'SE_TV_09_S0')
    def update(self, f):
        tv = self.tv
        if f in (0, 3, 6, 9, 12):
            if not tv.has('L'): tv.play('L', 'SE_TV_09_L0')
            if not tv.has('S'): tv.play('S', 'SE_TV_09_S0')
            tv.write('L', 0, 1); tv.write('S', 0, 1)
        elif f == 270: tv.write('L', 0, 2); tv.write('S', 0, 2)
        elif f == 420: tv.write('L', 0, 3)
        elif f == 675: tv.write('L', 0, 4)
        elif f == 705: tv.write('L', 0, 5)
        elif f == 735: tv.write('S', 0, 3)


class Soccer(Program):  # 10: its own clock; the picture loop only gates the running sound (frame > 90)
    frames = 720
    def start(self): self.scene = self.prev = self.clock = 0; self.start_sounds()
    def start_sounds(self): self.tv.play('L', 'SE_TV_10_NORMAL'); self.prev = self.scene = 0
    def update(self, f):
        tv = self.tv
        self.clock += 1
        sc, c = self.scene, self.clock
        th = None
        if sc == 15 and c > 419 or sc == 16 and c > 299:
            th = (50, 75, 25, 50, 75, 88)
        elif sc % 5 < 3 and c > 359:
            th = [(50, 75), (40, 80), (40, 60)][min(sc // 5, 2)] + (25, 50, 75, 88)
        elif sc % 5 >= 3 and c > 209:
            th = [(50, 75), (40, 80), (40, 60)][min(sc // 5, 2)] + (35, 70, 100, 100)
        if th is None:
            return
        self.clock = 0
        tv.stop('S', 0)
        self.prev = sc
        r = tv.rand(100)
        tier = 0 if r <= th[0] else 5 if r <= th[1] else 10  # 0 calm, 5 commentary, 10 excited
        r = tv.rand(100)
        k = 0 if r <= th[2] else 1 if r <= th[3] else 2 if r <= th[4] else 3 if r <= th[5] else 4
        self.scene = tier + k
        if self.prev % 5 != k:
            tv.play('L', 'SE_TV_10_' + ['NORMAL', 'CALL', 'SONG', 'FINEPLAY', 'ROUGHPLAY'][k])
        if tier == 0:
            tv.track_volume('L', 3, 100 / 127.0)
            if tv.rand(100) < 40 and f > 90:
                tv.play('S', 'SE_TV_10_RUN'); tv.track_volume('S', 3, 100 / 127.0)
        elif tier == 5:
            tv.track_volume('L', 3, 75 / 127.0); tv.play('S', 'SE_TV_10_TALK')
        else:
            tv.track_volume('L', 3, 1.0)
            if tv.rand(100) < 40 and f > 90 and self.scene < 14:
                tv.play('S', 'SE_TV_10_RUN'); tv.track_volume('S', 3, 80 / 127.0)
        if self.prev in (11, 13):
            r = tv.rand(100)
            if r < 21: tv.play('L', 'SE_TV_10_GOAL'); self.scene = 15
            elif r < 41: tv.play('L', 'SE_TV_10_MISS'); self.scene = 16


class Weather(Program):  # 255
    TUNES = ['TV_SEQ_KK_SWING', 'TV_SEQ_KK_LSONG', 'TV_SEQ_KK_PARIS']
    def start_sounds(self):
        self.loops = 0; self.tune = self.tv.rand(3); self.state = 0
        self.tv.play('L', self.TUNES[self.tune])
    def update(self, f):
        tv, s = self.tv, self.state
        if f == 30: tv.play('S', 'SE_TV_255_v10' if s == 1 else 'SE_TV_255_v00') if s < 2 else None
        elif f == 150 and s == 0: tv.play('S', 'SE_TV_255_v01')
        elif f == 170 and s == 1: tv.play('S', 'SE_TV_255_v11')
        elif f == 1199:
            self.loops += 1
            if self.loops == 7:
                self.loops = 0
                t = self.tune
                while t == self.tune: t = tv.rand(3)
                self.tune = t; tv.play('L', self.TUNES[t])
            self.state = (self.state + 1) % 3


PROGRAMS = {'storm': Storm, 'color': Color, 'news': News, 'drama': Drama, 'song': Song, 'quiz': Quiz,
            'travel': Travel, 'hero': Hero, 'variety': Variety, 'movie': Movie, 'soccer': Soccer,
            'weather': Weather}


def simulate(name, loops, seed=1, start_frame=0):
    tv = TV(seed)
    p = PROGRAMS[name](tv)
    p.start()          # holder start: sounds begin, volume factor 0
    p.first = 1        # first audible frame: SoundTv +0x39 = 1 (hero / variety restart their music)
    n = int(p.frames * loops)
    for i in range(n):
        tv.t = i / FPS
        p.update((start_frame + i) % p.frames)
    return tv.mix(n / FPS)


# What cf_sky/tv/ holds, by pc_tv.c's program number (City Folk's 0-10, then the weather report): name,
# and how many picture loops make one whole cycle of the sound (its states; random programs get a few)
CYCLES = [('storm', 25), ('color', 180), ('news', 3), ('drama', 2), ('song', 2), ('quiz', 2), ('travel', 2),
          ('hero', 3), ('variety', 3), ('movie', 1), ('soccer', 5), ('weather', 3)]
PEAK = 0.9  # the loudest program; the others keep their loudness next to it


def write_tv_sound(root, out_dir):
    """cf_sky/tv/<name>.wav: mono 16-bit at 32 kHz, exactly the program's frames x loops long, so pc_tv.c
    can play it from the same clock as the picture"""
    global AR
    AR = Brsar(open(os.path.join(root, 'Sound', 'rv_forest.brsar'), 'rb').read())
    _natural.clear()
    mixes = []
    for name, loops in CYCLES:
        n = PROGRAMS[name].frames * loops * RATE // 60
        mix = simulate(name, loops).mean(axis=1)[:n]
        mixes.append((name, np.pad(mix, (0, n - len(mix)))))
    peak = max(float(np.abs(m).max()) for _, m in mixes)
    os.makedirs(out_dir, exist_ok=True)
    for name, mix in mixes:
        w = wave.open(os.path.join(out_dir, name + '.wav'), 'wb')
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes((np.clip(mix * (PEAK / max(peak, 1e-6)), -1, 1) * 32767).astype('<i2').tobytes())
        w.close()
    print('wrote %s (%d programs\' sound)' % (out_dir, len(mixes)))


if __name__ == '__main__':
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    write_tv_sound(sys.argv[1], sys.argv[2])
