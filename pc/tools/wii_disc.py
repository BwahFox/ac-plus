#!/usr/bin/env python3
"""Read files from a Wii disc image (.iso, or .wbfs) without other tools, for extract_cf_sky.py.

A Wii disc keeps the game in an encrypted partition: each 32 KiB cluster has 1 KiB of hashes and
31 KiB of data, AES-128-CBC with the partition's title key, which in turn is encrypted with the Wii
common key (as Dolphin and Wiimms ISO Tools also carry it). WBFS files hold the same disc with the
unused parts left out.

    disc = WiiDisc(path)          # .iso or .wbfs
    disc.game_id                  # 'RUUE01'
    disc.revision                 # 0
    disc.read('Sound/rv_forest.brsar')
    disc.extract(names, out_dir)  # several files or folders (a trailing '/'), as files under out_dir
"""
import ctypes
import ctypes.util
import os
import struct
import sys

COMMON_KEY = bytes.fromhex('ebe42a225e8593e448d9c5457381aaf7')
CLUSTER = 0x8000
CLUSTER_DATA = 0x7C00


def u32(b, o):
    return struct.unpack_from('>I', b, o)[0]


# --- AES-128-CBC decryption: the system's own if it has one, else numpy ---

class _BCrypt:
    """Windows' CNG (also under Wine and Proton)."""

    def __init__(self):
        self.lib = ctypes.WinDLL('bcrypt')
        self.alg = ctypes.c_void_p()
        if self.lib.BCryptOpenAlgorithmProvider(ctypes.byref(self.alg), ctypes.c_wchar_p('AES'), None, 0):
            raise OSError('no AES')
        mode = ctypes.create_unicode_buffer('ChainingModeCBC')
        if self.lib.BCryptSetProperty(self.alg, ctypes.c_wchar_p('ChainingMode'), mode, ctypes.sizeof(mode), 0):
            raise OSError('no CBC')

    def decrypt(self, key, iv, data):
        handle = ctypes.c_void_p()
        kb = ctypes.create_string_buffer(key, len(key))
        if self.lib.BCryptGenerateSymmetricKey(self.alg, ctypes.byref(handle), None, 0, kb, len(key), 0):
            raise OSError('bad key')
        ivb = ctypes.create_string_buffer(iv, len(iv))
        out = ctypes.create_string_buffer(len(data))
        got = ctypes.c_ulong()
        status = self.lib.BCryptDecrypt(handle, data, len(data), None, ivb, len(iv), out, len(data),
                                        ctypes.byref(got), 0)
        self.lib.BCryptDestroyKey(handle)
        if status:
            raise OSError('decrypt failed')
        return out.raw[:got.value]


class _OpenSSL:
    """libcrypto (Linux, macOS)."""

    def __init__(self):
        name = ctypes.util.find_library('crypto')
        if not name:
            raise OSError('no libcrypto')
        lib = self.lib = ctypes.CDLL(name)
        lib.EVP_CIPHER_CTX_new.restype = ctypes.c_void_p
        lib.EVP_aes_128_cbc.restype = ctypes.c_void_p
        lib.EVP_DecryptInit_ex.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_char_p,
                                           ctypes.c_char_p]
        lib.EVP_DecryptUpdate.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.POINTER(ctypes.c_int),
                                          ctypes.c_char_p, ctypes.c_int]
        lib.EVP_CIPHER_CTX_set_padding.argtypes = [ctypes.c_void_p, ctypes.c_int]
        lib.EVP_CIPHER_CTX_free.argtypes = [ctypes.c_void_p]

    def decrypt(self, key, iv, data):
        lib = self.lib
        ctx = lib.EVP_CIPHER_CTX_new()
        lib.EVP_DecryptInit_ex(ctx, lib.EVP_aes_128_cbc(), None, key, iv)
        lib.EVP_CIPHER_CTX_set_padding(ctx, 0)
        out = ctypes.create_string_buffer(len(data) + 16)
        n = ctypes.c_int()
        lib.EVP_DecryptUpdate(ctx, out, ctypes.byref(n), data, len(data))
        lib.EVP_CIPHER_CTX_free(ctx)
        return out.raw[:n.value]


