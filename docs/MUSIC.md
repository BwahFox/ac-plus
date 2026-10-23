# Replacing the music

ac-plus can play your own music files instead of the game's hourly music: the 24 tracks that
play outdoors, one for each hour of the day. A set of replacement tracks is called a **music
pack**.

The rest of the game's music stays as it is: shops, events, K.K. Slider, the title screen, and so on.

## 1. Make the folder

In the game folder, next to `AnimalCrossing.exe`, there's a `music/` folder. Make a folder inside it
for your pack, and an `hourly/` folder inside that:

```
ac-plus/
├── AnimalCrossing.exe
└── music/
    └── my-pack/
        └── hourly/
            ├── 00.ogg      midnight
            ├── 01.ogg      1 AM
            ├── ...
            ├── 12.ogg      noon
            ├── ...
            └── 23.ogg      11 PM
```

- **File names** are the hour on a 24-hour clock, always with two digits: `00` to `23`.
- **Formats:** `.ogg`, `.mp3`, `.flac` or `.wav`. Formats can be mixed in one pack.
- **Partial packs work:** you don't need all 24. Any hour without a file plays the game's own
  track.

## 2. Rain and snow (optional)

The game has separate versions of the hourly music for rain and snow. To replace those too, add
`rain/` and `snow/` folders next to `hourly/`, named the same way:

```
my-pack/
├── hourly/   00.ogg ... 23.ogg
├── rain/     00.ogg ... 23.ogg
└── snow/     00.ogg ... 23.ogg
```

- **One track for all hours:** to use the same rain track at every hour, name it `rain/all.ogg`.
  `snow/all.ogg` does the same for snow.
- **What plays in rain or snow:** first that hour's rain (or snow) file, then `all`, then the
  hour's `hourly/` file, then the game's own track.

## 3. Turn it on

Open `settings.ini` (next to the exe) and set the pack's folder name:

```ini
music_pack = my-pack
```

- **Volume:** `music_volume` (0–100) sets your pack's volume, separately from the game's
  `master_volume`.
- **Restart the game** after changing these.
- **Turning it off:** leave `music_pack` empty to go back to the game's own music.

## Looping

- **By default:** a track plays to the end and starts again from the beginning.
- **To loop from a point inside the track** (for example, to skip an intro), make a text file
  next to it with the same name plus `.loop`. For example, `hourly/08.ogg.loop` goes with
  `hourly/08.ogg`.
- **What goes in the `.loop` file:** one number, the point to jump back to, counted in samples at
  32,000 per second. For a loop that starts 4.5 seconds in, that's 4.5 × 32000 = `144000`.

## How it behaves

- **Same timing as the original:** your tracks change on the hour, fade when you go indoors and
  duck under sounds, just like the game's music.
- **Loading:** each track is loaded the first time it's needed. A very long file can take a
  moment.
- **Files that won't play:** if a file can't be read, the game plays its own track for that hour
  instead.
- **Checking what plays:** start the game with `--verbose` to log each track as it loads.

## Sharing a pack

A pack is just its folder. To share one, zip `music/my-pack/`. Whoever receives it unzips it
into their own `music/` folder and sets `music_pack`. Only share music you're allowed to share.
