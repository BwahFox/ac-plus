# Online visits

Status: **prototype**. Up to three visitors at a time, by address, invite code, the server's list of open towns,
or a town visited before.

## Playing

Pause menu > **Online Play**.

- **Host my town**: your town is open to up to three visitors. The page shows the address visitors on your network type
  (`192.168.x.y:41330`) and your key. For visitors from outside your network, forward UDP port 41330 (Settings:
  `mp_port` in `settings.ini`) on your router to your computer.
- **Invite codes**: with a server set (`mp_server` in `settings.ini`, see `server/README.md`), hosting also gets a
  six-letter invite code from it, shown on the page. A visitor types the code instead of an address. The server
  only introduces the two games (both send to each other at once, which gets through most home routers) and
  checks that the host is the one who asked for the code; the visit itself doesn't go through it. If the
  two games can't reach each other within a few seconds (CGNAT, strict routers) and the server runs with
  `--relay`, the visitor calls again through the server, which passes on the encrypted packets without being
  able to read them.
- **Browse open towns**: the towns hosted with `mp_public = 1` on your server; pick one to visit it.
- **Towns you've visited**: every town you visit is remembered (`save/multiplayer_friends.txt`, its host's key and
  names). Pick one to visit it again if it's being hosted on your server, no code needed; Left forgets it. The
  connection only goes through if the town has the key you remember.
- **Say**: while hosting or visiting, a line of text for everyone, shown at the top of the screen.
- **Visit**: type the host's address (`address` or `address:port`) or invite code, then **Go visit**. You arrive at the host's
  station, no train ride. **Leave the visit** takes you home again; so does the host closing their town.
- While visiting, the town runs on the host's clock and weather. You keep your own pockets and wallet; what you
  pick up goes home with you.
- As on the GameCube, you are away from home while visiting. If your game is closed during a visit (not by
  leaving), your character comes home with the gyroid face and empty pockets, as when the GameCube was switched off
  on the train.

While hosting, the page lists your visitors. A on a name opens their page: each is a **guest** until you mark them
**trusted**, and **Send ... home** ends their visit.

What a guest may do:
- pick up, put down and swap items and furniture on open ground;
- shake fruit trees, and trees with the town's chance drops (bells, furniture, bees, presents). Money trees
  stay the owner's;
- pull weeds.

A guest's shovel and axe bounce off: no digging, filling holes or chopping, and nothing buried can be taken. The host's game also checks every change
a visitor sends and puts back anything a guest may not do. A trusted visitor may dig, chop, plant and pick like the owner, but even their changes must be things
playing can make (items, furniture, weeds, holes, stumps, trees, saplings, flowers) on both sides of the change:
never buildings, signs, fences, rocks, doors or placeholders (`pc_mp_fg_natural_ok`). Ids like those, put where
they don't belong, were Wild World's "brick seeds".

## How it works

The host's game is authoritative. The code is in four layers:

| Layer | Files | Job |
|---|---|---|
| Wire | `pc/src/mp/mp_wire.c` | The only code that reads bytes from the network. Turns a frame into checked, fixed-size structs, or rejects it whole. |
| Crypto | `pc/src/mp/mp_noise.c` | Handshake and packet encryption (Monocypher, vendored in `pc/lib/monocypher`). |
| Link | `pc/src/mp/mp_link.c` | UDP, peers, reliable messages in order, latest-only states, keepalives, timeouts, relay routes. |
| Matchmaking | `pc/src/mp/mp_mm.c` | The server's packets (`server/ac-plus-server.py` documents them), fixed sizes. |
| Game | `pc/src/pc_mp*.c`, `src/actor/ac_mp_puppet.c` | Sessions, the town transfer, keeping the town the same, puppets, villagers, the menu. |

- **Puppets** (`ac_mp_puppet.c`). The engine has one player actor, so other players are puppets. A puppet is the
  player skeleton and animations, drawn with the other player's face, skin and the shirt texture they wear, and
  posed from their state 60 times a second. The tool in their hand is drawn at the puppet's hand from the
  game's own item models and animations; an umbrella is the umbrella actor villagers carry.