class _NumpyAES:
    """Plain AES with numpy tables: slower, but runs anywhere numpy does."""

    def __init__(self):
        import numpy as np
        self.np = np
        sbox = [0] * 256
        p = q = 1
        while True:  # the AES S-box, from the multiplicative inverse
            p = p ^ ((p << 1) & 0xFF) ^ (0x1B if p & 0x80 else 0)
            q ^= q << 1
            q ^= q << 2
            q ^= q << 4
            q &= 0xFF
            if q & 0x80:
                q ^= 0x09
            x = q ^ ((q << 1) | (q >> 7)) ^ ((q << 2) | (q >> 6)) ^ ((q << 3) | (q >> 5)) ^ ((q << 4) | (q >> 4))
            sbox[p] = (x ^ 0x63) & 0xFF
            if p == 1:
                break
        sbox[0] = 0x63
        inv = [0] * 256
        for i, v in enumerate(sbox):
            inv[v] = i
        self.sbox, self.inv = sbox, np.array(inv, dtype=np.uint8)

        def mul(a, b):
            r = 0
            while b:
                if b & 1:
                    r ^= a
                a = ((a << 1) ^ (0x1B if a & 0x80 else 0)) & 0xFF
                b >>= 1
            return r
        self.mul = {k: np.array([mul(i, k) for i in range(256)], dtype=np.uint8) for k in (9, 11, 13, 14)}

    def _round_keys(self, key):
        sbox = self.sbox
        w = [list(key[i:i + 4]) for i in range(0, 16, 4)]
        rcon = 1
        for i in range(4, 44):
            t = list(w[i - 1])
            if i % 4 == 0:
                t = [sbox[b] for b in t[1:] + t[:1]]
                t[0] ^= rcon
                rcon = ((rcon << 1) ^ (0x11B if rcon & 0x80 else 0)) & 0xFF
            w.append([a ^ b for a, b in zip(w[i - 4], t)])
        return [self.np.array(sum(w[r * 4:r * 4 + 4], []), dtype=self.np.uint8) for r in range(11)]

    def decrypt(self, key, iv, data):
        np = self.np
        rk = self._round_keys(key)
        c = np.frombuffer(data, dtype=np.uint8).reshape(-1, 16)
        s = c ^ rk[10]
        shift = np.array([0, 13, 10, 7, 4, 1, 14, 11, 8, 5, 2, 15, 12, 9, 6, 3])  # inverse ShiftRows
        for r in range(9, -1, -1):
            s = self.inv[s[:, shift]] ^ rk[r]
            if r:
                a = s.reshape(-1, 4, 4)
                m = self.mul
                s = np.stack([m[14][a[:, :, 0]] ^ m[11][a[:, :, 1]] ^ m[13][a[:, :, 2]] ^ m[9][a[:, :, 3]],
                              m[9][a[:, :, 0]] ^ m[14][a[:, :, 1]] ^ m[11][a[:, :, 2]] ^ m[13][a[:, :, 3]],
                              m[13][a[:, :, 0]] ^ m[9][a[:, :, 1]] ^ m[14][a[:, :, 2]] ^ m[11][a[:, :, 3]],
                              m[11][a[:, :, 0]] ^ m[13][a[:, :, 1]] ^ m[9][a[:, :, 2]] ^ m[14][a[:, :, 3]]],
                             axis=2).reshape(-1, 16)
        prev = np.concatenate([np.frombuffer(iv, dtype=np.uint8)[None, :], c[:-1]])
        return (s ^ prev).tobytes()


def _aes():
    makers = [_BCrypt] if sys.platform == 'win32' else [_OpenSSL]
    makers.append(_NumpyAES)
    for make in makers:
        try:
            return make()
        except Exception:
            continue
    raise RuntimeError('no way to decrypt AES here (install numpy)')


# --- the disc ---

class _Raw:
    def __init__(self, f):
        self.f = f

    def read(self, off, n):
        self.f.seek(off)
        return self.f.read(n)


class _Wbfs:
    """A WBFS file's first disc, read by disc offset (missing blocks read as zeros)."""

    def __init__(self, f):
        self.f = f
        head = f.read(16)
        self.hd_sec = 1 << head[8]
        self.wbfs_sec = 1 << head[9]
        n_wii_sec = 143432 * 2  # dual layer, as WBFS always counts
        n_wbfs_per_disc = (n_wii_sec * 0x8000) // self.wbfs_sec
        f.seek(self.hd_sec + 0x100)  # the first disc slot: header copy, then its block table
        self.table = struct.unpack('>%dH' % n_wbfs_per_disc, f.read(2 * n_wbfs_per_disc))

    def read(self, off, n):
        out = bytearray()
        while n > 0:
            blk, inner = divmod(off, self.wbfs_sec)
            take = min(n, self.wbfs_sec - inner)
            where = self.table[blk] if blk < len(self.table) else 0
            if where:
                self.f.seek(where * self.wbfs_sec + inner)
                out += self.f.read(take)
            else:
                out += bytes(take)
            off += take
            n -= take
        return bytes(out)


