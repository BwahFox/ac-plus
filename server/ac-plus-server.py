#!/usr/bin/env python3
"""ac-plus-server: keeps players' towns in sync between their machines. Anyone can run one.

One file, Python 3.8 or newer, nothing to install. The game uploads its town after every save and fetches it at
startup (pc/src/pc_sync.c). Later releases add multiplayer matchmaking here too.

    ./ac-plus-server.py add-token NAME       make a token for NAME (paste it into the game's settings)
    ./ac-plus-server.py list                 who has a token, and their town's latest upload
    ./ac-plus-server.py revoke NAME          take NAME's token away (their saves stay)
    ./ac-plus-server.py serve [--port 8086] [--bind 0.0.0.0]
    (all take --data DIR; default ./ac-plus-data)

Plain HTTP: put it behind Caddy, nginx or similar for HTTPS anywhere but your own network. See server/README.md.

Protocol (version 1). Every request carries "Authorization: Bearer <token>"; each token has its own town.
    GET  /v1/save/info   {"rev": N, "sha256": hex, "saved_at": unix, "device": name, "size": bytes} (rev 0: none yet)
    GET  /v1/save        the town as uploaded (the game's bundle of its save files); headers X-Rev, X-Sha256
    PUT  /v1/save        a new upload. X-Base-Rev: the rev it was made from (409 with the info if the server has
                         moved on since: a conflict for the player to settle), X-Force: 1 to replace regardless,
                         X-Sha256 of the body, X-Device: the machine's name. Answers with the new info.
Every upload is kept in the token's history/ (the last KEEP_HISTORY of them), so nothing is lost to a conflict.
"""
import argparse
import hashlib
import hmac
import json
import os
import re
import secrets
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


def main(argv=None):
    ap = argparse.ArgumentParser(description="ac-plus save sync server")
    ap.add_argument("--data", default="ac-plus-data", help="where tokens and towns are kept")
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("serve")
    p.add_argument("--port", type=int, default=8086)
    p.add_argument("--bind", default="0.0.0.0")
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
        Handler.data = a.data
        os.makedirs(a.data, exist_ok=True)
        srv = ThreadingHTTPServer((a.bind, a.port), Handler)
        print("ac-plus-server: sync on http://%s:%d (data in %s)" % (a.bind, a.port, os.path.abspath(a.data)))
        try:
            srv.serve_forever()
        except KeyboardInterrupt:
            pass


if __name__ == "__main__":
    main()