- **The town** (`pc_mp_town.c`). When a visitor joins, the host sends its town in our own field-by-field layout,
  not a save file:
  - names, acres, items on the ground, buried flags;
  - house outsides and rooms, villagers, the shop;
  - a few settings;
  - ground patterns with their designs, and outdoor furniture.

  The visitor builds a town from it on top of a copy of its own save. Every id the game would index with is checked
  against the size of the game's own table (`pc_mp_items.c`); anything wrong becomes something harmless. Villagers
  are made new from their ids with the game's defaults; nobody's mail, memories or private data is sent. The trip
  is the GameCube's passport visit (`pc_m_card.c pc_mcd_online_depart/return`). The visited town is never written
  to disk.
- **Keeping it the same** (`pc_mp_world.c`). Both games compare the town's items and buried flags with the last
  frame.
  - The host's changes go to the visitor.
  - A visitor's change is a request. The host checks the visitor's trust, the ids, and that what the visitor saw is
    still there, then applies it or tells the visitor what is really there.
  - Buildings on screen put placeholders on their units; those are never sent.
  - The shop's goods are kept the same way. A sale turns an item into a "sold" mark. A visitor's purchase is a request
    too; if someone else bought it first, the host refuses and the visitor's game gives back the item and the bells.
    Shelves show a change the next time someone walks in.
  - The museum comes with the town, and the host's donations are passed on. Visitors can't donate, as on the
    GameCube.
  - Houses: the host's rearranging is passed on (a room shows it the next time someone walks in). Visitors can go in
    but don't move the host's furniture, indoors or out, trusted or not.
- **Several visitors.** Visitors only talk to the host. The host passes each visitor's position and look on to
  the others, and tells them when one goes home (LEFT). Each visitor gets the town as it was when they joined, and
  the changes from then on.
- **Villagers** (`pc_mp_npc.c`). Who moves a villager depends on who is near them. If someone is talking to them,
  that game does. Otherwise the host does if it has them loaded, else the first visitor who has. The host tells
  each visitor where the villagers it doesn't move are, and whether they're walking, running or standing.
- **Clock** (`pc_os.c pc_os_mp_set_clock`). The visitor reads the host's town time, sent every 5 seconds.

## Security

The goal: nobody can run code on another player's computer, or damage their saves, through a visit.

- **Encryption and authentication.** Every packet after the handshake is encrypted and authenticated
  (XChaCha20-Poly1305). A packet whose tag doesn't check, or that was seen before (replay window), is dropped
  before anything reads it.
- **Handshake.** Noise XX over X25519 and BLAKE2b. Each install has a key (`save/multiplayer_id.bin`), shown as a
  fingerprint in the menu, so players can check who they're talking to.
- **Spoofing.** A stateless cookie bound to the sender's address comes first, so spoofed packets can't make a host
  do work or send more than it got.
- **Untrusted input.** Everything that arrives goes through `mp_wire.c`:
  - fixed sizes; lengths checked before use;
  - floats finite and inside the world;
  - every id checked again by the game against its own tables;
  - text in the game's charset with control codes removed, never a format string.

  An authenticated peer that sends a malformed frame is disconnected.
- **No files over the network.** No saves, no code. Patterns are fixed-size pixel data.
- **Visitor slots.** A visitor's slot comes from the host's records, never from what the visitor says.
- **Plausible changes.** The host takes a visitor's change only if it's next to where the visitor stands (purchases:
  inside a shop) and comes no faster than ten a second.
- **The host's data.** Everything in the town stream is checked against the values the game itself makes, not just
  the size of a table (a house is never bigger than the game builds, weather types and strengths, house order).
  Villagers' positions stay inside the town. A tool's skeleton is only posed by its own animations.
- **Saves.** While visiting, nothing writes a save file: the town in memory is someone else's.
- **Testing.** `pc/tools/mp-test/run.sh` builds the layers natively with ASan and UBSan. It checks the crypto
  against libsodium, tampers with every byte of the handshake, round-trips every message, and runs a real link over
  loopback with 25% packet loss. `run.sh fuzz 60` runs libFuzzer on the frame decoder and the handshake and packet
  parsers.
