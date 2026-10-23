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
  look and lines, and her telescope for drawing your own constellations in the night sky.
- **The Roost**, the café downstairs from the lobby. It has Brewster, his City Folk lines and
  music, and coffee once a day, made and drunk with City Folk's own animations. Brewster warms up
  to you as you keep coming back.
- **K.K. Slider at the Roost:** from 6 PM to midnight, every day but Saturday, he plays his songs
  on the stage with the lights down.
- **Villagers at the Roost:** one or two of your villagers drop in each day for an hour or two of
  coffee. You can chat with them and do their favors there, as anywhere else.
- **Fishing:** City Folk's plop when a fish takes the float.

## What's new

Besides everything ACGC-PC-Port adds (widescreen, high resolutions, texture packs, keyboard and
mouse, controllers), ac-plus adds:

**Town and world**
- City Folk style curved world and sky outdoors (`world_curve` setting; 0 turns it off).
- City Folk weather: the weather can change during the day, with overcast spells and rainbows
  after rain (`cf_weather` setting).
- Borderless acres: walking between acres doesn't stop the camera (`borderless_acres`).
- The town's clock keeps up with your computer's: putting a PC or Steam Deck to sleep with the game
  open no longer leaves the town behind.

**Patterns**
- Design a Pattern from the pause menu (Esc, or Back on a controller), once you've designed five
  patterns at the Able Sisters' counter.
- **Pattern Storage**, in the pause menu.
- Patterns keep their identity across slots and storage, so swapping slots no longer changes
  shirts, doors, signs or umbrellas made from them.
- **Ground patterns:** "Lay on Ground" in a pattern's menu puts it on the ground in town.
- **Pattern QR codes in New Leaf's format.** Pause menu → Pattern QR Codes saves your patterns
  as images in `qr/export/`, and reads New Leaf design QR codes you put in `qr/import/`.
- Mouse controls in the pattern designer, plus Ctrl+Z to undo (`mouse_mode`).

**Museum**
- Blathers identifies and assesses fossils as soon as you talk to him.
- The observatory and the Roost (see [What comes from City Folk](#what-comes-from-city-folk)).

**Home**
- City Folk style storage: your own dressers, wardrobes and closets open a large shared storage.
- The basement takes wallpaper and carpets. Its rock wall and floor come back to you as items, so
  you can put them back later.

**Items**
- The four NES games that were only available through e-Reader cards (Ice Climber,
  Mario Bros., Super Mario Bros. and The Legend of Zelda) turn up like the others. They're in the
  same prize pools, and Tom Nook's codes work for them.

**Saving and music**
- Save anywhere with **Z + Start** (where the game allows it).
- Save on quit, and an optional autosave every few minutes.
- The `.acplus` file carries a checksum, and the last three copies are kept. A damaged one is set
  aside and the newest good copy is loaded instead.
- Quick load: returning players start straight in town.
- Optional save syncing: through a synced folder (Syncthing, Nextcloud…), or with your own server
  over ssh (`ac-sync.sh`, see [Saves](#saves)).
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
    pattern IDs, constellations, the Roost, basement wallpaper and carpets. Its last three copies
    are kept as `.acplus.bak1` to `.bak3`.

  **Always copy or back up the two files together.**
- **Bringing a Dolphin save:** export it from Dolphin's Memory Card Manager as a `.gci` and put it
  in `save/card_a/`. The `.acplus` file is created the next time you save.
- **Syncing through a folder:** set `cloud_save = 1` and `cloud_save_folder` to a folder that
  Syncthing, Nextcloud or similar keeps in sync. Any save that gets replaced is kept as a
  `.cloudbak` file.
- **Syncing with your own server (ssh):**
  - Turn on Server sync in Settings → Saving and enter your server's address.
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
- **Ground patterns** don't receive shadows from trees and buildings.

**Sky**
- **The curved world** makes far-off acres look emptier: like the original game, it only shows
  trees in the acres around you.
- **Sky events:** fireworks, meteor showers, the UFO and balloons haven't all been checked against
  the City Folk sky yet.

**Museum and Roost**
- **Arriving facing the wrong way:** in the observatory or the café you don't always face away
  from the door you came in by.
- **Celeste:** she has no bow, villagers don't mention constellations yet, and the telescope has
  no mouse controls.
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
- **Libraries:** [qrcodegen](https://www.nayuki.io/page/qr-code-generator-library),
  [quirc](https://github.com/dlbeer/quirc), [dr_libs](https://github.com/mackron/dr_libs) and
  [stb](https://github.com/nothings/stb). Their licenses are in `pc/lib/`.
- **The HD texture pack** linked above is by the Animal Crossing community.

**Nintendo:** Animal Crossing and Animal Crossing: City Folk are Nintendo's. This project isn't
affiliated with Nintendo. It contains none of their game files, and all game content is read from
copies you own.

See [LICENSE](LICENSE) and the [FAQ](FAQ.md).
