#!/usr/bin/env python3
"""ac-plus-server: keeps players' towns in sync between their machines, and introduces players for online visits.
Anyone can run one.

One file, Python 3.8 or newer, nothing to install. The game uploads its town after every save and fetches it at
startup (pc/src/pc_sync.c). For online visits it hands out invite codes and puts visitors in touch with hosts
(pc/src/pc_mp_mm.c); the games then talk to each other directly, encrypted. See "Matchmaking" below.

    ./ac-plus-server.py add-token NAME       make a token for NAME (paste it into the game's settings)
    ./ac-plus-server.py list                 who has a token, and their town's latest upload
    ./ac-plus-server.py revoke NAME          take NAME's token away (their saves stay)
    ./ac-plus-server.py serve [--port 8086] [--bind 0.0.0.0] [--mm-port 41331 | --no-mm]
    (all take --data DIR; default ./ac-plus-data)

Plain HTTP: put it behind Caddy, nginx or similar for HTTPS anywhere but your own network. See server/README.md.

Protocol (version 1). Every request carries "Authorization: Bearer <token>"; each token has its own town.
    GET  /v1/save/info   {"rev": N, "sha256": hex, "saved_at": unix, "device": name, "size": bytes} (rev 0: none yet)
    GET  /v1/save        the town as uploaded (the game's bundle of its save files); headers X-Rev, X-Sha256
    PUT  /v1/save        a new upload. X-Base-Rev: the rev it was made from (409 with the info if the server has
                         moved on since: a conflict for the player to settle), X-Force: 1 to replace regardless,
                         X-Sha256 of the body, X-Device: the machine's name. Answers with the new info.
Every upload is kept in the token's history/ (the last KEEP_HISTORY of them), so nothing is lost to a conflict.

Matchmaking (UDP, version 1; the game's side is pc/src/mp/mp_mm.c and pc/src/pc_mp.c). No accounts and nothing stored: a host's game
tells the server it is open and gets a short invite code; a visitor's game asks for a code; the host's game says
yes or no; only then does each learn where the other is (so a public listing never gives a host's address away),
and both send packets to each other to open their routers. Everything after that is between the two games,
encrypted end to end (pc/src/mp). Packets (all integers little-endian):
    host    -> server 0x20 HOST    "ACMM" u16 ver, u32 nonce, key[32], u8 public, town[8], name[8], pad to 96
    server  -> host   0x21 CODE    u32 nonce, code[6]                          (send HOST every 15 s to keep it)
    visitor -> server 0x22 LOOKUP  "ACMM" u16 ver, u32 nonce, code[6], key[32], name[8], pad to 96
    visitor -> server 0x2E FIND    "ACMM" u16 ver, u32 nonce, host key[32], key[32], name[8], pad to 96
                                   (a town visited before, by its host's key; answered like LOOKUP)
    server  -> host   0x23 ASK     u32 request, key[32], name[8]               (someone wants to visit)
    host    -> server 0x24 ANSWER  "ACMM" u16 ver, u32 request, u8 yes, pad to 96
    server  -> visitor 0x25 FOUND  u32 nonce, ip[4], u16 port, key[32], town[8], name[8]
    server  -> visitor 0x27 NONE   u32 nonce, u8 why (0 no such code, 1 no, 2 too many tries, 3 full)
    server  -> host   0x28 INTRO   u32 request, ip[4], u16 port, key[32]        (the visitor said hello: punch)
    any     -> server 0x26 LIST    "ACMM" u16 ver, u32 nonce, u8 page, pad to 400
    server  -> any    0x29 TOWNS   u32 nonce, u8 count, count x (code[6], town[8], name[8])   (public hosts, 16 a page)
    host    -> server 0x2A CLOSE   "ACMM" u16 ver, u32 nonce, key[32], pad to 96
With --relay, for games that can't reach each other (CGNAT, strict routers):
    server  -> both   0x2C RELAY   u32 nonce (visitor) / request (host), token[8]   (sent with FOUND and INTRO)
    either <-> server 0x2D RELAYED token[8], an encrypted game packet: the server swaps in the other side's token
                                   and sends it on. It can't read or change what it carries.
Requests are padded so no answer is bigger than its question (spoofed requests can't be amplified), each address
gets a limited number of packets and look-ups, and codes go away a minute after their host stops renewing them.
A relay takes packets for a token only from the address it gave the token to, at most MM_RELAY_BPS a second, and an
address has at most MM_RELAY_PER_IP routes. A host's key only moves to a new address once the old one has gone quiet
(MM_HOST_TAKEOVER), so nobody can take over a live host's listing; the games pin the key anyway.
"""
import argparse
import hashlib
import hmac
import json
import os
import re
import secrets
import socket
import struct
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

