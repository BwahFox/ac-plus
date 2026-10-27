# ac-plus-server

Keeps your town in sync between your machines: a PC and a Steam Deck, say. It also hands out invite
codes for online visits. Anyone can run one, for themselves or for friends. It's one Python file (3.8 or
newer) with nothing to install.

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

## Invite codes for online visits

`serve` also listens on UDP port 41331 for online visits (`--mm-port N` to change it, `--no-mm` to turn
it off). Open that UDP port to the internet if your visitors are elsewhere; the HTTPS proxy isn't
involved.

In the game's `settings.ini`, both players set:

```ini
mp_server = yourserver.example.org     # or host:port if you changed --mm-port
```

Then, in pause menu > Online Play:
- **Hosting** gets an invite code from the server.
- **The visitor** types that code where an address would go.

A town you've visited can be found again by its host's key (Online Play > Towns you've visited), as long
as it's being hosted on the same server.

The server introduces the two games, and they then talk to each other directly, encrypted. For most home
networks that means no router setup at all. `mp_public = 1` also lists the town on the server while it is
hosted.

What the server knows and keeps:
- **Kept:** nothing on disk.
- **In memory, while a town is hosted:** who is hosting (their address and key), and the town and
  player names they chose to share.
- **A host's address** goes to a visitor only after the host's game has said yes.
- **Codes** expire a minute after the host stops.
- **Limits:** each address gets a limited number of look-ups, so codes can't be guessed quickly.

### Relaying visits (`--relay`)

Some networks can't take a direct connection at all: mobile hotspots, many dorm and apartment networks, and
internet providers that share one address among many customers (CGNAT). With `--relay`, the server passes
the packets along for those visits:

```sh
python3 ac-plus-server.py serve --relay
```

- **When it's used:** only when the visitor's game can't reach the host directly within a few seconds.
- **What it can see:** nothing readable. The games encrypt everything end to end, so the relay only
  carries packets it can't read or change without the games noticing.
- **Bandwidth:** about 10 KB/s each way per visit, more for a moment while the town arrives. Each side
  is capped at 128 KB/s, and at most 64 visits are relayed at once.
- **Placement:** a relay is best on a machine with bandwidth to spare, such as a small VPS, not your home
  connection.

### A matchmaking server for the public (`--no-sync`, `--quiet`)

```sh
python3 ac-plus-server.py serve --no-sync --relay --quiet
```

- `--no-sync` runs invite codes (and the relay, with `--relay`) only: nothing listens for save sync,
  and no tokens or saves are kept, so nothing is written to disk.
- `--quiet` logs no addresses or invite codes.

This is how the ac-plus project's own server (the game's default **Invite code server**) runs. Open UDP
41331 in the firewall, and nothing else is needed.
