# ac-plus-server

Keeps your town in sync between your machines: a PC and a Steam Deck, say. Anyone can run one, for
themselves or for friends. It's one Python file (3.8 or newer) with nothing to install. Later
releases add multiplayer matchmaking to it.

## Running it

```sh
./ac-plus-server.py add-token yourname   # prints a token: keep it, it's shown once
./ac-plus-server.py serve                # listens on port 8086
```

- `--data DIR` chooses where tokens and saves are kept (default `ac-plus-data/` next to where you run it).
- `serve --port N --bind ADDR` chooses where it listens (`--data` goes before `serve`).
- `list` shows who has a token and their latest upload; `revoke NAME` takes a token away (the saves stay).

To keep it running, use a systemd service, a container, or `tmux`; whatever you already use.

**Use HTTPS outside your own network.** The server speaks plain HTTP, and the token travels with every
request. Put it behind a reverse proxy that adds HTTPS, for example Caddy:

```
saves.example.org {
    reverse_proxy localhost:8086
}
```

## In the game

In the game's Settings → Saving: turn on **In-game sync**, then type the **Sync server** (`host:port` for
a server at home, or the full `https://` address) and the **Sync token** (Ctrl+V pastes it). Or in
`settings.ini` (next to the game):

```ini
sync_enabled = 1
sync_url = https://saves.example.org
sync_token = the token add-token printed
```

The game then:
- **at startup:** downloads your town if it was saved on another machine since you last played here. If
  both machines have new progress, it asks which one to keep.
- **after every save:** uploads it in the background and shows **Save Upload Complete**. That's when it's
  safe to close.

Nothing is lost to a mix-up:
- **On the server:** the last 30 uploads stay in `towns/NAME/history/`.
- **On the machine:** whatever a download replaces is copied to `save/sync-backups/`. A download that
  can't be written whole is undone from that copy.
- **No answer:** if the which-one question can't be answered (closed, or it couldn't show), the game plays
  this machine's save and uploads nothing until it asks again at the next start.

With `sync_enabled = 1`, `ac-sync.sh` (the older ssh sync) stands aside.