PROTOCOL = 1
MAX_UPLOAD = 32 * 1024 * 1024
KEEP_HISTORY = 30
NAME_RE = re.compile(r"^[A-Za-z0-9_.-]{1,40}$")

_lock = threading.Lock()


def _tokens_path(data):
    return os.path.join(data, "tokens.json")


def load_tokens(data):
    try:
        with open(_tokens_path(data)) as f:
            return json.load(f)  # {sha256(token): name}
    except FileNotFoundError:
        return {}


def save_tokens(data, tokens):
    os.makedirs(data, exist_ok=True)
    tmp = _tokens_path(data) + ".tmp"
    with open(tmp, "w") as f:
        json.dump(tokens, f, indent=1)
    os.chmod(tmp, 0o600)
    os.replace(tmp, _tokens_path(data))


def hash_token(token):
    return hashlib.sha256(token.encode()).hexdigest()


def town_dir(data, name):
    return os.path.join(data, "towns", name)


def read_info(data, name):
    try:
        with open(os.path.join(town_dir(data, name), "info.json")) as f:
            return json.load(f)
    except FileNotFoundError:
        return {"rev": 0}


def store(data, name, body, device):
    """Writes a new upload (under _lock). Returns its info."""
    d = town_dir(data, name)
    hist = os.path.join(d, "history")
    os.makedirs(hist, exist_ok=True)
    info = read_info(data, name)
    rev = info.get("rev", 0) + 1
    new = {"rev": rev, "sha256": hashlib.sha256(body).hexdigest(), "saved_at": int(time.time()),
           "device": device[:64], "size": len(body)}
    for path, content in ((os.path.join(hist, "%06d.bundle" % rev), body),
                          (os.path.join(d, "current.bundle"), body)):
        tmp = path + ".tmp"
        with open(tmp, "wb") as f:
            f.write(content)
            f.flush()
            os.fsync(f.fileno())
        os.replace(tmp, path)
    tmp = os.path.join(d, "info.json.tmp")
    with open(tmp, "w") as f:
        json.dump(new, f)
    os.replace(tmp, os.path.join(d, "info.json"))
    old = sorted(os.listdir(hist))
    for h in old[:-KEEP_HISTORY]:
        os.remove(os.path.join(hist, h))
    return new


class Handler(BaseHTTPRequestHandler):
    server_version = "ac-plus-server/%d" % PROTOCOL
    data = "."

    def log_message(self, fmt, *args):
        sys.stderr.write("%s %s\n" % (time.strftime("%Y-%m-%d %H:%M:%S"), fmt % args))

    def _send(self, code, body=b"", ctype="application/json", headers=None):
        if isinstance(body, (dict, list)):
            body = json.dumps(body).encode()
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("X-Protocol", str(PROTOCOL))
        for k, v in (headers or {}).items():
            self.send_header(k, v)
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(body)

    def _who(self):
        auth = self.headers.get("Authorization", "")
        if not auth.startswith("Bearer "):
            return None
        want = hash_token(auth[7:].strip())
        for h, name in load_tokens(self.data).items():
            if hmac.compare_digest(h, want):
                return name
        return None

    def do_GET(self):
        name = self._who()
        if name is None:
            return self._send(401, {"error": "unknown token"})
        if self.path == "/v1/save/info":
            return self._send(200, read_info(self.data, name))
        if self.path == "/v1/save":
            info = read_info(self.data, name)
            if info.get("rev", 0) == 0:
                return self._send(404, {"error": "no save uploaded yet"})
            with open(os.path.join(town_dir(self.data, name), "current.bundle"), "rb") as f:
                body = f.read()
            return self._send(200, body, "application/octet-stream",
                              {"X-Rev": str(info["rev"]), "X-Sha256": info["sha256"]})
        return self._send(404, {"error": "no such thing"})

    def do_PUT(self):
        name = self._who()
        if name is None:
            return self._send(401, {"error": "unknown token"})
        if self.path != "/v1/save":
            return self._send(404, {"error": "no such thing"})
        try:
            size = int(self.headers.get("Content-Length", "-1"))
            base = int(self.headers.get("X-Base-Rev", "-1"))
        except ValueError:
            return self._send(400, {"error": "bad headers"})
        if size < 0 or size > MAX_UPLOAD:
            return self._send(413, {"error": "too big"})
        body = self.rfile.read(size)
        if len(body) != size or hashlib.sha256(body).hexdigest() != self.headers.get("X-Sha256", "").lower():
            return self._send(400, {"error": "the upload arrived damaged"})
        with _lock:
            info = read_info(self.data, name)
            if self.headers.get("X-Force") != "1" and base != info.get("rev", 0):
                return self._send(409, info)
            if info.get("sha256") == hashlib.sha256(body).hexdigest():
                return self._send(200, info)  # already there
            new = store(self.data, name, body, self.headers.get("X-Device", "unknown"))
        self.log_message("%s uploaded rev %d (%d bytes) from %s", name, new["rev"], size, new["device"])
        return self._send(200, new)