class WiiDisc:
    def __init__(self, path):
        self.f = open(path, 'rb')
        magic = self.f.read(4)
        self.f.seek(0)
        self.src = _Wbfs(self.f) if magic == b'WBFS' else _Raw(self.f)
        head = self.src.read(0, 0x20)
        if u32(head, 0x18) != 0x5D1C9EA3:
            raise ValueError('%s is not a Wii disc image' % path)
        self.game_id = head[:6].decode('ascii', 'replace')
        self.revision = head[7]
        self.aes = _aes()
        self._open_partition()
        self._read_fst()

    def _open_partition(self):
        info = self.src.read(0x40000, 0x20)
        for group in range(4):
            count, table = u32(info, group * 8), u32(info, group * 8 + 4) << 2
            entries = self.src.read(table, count * 8)
            for i in range(count):
                off, kind = u32(entries, i * 8) << 2, u32(entries, i * 8 + 4)
                if kind == 0:  # the game's own partition
                    ticket = self.src.read(off, 0x2C0)
                    enc_key, title_id = ticket[0x1BF:0x1CF], ticket[0x1DC:0x1E4]
                    self.title_key = self.aes.decrypt(COMMON_KEY, title_id + bytes(8), enc_key)
                    self.data_off = off + (u32(ticket, 0x2B8) << 2)
                    return
        raise ValueError('no game partition on the disc')

    def _clusters(self, first, count):
        raw = self.src.read(self.data_off + first * CLUSTER, count * CLUSTER)
        out = bytearray()
        for i in range(count):
            c = raw[i * CLUSTER:(i + 1) * CLUSTER]
            if len(c) < CLUSTER:
                c += bytes(CLUSTER - len(c))
            out += self.aes.decrypt(self.title_key, c[0x3D0:0x3E0], c[0x400:])
        return bytes(out)

    def read_data(self, off, n):
        """Bytes off.. off+n of the partition's (decrypted) data."""
        first = off // CLUSTER_DATA
        last = (off + n + CLUSTER_DATA - 1) // CLUSTER_DATA
        out = bytearray()
        for start in range(first, last, 256):  # 8 MiB at a time
            out += self._clusters(start, min(256, last - start))
        skip = off - first * CLUSTER_DATA
        return bytes(out[skip:skip + n])

    def _read_fst(self):
        boot = self.read_data(0, 0x440)
        fst_off, fst_size = u32(boot, 0x424) << 2, u32(boot, 0x428) << 2
        fst = self.read_data(fst_off, fst_size)
        n = u32(fst, 8)
        names = fst[n * 12:]
        self.files = {}  # 'Sound/rv_forest.brsar' -> (offset, size)
        dirs = [(n, '')]  # (end entry, path)
        for i in range(1, n):
            while dirs and i >= dirs[-1][0]:
                dirs.pop()
            word = u32(fst, i * 12)
            name_off = word & 0xFFFFFF
            name = names[name_off:names.index(b'\0', name_off)].decode('ascii', 'replace')
            path = dirs[-1][1] + name if dirs else name
            if word >> 24:
                dirs.append((u32(fst, i * 12 + 8), path + '/'))
            else:
                self.files[path] = (u32(fst, i * 12 + 4) << 2, u32(fst, i * 12 + 8))

    def read(self, name):
        off, size = self.files[name]
        return self.read_data(off, size)

    def extract(self, wanted, out_dir, progress=None):
        """wanted: file names, or folders ending in '/'. Writes them under out_dir/files/."""
        names = [p for p in sorted(self.files) if any(p == w or (w.endswith('/') and p.startswith(w)) for w in wanted)]
        total = sum(self.files[p][1] for p in names) or 1
        done = 0
        for p in names:
            dst = os.path.join(out_dir, 'files', *p.split('/'))
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            with open(dst, 'wb') as f:
                f.write(self.read(p))
            done += self.files[p][1]
            if progress:
                progress(done / total)
        return os.path.join(out_dir, 'files')


if __name__ == '__main__':
    d = WiiDisc(sys.argv[1])
    print(d.game_id, 'revision', d.revision, type(d.aes).__name__, len(d.files), 'files')
    for name in sys.argv[2:]:
        data = d.read(name)
        print(name, len(data), data[:8])