- **Build hardening.** The PC layer is built with `-fstack-protector-strong -D_FORTIFY_SOURCE=2`.

## Protocol (version 3)

All integers are little-endian. UDP, default port 41330.

**Packets** (`mp_noise.c`; the index is a random u32 each side picks for the session):

```
INIT    u8 1, u32 sender, "ACMP", u16 version, e[32], cookie[16] (zero first), zero padding to 128 bytes
COOKIE  u8 2, u32 receiver, cookie[16]
RESP    u8 3, u32 receiver, u32 sender, e[32], enc(s)[48], enc()[16]
FIN     u8 4, u32 receiver, enc(s)[48], enc()[16]
DATA    u8 5, u32 receiver, u64 counter, ciphertext, tag[16]   (the 13 header bytes are authenticated)
```

The handshake is Noise XX (`-> e; <- e, ee, s, es; -> s, se`), protocol name
`ac-plus Noise_XX_25519_XChaChaPoly_BLAKE2b`, prologue `"ACMP"` + version. h and ck are BLAKE2b-512, HKDF uses keyed
BLAKE2b, and the nonce is the counter in the first 8 bytes of the 24.

**Frames** (inside DATA, at most 1160 bytes, `mp_wire.c`):

```
u32 ack, u64 selective ack, u8 count, then count messages:
u8 type, u8 flags (bit 0 reliable), [u32 seq if reliable], u16 body length, body
```

PSTATE and NPCS are unreliable; everything else is reliable and in order. A frame carries the latest PSTATE of each
player the sender passes on. A message's body has exactly its type's
size, apart from BULK and CHAT, which have their own length.

| Type | Body |
|---|---|
| 1 PSTATE | player, scene, field, flags, x, y, z, angle, anim0, anim1, frame0, frame1, speed, part table, eye, mouth, item main, item model, item animation, umbrella, item frame, tick |
| 2 JOIN | version, wanted slot, LOOK |
| 3 WELCOME | slot, permissions, town name, town stream size |
| 4 LOOK | slot, name, town, gender, face, skin, tan, cloth, shirt texture (512), palette (16 x u16) |
| 5 BULK | stream, total, offset, length, data (up to 960) |
| 6 CLOCK | year, month, day, hour, min, sec, weekday, weather, intensity |
| 7 BYE | reason (quit, full, version, refused, kicked, error) |
| 8 CHAT | player, length, text (up to 32) |
| 9 ACTION, 10 WORLD | kind (1 unit, 2 buried flags, 3 shop slot, 4 museum byte, 5 house unit, 6 furniture switch), player, a, b, old, new, request, ok |
| 11 NPCS | count, then per villager: index, flags (loaded, talking), x, y, z, angle, animation |
| 12 LEFT | slot (1-3): that visitor went home |

Town stream version 4 (adds each house's door design): see `pc_mp_town.c` (`town_pack` / `pc_mp_town_unpack`).

## Testing on one computer

`~/Documents/ac/test-harness/mpvisit.sh` (dev machine only) starts a hidden host and a hidden visitor and waits
until the visitor arrives. Without menus: `PC_MP_HOST=port` hosts, `PC_MP_JOIN=address:port` visits.
`PC_MP_DEBUG=1` logs the slots and villagers, `PC_MP_TRUST=1` trusts visitors, and `PC_MP_RELAY=1` makes a visitor
joining by code go through the server's relay. `mpcode.sh` is the same test joining by invite code
(`VISIT_ARGS=PC_MP_RELAY=1` for the relay).

## Not done yet

- **Building footprints:** a building only marks its units while it's on screen, so a modified visitor's game standing
  next to one could put an item where the building stands (it stays hidden under it; nothing breaks).
- **Puppets:** a custom-pattern umbrella shows the Able Sisters' design instead of the player's own; the fishing
  line and bobber aren't drawn.
- **Villagers:** only walking, running and standing are kept the same between games (other animations, like
  greetings, are each game's own); villagers nobody has loaded aren't kept the same.