# --- Matchmaking -------------------------------------------------------------------------------------------------

MM_VERSION = 1
MM_MAGIC = b"ACMM"
MM_CODE_CHARS = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789"
MM_HOST_TTL = 60          # a code lasts this long after its host's last HOST
MM_ASK_TTL = 15           # a visitor waits this long for the host's answer
MM_RATE = (40, 10.0)      # packets per address per window (seconds)
MM_LOOKUPS = (10, 60.0)   # code look-ups per address per window: guessing codes is slow
MM_PAGE = 16
MM_HOST_TAKEOVER = 20     # a host's key moves to a new address only once the old one has been quiet this long
MM_RELAY_MAX = 64         # visits relayed at once (--relay)
MM_RELAY_PER_IP = 8       # relay routes one address may have at once (four visits)
MM_RELAY_IDLE = 30        # a relay route goes after this long without a packet
MM_RELAY_BPS = 128 * 1024 # bytes per second per side of a visit; a visit uses about 10k, a town arriving more


class Matchmaker:
    def __init__(self, sock, log=None, relay=False):
        self.sock = sock
        self.relay = relay
        self.relays = {}      # token -> dict(addr, peer token, at, window start, bytes)
        self.expired_at = 0
        self.hosts = {}       # code -> dict(addr, key, public, town, name, seen, nonce)
        self.by_key = {}      # key -> code
        self.asks = {}        # request -> dict(code, addr, key, name, nonce, at)
        self.rates = {}       # ip -> [count, window start, lookups, lookup window start]
        self.log = log or (lambda *a: None)

    def _allowed(self, ip, lookup=False):
        now = time.time()
        r = self.rates.setdefault(ip, [0, now, 0, now])
        if now - r[1] > MM_RATE[1]:
            r[0], r[1] = 0, now
        if now - r[3] > MM_LOOKUPS[1]:
            r[2], r[3] = 0, now
        r[0] += 1
        if r[0] > MM_RATE[0]:
            return False
        if lookup:
            r[2] += 1
            if r[2] > MM_LOOKUPS[0]:
                return False
        return True

    def _expire(self):
        now = time.time()
        if now - self.expired_at < 1:
            return
        self.expired_at = now
        for t in [t for t, r in self.relays.items() if now - r["at"] > MM_RELAY_IDLE]:
            del self.relays[t]
        for code in [c for c, h in self.hosts.items() if now - h["seen"] > MM_HOST_TTL]:
            self.by_key.pop(self.hosts[code]["key"], None)
            del self.hosts[code]
        for req in [r for r, a in self.asks.items() if now - a["at"] > MM_ASK_TTL]:
            del self.asks[req]
        if len(self.rates) > 10000:
            # forget addresses whose windows are over; only a flood of new addresses empties it all
            for ip in [ip for ip, r in self.rates.items() if now - r[1] > MM_RATE[1] and now - r[3] > MM_LOOKUPS[1]]:
                del self.rates[ip]
            if len(self.rates) > 50000:
                self.rates.clear()

    def _send(self, addr, data):
        try:
            self.sock.sendto(data, addr)
        except OSError:
            pass

    @staticmethod
    def _ip(addr):
        return socket.inet_aton(addr[0]) + struct.pack("<H", addr[1])

    def _relay(self, data, addr):
        """RELAYED: u8 0x2D, token[8], an encrypted game packet. Goes to the other side with their token, only from
        the address the token was given to, and no more than MM_RELAY_BPS a second."""
        if not self.relay or len(data) < 14 or len(data) > 9 + 1200:
            return
        r = self.relays.get(data[1:9])
        if r is None or r["addr"] != addr:
            return
        now = time.time()
        if now - r["window"] >= 1:
            r["window"], r["bytes"] = now, 0
        r["bytes"] += len(data)
        if r["bytes"] > MM_RELAY_BPS:
            return
        r["at"] = now
        peer = self.relays.get(r["peer"])
        if peer is not None:
            self._send(peer["addr"], b"\x2d" + r["peer"] + data[9:])

    def _new_relay(self, visitor, visitor_nonce, host, req):
        """RELAY to each side: u8 0x2C, u32 (the visitor's nonce / the host's request), token[8]"""
        if not self.relay or len(self.relays) >= MM_RELAY_MAX * 2:
            return
        for ip in (visitor[0], host[0]):
            if sum(1 for r in self.relays.values() if r["addr"][0] == ip) >= MM_RELAY_PER_IP:
                return  # one address doesn't get to use up the relay
        tv, th = secrets.token_bytes(8), secrets.token_bytes(8)
        now = time.time()
        self.relays[tv] = dict(addr=visitor, peer=th, at=now, window=now, bytes=0)
        self.relays[th] = dict(addr=host, peer=tv, at=now, window=now, bytes=0)
        self._send(visitor, bytes([0x2C]) + struct.pack("<I", visitor_nonce) + tv)
        self._send(host, bytes([0x2C]) + struct.pack("<I", req) + th)

    def handle(self, data, addr):
        """One packet. Anything that isn't exactly right is ignored."""
        self._expire()
        if data[:1] == b"\x2d":
            return self._relay(data, addr)
        if len(data) < 7 or data[1:5] != MM_MAGIC or struct.unpack_from("<H", data, 5)[0] != MM_VERSION:
            return
        if not self._allowed(addr[0], lookup=data[0] in (0x22, 0x2E)):
            return
        kind = data[0]
        if kind == 0x20 and len(data) == 96:  # HOST
            nonce, = struct.unpack_from("<I", data, 7)
            key, public, town, name = data[11:43], data[43], data[44:52], data[52:60]
            code = self.by_key.get(key)
            old = self.hosts.get(code) if code is not None else None
            if old is not None and old["addr"] != addr and time.time() - old["seen"] < MM_HOST_TAKEOVER:
                return  # someone else claiming a live host's key (the real one renews every 15 s): not yet
            if code is None or self.hosts.get(code, {}).get("addr") != addr:
                if code is not None:
                    self.hosts.pop(code, None)  # the same key from a new address: a new code
                code = "".join(secrets.choice(MM_CODE_CHARS) for _ in range(6))
                while code in self.hosts:
                    code = "".join(secrets.choice(MM_CODE_CHARS) for _ in range(6))
                self.by_key[key] = code
                self.log("mm: host %s:%d has code %s" % (addr[0], addr[1], code))
            self.hosts[code] = dict(addr=addr, key=key, public=public == 1, town=town, name=name, seen=time.time())
            self._send(addr, bytes([0x21]) + struct.pack("<I", nonce) + code.encode())
        elif kind in (0x22, 0x2E) and len(data) == 96:  # LOOKUP by code, FIND by the host's key
            nonce, = struct.unpack_from("<I", data, 7)
            if kind == 0x22:
                code = data[11:17].decode("ascii", "replace").upper()
                key, name = data[17:49], data[49:57]
            else:
                code = self.by_key.get(data[11:43], "")
                key, name = data[43:75], data[75:83]
            host = self.hosts.get(code)
            if host is None:
                return self._send(addr, bytes([0x27]) + struct.pack("<I", nonce) + bytes([0]))
            req = secrets.randbits(32) or 1
            self.asks[req] = dict(code=code, addr=addr, key=key, name=name, nonce=nonce, at=time.time())
            self._send(host["addr"], bytes([0x23]) + struct.pack("<I", req) + key + name)
        elif kind == 0x24 and len(data) == 96:  # ANSWER
            req, = struct.unpack_from("<I", data, 7)
            yes = data[11]
            ask = self.asks.pop(req, None)
            host = self.hosts.get(ask["code"]) if ask else None
            if ask is None or host is None or host["addr"] != addr:
                return  # only the host the visitor asked for may answer
            if yes == 1:
                self._new_relay(ask["addr"], ask["nonce"], addr, req)
                self._send(ask["addr"], bytes([0x25]) + struct.pack("<I", ask["nonce"]) + self._ip(addr) +
                           host["key"] + host["town"] + host["name"])
                self._send(addr, bytes([0x28]) + struct.pack("<I", req) + self._ip(ask["addr"]) + ask["key"])
                self.log("mm: %s:%d visits code %s" % (ask["addr"][0], ask["addr"][1], ask["code"]))
            else:
                self._send(ask["addr"], bytes([0x27]) + struct.pack("<I", ask["nonce"]) + bytes([1 if yes == 0 else 3]))
        elif kind == 0x26 and len(data) == 400:  # LIST
            nonce, = struct.unpack_from("<I", data, 7)
            page = data[11]
            public = sorted((c, h) for c, h in self.hosts.items() if h["public"])
            chunk = public[page * MM_PAGE:(page + 1) * MM_PAGE]
            body = b"".join(c.encode() + h["town"] + h["name"] for c, h in chunk)
            self._send(addr, bytes([0x29]) + struct.pack("<I", nonce) + bytes([len(chunk)]) + body)
        elif kind == 0x2A and len(data) == 96:  # CLOSE
            key = data[11:43]
            code = self.by_key.get(key)
            if code is not None and self.hosts.get(code, {}).get("addr") == addr:
                del self.hosts[code]
                del self.by_key[key]

    def serve_forever(self):
        while True:
            try:
                data, addr = self.sock.recvfrom(2048)
            except OSError:
                continue
            try:
                self.handle(data, addr)
            except Exception as e:  # a bad packet must never stop the server
                self.log("mm: dropped a packet from %s: %r" % (addr[0], e))


