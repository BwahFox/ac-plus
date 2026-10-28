# ac-plus

**AI:** AI tools (Claude) were used to write code for this project. No AI-generated art, models or
sounds are used. Everything you see and hear comes from the original games or was made by hand.

**ac-plus** is Animal Crossing (GameCube) on PC, with quality-of-life features and touches of
City Folk.
It's built on [ACGC-PC-Port](https://github.com/flyngmt/ACGC-PC-Port), which runs the
[ac-decomp](https://github.com/ACreTeam/ac-decomp) decompilation natively on PC.

This repository contains no game files. To play, you need your own copies of **both** games:

- **Animal Crossing** for GameCube, **USA version (GAFE01, revision 0)**, as a disc image (`.iso`,
  `.gcm` or `.ciso`).
- **Animal Crossing: City Folk** for Wii, **USA version (RUUE01)**, as a disc image (`.wbfs` or
  `.iso`). ac-plus uses a lot of City Folk: its sky, rooms, characters, animations, lines, music and
  sounds. The game converts them from your disc on your computer, the first time it starts.

To check that your dumps are the right ones, see [Checking your discs](#checking-your-discs).

The game runs on Windows. On Linux and the Steam Deck it runs through Wine or Proton.

## Contents

- [Setting up](#setting-up)
- [Checking your discs](#checking-your-discs)
- [What comes from City Folk](#what-comes-from-city-folk)
- [What's new](#whats-new)
- [Controls](#controls)
- [Settings](#settings)
- [Saves](#saves)
- [Known issues](#known-issues)
- [Building on Windows without a container](#building-on-windows-without-a-container)
- [Credits](#credits)

## Setting up

### 1. Get the game folder

If this repository's **Releases** page has a download, extract the zip somewhere and go on to
step 2.

Otherwise, build it. The build runs in a container, so the only thing to install is
[Podman](https://podman.io) or [Docker](https://www.docker.com):

```sh
git clone <this repository>
cd ac-plus
./ac-plus.sh build
```

The first build takes a few minutes, because it makes the container first. Later builds take
about a minute. The game folder is `dist/ac-plus/`.

On Windows, run these commands in a WSL terminal (with Docker Desktop or Podman Desktop
installed), or see [Building on Windows without a container](#building-on-windows-without-a-container).

### 2. Add your discs

Put both disc images in the game folder's `rom/` folder. Any file names work:

```
ac-plus/
├── AnimalCrossing.exe
├── rom/
│   ├── Animal Crossing (USA).iso
│   └── Animal Crossing City Folk (USA).wbfs
├── save/
└── shaders/
```

- **GameCube:** `.iso`, `.gcm` or `.ciso`.
- **City Folk:** `.wbfs` or `.iso`.

### 3. Start the game

- **Windows:** double-click `AnimalCrossing.exe`.
- **Linux:** run `wine AnimalCrossing.exe` from the game folder.
- **Steam Deck:** in Desktop Mode, add `AnimalCrossing.exe` to Steam as a non-Steam game. Then, in
  its Properties, under Compatibility, force a Proton version. Back in Gaming Mode, it starts like
  any other game.

**The first start takes about a minute longer.** ac-plus converts what it uses from your City Folk
disc into a `cf_sky/` folder, with a progress bar, then the game starts. It only happens once, and
again if a new version of ac-plus needs it.

After that, the City Folk disc can stay in `rom/` or be moved away. The GameCube disc has to stay:
the game reads it each time it starts.

**Converting by hand:** a build from source can also convert City Folk without starting the game,
with `./ac-plus.sh extract-cf "/path/to/City Folk.wbfs"`. This also accepts a folder that Dolphin
extracted the disc's files to. Set `GAME_DIR` to write into another game folder.

## Checking your discs

ac-plus is made for these exact discs, as catalogued by [Redump](http://redump.org), the
disc-preservation database.

| Game | Redump entry | Size | CRC-32 | MD5 | SHA-1 |
|---|---|---|---|---|---|
| Animal Crossing (USA), GAFE01, revision 0 | [5169](http://redump.org/disc/5169/) | 1,459,978,240 bytes | `7be72542` | `7853049ccd0c1e2bffeb5b12393a5409` | `2d2b1fa3883f49af779ce9ca133db3be17be8f32` |
| Animal Crossing: City Folk (USA), RUUE01, revision 0 | [8057](http://redump.org/disc/8057/) | 4,699,979,776 bytes | `2f468135` | `732d7cddfd2f958d30b98ffeb72eeac4` | `a75c7389877b331a520803529798f909b0dd0122` |

- **These hashes are for full `.iso` images.** A `.ciso` (GameCube) or `.wbfs` (Wii) of the same disc
  is smaller and has different hashes, but still works.
- **The easiest way to check:** in [Dolphin](https://dolphin-emu.org), right-click the game,
  choose Properties, then the Verify tab, then Verify Integrity. Dolphin compares the disc with
  Redump's records, so this works for `.ciso` and `.wbfs` files too.
- **Checking a full `.iso` by hand:** `sha1sum <file>` on Linux, or `certutil -hashfile <file> SHA1`
  in a Windows command prompt.
- **The City Folk "Nintendo Selects" re-release** (USA, revision 1, Redump
  [54552](http://redump.org/disc/54552/)) hasn't been tested with ac-plus.

## What comes from City Folk

- **The sky:** a City Folk style sky, with clouds, stars and a moon, over a gently curved world.
- **The museum's observatory**, upstairs from the lobby. Celeste is there, with her City Folk
  look and lines, and her telescope for drawing your own constellations in the night sky (with
  the mouse too: click stars to link them, the wheel or the screen's edge to look around).
- **The Roost**, the café downstairs from the lobby. It has Brewster, his City Folk lines and
  music, and coffee once a day, made and drunk with City Folk's own animations. Brewster warms up
  to you as you keep coming back.
- **K.K. Slider at the Roost:** from 6 PM to midnight, every day but Saturday, he plays his songs
  on the stage with the lights down.
- **Villagers at the Roost:** one or two of your villagers drop in each day for an hour or two of
  coffee. You can chat with them and do their favors there, as anywhere else.
- **Fishing:** City Folk's plop when a fish takes the float.
- **TV programs:** switched-on TVs show City Folk's programs on its weekly schedule: the news,
  the weather report with a forecast of the coming hours, a hero show, soccer, a movie on weekend
  nights, and static after the late show, each with City Folk's own sound (`cf_tv` setting).

## What's new

Besides everything ACGC-PC-Port adds (widescreen, high resolutions, texture packs, keyboard and
mouse, controllers), ac-plus adds:

**Online visits (experimental)**
- **Visit a friend's town, or host yours**, for up to four players: talk to Porter at the train
  station and choose **Online**. A visit leaves on his train, and you step off it at your friend's
  station. To go home, tell their Porter "Go home". Once you're online, the pause menu's Online Play
  has the rest (chat, visitors, stop). Hosting gives you an **invite code** for your friends to
  type in. No router setup is needed for most networks: the ac-plus server introduces the two
  games, and relays the visit when they can't reach each other directly.
- Settings → Online → **Invite code server** chooses that server: the ac-plus project's by default,
  your own (it's the same one-file server, see [its guide](server/README.md)), or none. Without
  one, visit by address instead (`192.168.1.20:41330` on your own network; from outside, the host
  forwards UDP port 41330 on their router).
- **Open to everyone** (off by default) lists your town in Browse open towns while you host.
  Otherwise only people you give the code to can find it.
- The host's town is the real one: what visitors pick up, buy, plant or donate goes through the
  host's game, and only the host's save changes. Visitors are guests (no digging, chopping or
  picking flowers) until the host trusts them in Online Play, and the host can send anyone home.
- Shared with everyone in the town: items on the ground, trees, holes, flowers, the shop's stock,
  the museum, furniture indoors and out, switches (TVs, lamps, stereos, instruments), the town's
  time and weather, where villagers are, fish and insects, and each player's clothes and face.
  Two players can't talk to the same villager or sit on the same seat at once.
- A purchase comes off the shelves at once for everyone in the shop, candy and grab bags included.
- Villagers remember a visitor from one visit to the next.
- Chat from the Online Play menu (Say), or press **T** (keyboard) or **LB / L1** (controller) to start
  typing a line right away. Enter sends it and takes you back to the game. Change the button in
  Settings > Controls > Keybindings ("Chat (online)").
- Connections are encrypted, and each game has its own key (shown in Online Play). Your town is
  only open while you host.
- **Nothing is lost if a visit drops:** during a visit your character (pockets, Bells, clothes and
  all) is kept in `save/online_passport.bin`. If the game closes or the connection is lost, the
  next start brings you home with everything you had.

**Town and world**
- City Folk style curved world and sky outdoors (`world_curve` setting; 0 turns it off).
- City Folk weather: the weather can change during the day, with overcast spells and rainbows
  after rain (`cf_weather` setting).
- Clear days on request: list dates in `weather_clear_days` (for example `7/4,12/31`) and the
  weather stays clear on them.
- Fireworks and meteor showers light up the City Folk sky, not only their reflection in the water.
- Borderless acres: walking between acres doesn't stop the camera (`borderless_acres`).
- The town's clock keeps up with your computer's: putting a PC or Steam Deck to sleep with the game
  open no longer leaves the town behind.

**Furniture outdoors**
- **Put furniture down in town**, New Horizons style: anywhere that takes a ground pattern, on
  level ground. Push, pull, turn and pick it up as at home. Pieces bigger than one square work too.
- Tables outdoors hold items and small furniture, so an NES or a radio can sit on one.
- Stereos play their K.K. song to anyone nearby; walk away and the hourly music comes back.
  Instruments and gyroids follow the K.K. song, not the hourly music.
- Fish and insects can be placed straight from your pockets.
- Spots kept for a villager's house stay clear. If a house is later built where furniture
  stood, the furniture (and what's on it) waits for you at the police station's lost and found.

**You**
- **Skin tones**, as Animal Crossing Deluxe has them: eight to choose from. Rover asks a new
  resident on the train, and checking a mirror at home changes it any time. Works with the HD
  texture pack's face.
- Tanning is now a setting (Settings → Gameplay → Tanning), off by default.
- Sit on a chair by walking into its side, not only its front. Getting up, you step off whichever
  side is free.

**Patterns**
- Design a Pattern from the pause menu (Esc, or Back on a controller), once you've designed five
  patterns at the Able Sisters' counter.
- **Pattern Storage**, in the pause menu.
- Patterns keep their identity across slots and storage, so swapping slots no longer changes
  shirts, doors, signs or umbrellas made from them.
- **Ground patterns:** "Lay on Ground" in a pattern's menu puts it on the ground in town. Shadows
  fall on them as on the grass.
- **Pattern QR codes in New Leaf's format.** Pause menu → Pattern QR Codes saves your patterns
  as images in `qr/export/`, and reads New Leaf design QR codes you put in `qr/import/`.
- Mouse controls in the pattern designer, plus Ctrl+Z to undo (`mouse_mode`).

**Museum**
- Blathers identifies fossils himself, as in Animal Crossing Deluxe: once 15 fossils are in the museum he gets his certification, and his menu offers "Identify fossils" (mark several with X to hand them over together).
- The observatory and the Roost (see [What comes from City Folk](#what-comes-from-city-folk)).
- Fish and insects you've donated get the map's museum marker in your collection pages.
- At the Roost, A by the stool always sits you down, without starting a chat with Brewster first.

**Home**
- Ask Tom Nook to repaint your roof any time: "Other things" → "About my house". It costs what a can of his paint does, and the new color is there the next day.
- **New Leaf style extra rooms:** once your house is fully paid off (big main room, upstairs and
  basement), Tom Nook offers more: a room off the left, the right and the back of the main room,
  each 6×6 at first and then grown to 8×8. Each one is built overnight, adds a wing to the house
  outside, and is paid off like any other loan. The statue comes after the last one. If something
  is in a doorway's way, it stays where it is for you to move. The Happy Room Academy and feng
  shui count them like the upstairs.
- City Folk style storage: each of your dressers, wardrobes and closets has its own large storage
  (up to 128 in a town). Storage from older versions turns up in the first one you open.
- With the HD texture pack, wallpaper no longer stretches after you've browsed the catalog.
- The basement takes wallpaper and carpets. Its rock wall and floor come back to you as items, so
  you can put them back later, or use them in any other room. Both are in Tom Nook's catalog.

**Items and Bells**
- **90,000 Bells bags**, as Animal Crossing Deluxe has them. Make one from your wallet's menu once
  you have 90,000 Bells. The bank, shops, the loan and the gyroids take them, and a full wallet
  overflows into them. Bury one in a glowing spot for a money tree that drops three of them.
- The post office's savings account is open from the start. You don't have to pay off the loan
  first. While you still owe Tom Nook, the menu offers both "Pay loan" and "Savings".
- The four NES games that were only available through e-Reader cards (Ice Climber,
  Mario Bros., Super Mario Bros. and The Legend of Zelda) turn up like the others. They're in the
  same prize pools, and Tom Nook's codes work for them.

**Updates**
- **Updates itself:** at startup, ac-plus looks for a newer release and offers it, with its notes:
  Update now, Later or Skip this version. Updating keeps your saves, settings, music and texture
  packs. The files it replaces are kept in `update/previous/`. Turn it off with `update_check = 0`;
  a fork can point `update_repo` at its own releases.
- Only releases signed with the ac-plus release key are installed, so a tampered or fake download
  is refused before anything is changed.
- The title screen shows which version you have.
- **Saves from a newer version:** if your town was last saved by a newer ac-plus, the game warns you
  before you play it, and offers to quit so you can update first.

**Saving and music**
- Save anywhere with **Z + Start** (where the game allows it).
- Save on quit, and an optional autosave every few minutes.
- The `.acplus` file carries a checksum, and the last three copies are kept. A damaged one is set
  aside and the newest good copy is loaded instead.
- Quick load: returning players start straight in town.
- **Custom music** (Settings → Audio) switches between your music pack and the game's own music,
  and **Rain music** picks the rain song or the hour's spring arrangement in the rain.
- **Save syncing built in:** with your own small ac-plus-server (one Python file, see
  [Saves](#saves)), the game downloads your town at startup when another machine saved it last, and
  uploads after every save with **Save Upload Complete** on screen. If two machines both have new
  progress, it asks which one to keep. Also possible: a synced folder (Syncthing, Nextcloud…), or
  ssh (`ac-sync.sh`).

**Fixes**
- Opening the map indoors could crash the game; it now shows your house's acre.
- Picking up a 90,000 Bells bag only gave 200 Bells when it fit in the wallet. It now gives
  90,000, or goes into your pockets as a bag when the wallet is too full.
- Returning home from a town on memory card B could crash the game.
- At the Roost, getting up just as Brewster started talking left you free to walk during the talk.
- On Linux with an NVIDIA card, the game's memory no longer creeps up while it runs (from the
  second start: the first one switches Wine to the steadier way of drawing for this game).
- **Custom hourly music:** replace the 24 hourly tracks with your own files (see
  [the music guide](docs/MUSIC.md)).

## Controls

| Keyboard | GameCube |
|---|---|
| W A S D | Control stick |
| Arrow keys | C stick (camera) |
| Space | A |
| Left Shift | B |
| X / Y | X / Y |
| Enter | Start |
| Z | Z |
| Q / E | L / R |
| I J K L | D-pad |
| Esc | Pause menu (ac-plus) |
| F4 | Tile readout (why a tile won't take a ground pattern) |

- **Rebinding:** keys can be changed in `keybindings.ini`, next to the exe. Mouse buttons can be
  bound too.
- **Controllers:** SDL controllers work and can be plugged in at any time. **Back / Select** opens
  the pause menu.

## Settings

Most settings are in the pause menu (**Esc** → Settings). They're kept in `settings.ini` next to
the exe, which also explains each one. To reset a setting, delete its line; the game writes the
default back.

- **Custom music:** put your own tracks in `music/<pack>/hourly/00` to `23` and set
  `music_pack = <pack>`. The [music guide](docs/MUSIC.md) explains it step by step, with rain and
  snow versions and loop points.
- **Texture packs:** Dolphin format, in `texture_pack/`. The
  [Animal Crossing HD texture pack](https://forums.dolphin-emu.org/Thread-animal-crossing-hd-texture-pack-version-23-feb-22nd-2026)
  works.
- **Command line:** `--verbose` logs what the game does, `--time HOUR` sets the in-game hour, and
  `--no-framelimit` unlocks the frame rate.

## Saves

- **Where they are:** your town is in `save/card_a/`, as two files:
  - `DobutsunomoriP_MURA.gci`, a normal GameCube save that Dolphin and save editors understand;
  - `DobutsunomoriP_MURA.gci.acplus`, with everything ac-plus adds: ground patterns, storage,
    pattern IDs, constellations, the Roost, basement wallpaper and carpets, the extra rooms, skin tones, 90,000 Bells money trees. Its last three copies
    are kept as `.acplus.bak1` to `.bak3`.

  **Always copy or back up the two files together.**
- **Saves and older versions:** a town saved by a newer ac-plus warns you before an older one plays
  it. If you play on anyway, what the older version doesn't know is kept, not thrown away. (This
  works from Alpha 6 on: older releases don't warn.)
- **Bringing a Dolphin save:** export it from Dolphin's Memory Card Manager as a `.gci` and put it
  in `save/card_a/`. The `.acplus` file is created the next time you save.
- **Syncing through a folder:** set `cloud_save = 1` and `cloud_save_folder` to a folder that
  Syncthing, Nextcloud or similar keeps in sync. Any save that gets replaced is kept as a
  `.cloudbak` file.
- **Syncing with an ac-plus-server (built in):** run the small server in `server/` (see
  [its guide](server/README.md)), then in Settings → Saving turn on In-game sync and enter the
  Sync server (`host:port`, or a full `https://` address) and the Sync token (Ctrl+V pastes). The
  game downloads a newer save at startup, asks if two machines both have new progress, and uploads
  after every save, showing **Save Upload Complete** when it's safe to close. Every save is uploaded
  as it happens, so Steam's "Exit Game" leaves nothing behind.
- **Syncing with your own server (ssh):**
  - Turn on SSH sync in Settings → Saving and enter your server's address (SSH server).
  - Start the game through `ac-sync.sh`: `./ac-sync.sh wine AnimalCrossing.exe`, or on a Steam Deck,
    launch options `<game folder>/ac-sync.sh %command%`.
  - It downloads your town before playing and uploads it afterwards, keeping older copies on both
    sides.
  - It needs ssh login with a key, without a password prompt.

## Known issues

**General**
- **Only the USA versions are supported:** GAFE01 revision 0 for the GameCube game, RUUE01 for
  City Folk. Other versions won't work.
- **No Linux build yet:** on Linux the Windows build runs through Wine or Proton. To keep the
  desktop from dimming or sleeping, the game starts `ac-plus-inhibit` (a small Linux program in the
  game folder) while it runs; if that file is missing, the screen may still go to sleep.
- **Steam's "Exit Game" closes the game without saving.** Quit from the pause menu instead. Autosave
  covers you if you forget.

**Saving and patterns**
- **Quick load** only works for the town's first resident.
- **Outdoor furniture:** up to 128 pieces per acre. A town saved by Alpha 5 or older loses its
  outdoor furniture, so update every machine you sync between.
- **Skin tones:** on a train trip to another town, your character shows the default skin there.

**Sky**
- **The curved world** makes far-off acres look emptier: like the original game, it only shows
  trees in the acres around you.
- **Sky events:** the UFO and balloons haven't been checked against the City Folk sky yet.

**Online visits**
- **Experimental:** expect rough edges, and back up both save files before hosting.
- Every player needs the same ac-plus version, down to the point release (Alpha 9.1 can't visit
  Alpha 9.2). The game says so when they differ.
- Some things aren't shown to the other players yet: their footsteps and tool sounds, trees
  falling, and the dust of digging. Online trips skip the ride inside the train: you board at home
  and step off at your friend's station.
- A signboard design the host puts up during a visit shows for players who arrive after that.
- **Over the internet:** visits between different home networks work, but have had less testing.
  If a visit won't connect, a VPN such as Tailscale or ZeroTier puts everyone on one network: then
  visit by the host's VPN address.

**Museum and Roost**
- **Celeste:** she has no bow, and villagers don't mention constellations yet.
- **The Roost:** K.K. Slider can't be talked to there, and he doesn't take song requests.

## Building on Windows without a container

The original MSYS2 build still works:
1. Install [MSYS2](https://www.msys2.org/).
2. In the **MSYS2 MINGW32** shell:

   ```sh
   pacman -S mingw-w64-i686-gcc mingw-w64-i686-cmake mingw-w64-i686-SDL2 mingw-w64-i686-make
   ./build_pc.sh
   ```

3. The game is in `pc/build32/bin/`. Copy the `shaders/` folder along if you move the exe.

Builds made this way don't carry the City Folk converter that runs on the first start. Convert the
disc yourself with Python 3 and `numpy`:
`python pc/tools/extract_cf_sky.py <City Folk disc> <game folder>`.

## Credits

- **[ACreTeam](https://github.com/ACreTeam):** the ac-decomp decompilation everything is built on.
- **[flyngmt](https://github.com/flyngmt/ACGC-PC-Port):** ACGC-PC-Port, the PC port ac-plus is
  built on, and its contributors. AloXado320's mouse support in menus and the pattern designer
  came in through it.
- **Cuyler36's Animal Crossing Deluxe:** the skin tones follow it, with its eight tones' colours
  and its tanning on top of them. The 90,000 Bells bag comes from it too.
- **Libraries:** [qrcodegen](https://www.nayuki.io/page/qr-code-generator-library),
  [quirc](https://github.com/dlbeer/quirc), [dr_libs](https://github.com/mackron/dr_libs) and
  [stb](https://github.com/nothings/stb). Their licenses are in `pc/lib/`.
- **The HD texture pack** linked above is by the Animal Crossing community.

**Nintendo:** Animal Crossing and Animal Crossing: City Folk are Nintendo's. This project isn't
affiliated with Nintendo. It contains none of their game files, and all game content is read from
copies you own.

See [LICENSE](LICENSE) and the [FAQ](FAQ.md).