def main(argv=None):
    ap = argparse.ArgumentParser(description="ac-plus save sync server")
    ap.add_argument("--data", default="ac-plus-data", help="where tokens and towns are kept")
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("serve")
    p.add_argument("--port", type=int, default=8086)
    p.add_argument("--bind", default="0.0.0.0")
    p.add_argument("--mm-port", type=int, default=41331, help="UDP port for online visits' matchmaking")
    p.add_argument("--no-mm", action="store_true", help="sync only, no matchmaking")
    p.add_argument("--no-sync", action="store_true",
                   help="matchmaking only: no save sync (nothing listens on --port, no tokens or saves are kept)")
    p.add_argument("--quiet", action="store_true",
                   help="log no addresses or invite codes (for a server open to the public)")
    p.add_argument("--relay", action="store_true",
                   help="also relay visits that can't connect directly (uses this server's bandwidth)")
    p = sub.add_parser("add-token")
    p.add_argument("name")
    p = sub.add_parser("revoke")
    p.add_argument("name")
    sub.add_parser("list")
    a = ap.parse_args(argv)

    if a.cmd == "add-token":
        if not NAME_RE.match(a.name):
            sys.exit("names are letters, digits, _ . - (up to 40)")
        tokens = load_tokens(a.data)
        if a.name in tokens.values():
            sys.exit("%s already has a token (revoke it first to make a new one)" % a.name)
        token = secrets.token_urlsafe(24)
        tokens[hash_token(token)] = a.name
        save_tokens(a.data, tokens)
        print("Token for %s (shown once; paste it into the game's Settings -> Saving -> Sync token):\n%s"
              % (a.name, token))
    elif a.cmd == "revoke":
        tokens = load_tokens(a.data)
        kept = {h: n for h, n in tokens.items() if n != a.name}
        if len(kept) == len(tokens):
            sys.exit("%s has no token" % a.name)
        save_tokens(a.data, kept)
        print("%s's token no longer works (their saves are still in %s)" % (a.name, town_dir(a.data, a.name)))
    elif a.cmd == "list":
        for name in sorted(load_tokens(a.data).values()):
            info = read_info(a.data, name)
            when = time.strftime("%Y-%m-%d %H:%M", time.localtime(info["saved_at"])) if info.get("rev") else "-"
            print("%-20s rev %-5d %s  %s" % (name, info.get("rev", 0), when, info.get("device", "")))
    else:
        if a.no_sync and a.no_mm:
            sys.exit("--no-sync and --no-mm together leave nothing to serve")
        srv = None
        if not a.no_sync:
            Handler.data = a.data
            os.makedirs(a.data, exist_ok=True)
            srv = ThreadingHTTPServer((a.bind, a.port), Handler)
            print("ac-plus-server: sync on http://%s:%d (data in %s)" % (a.bind, a.port, os.path.abspath(a.data)))
        mm = None
        if not a.no_mm:
            mm_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            mm_sock.bind((a.bind, a.mm_port))
            mm_log = None if a.quiet else (lambda m: sys.stderr.write("%s %s\n" % (time.strftime("%Y-%m-%d %H:%M:%S"), m)))
            mm = Matchmaker(mm_sock, log=mm_log, relay=a.relay)
            print("ac-plus-server: matchmaking on udp %s:%d%s" % (a.bind, a.mm_port, " with relay" if a.relay else ""))
        sys.stdout.flush()
        try:
            if srv is None:
                mm.serve_forever()
            else:
                if mm is not None:
                    threading.Thread(target=mm.serve_forever, daemon=True).start()
                srv.serve_forever()
        except KeyboardInterrupt:
            pass


if __name__ == "__main__":
    main()
