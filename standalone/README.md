# Neon Lounge

A standalone native PS3 music-player app (title ID `SVIZ00001`), built with
PSL1GHT, SDL2, minimp3 and the RSX/GCM display backend. This is the app track
of PS3-Music-Visualizers, separate from the built-in Visual Player resources
in the rest of this repo.

## What it does

- MP3 playback from the internal HDD (`/dev_hdd0/music`, `/dev_hdd0`) and USB
  (`/dev_usb000`, `/dev_usb001`), with folder browsing and a playlist.
- An audio-reactive spectrum visualizer over a 7-slide artwork gallery
  (12-second auto-advance, Left/Right to step).
- Artwork options (Artwork section): view the gallery, add BMP artwork from
  HDD/USB (1280x720 or smaller, 8 MB max, 7 slides max), remove the current
  artwork (moved to a backup folder, not deleted), and a list of sites to
  get pictures from (X, DeviantArt, Pixiv, ArtStation, Zerochan, Pinterest).
- Check for updates (Settings section): fetches `standalone/update.json`
  from this repo, and if a newer version exists, downloads the new
  `EBOOT.BIN` from the matching GitHub release, verifies its SHA-256,
  backs up the current `EBOOT.BIN`, and installs the update. Restart the
  app to run the new version.

Controls: X select, O back, START pause, L1/R1 previous/next track,
L2/R2 volume, Left/Right artwork (in the gallery) and volume (in Settings).

## Building

The build runs in WSL with the PS3 SDK at `/opt/ps3sdk`:

```sh
cd standalone
make           # builds neon_lounge.self
make pkg       # builds neon_lounge.pkg
make host-test # host unit tests (spectrum, library, display guard, system
               # state, thread stacks, clock, update)
```

`update.c` links curl/mbedTLS for the update check; the version compare,
manifest parsing and SHA-256 code are host-testable without the PS3 SDK.

## Assets are not in this repo

Fonts, icons, backgrounds and gallery artwork live in `local/` (git-ignored)
and are not published here. A fresh checkout needs `local/package/` populated
with `USRDIR/art/`, `USRDIR/fonts/` and `local/ICON0.PNG` before `make pkg`
produces a working package. Do not commit artwork you do not have the rights
to publish.

## Pushing an update

1. Bump `NL_VERSION` in `src/update.h` (for example `1.00` -> `1.01`).
2. Rebuild: `make pkg`. Take the new NPDRM `EBOOT.BIN` from
   `build/pkg/USRDIR/EBOOT.BIN` and record its SHA-256.
3. Create a GitHub release tagged `neon-lounge-<version>` (for example
   `neon-lounge-1.01`) and attach that `EBOOT.BIN` as a release asset.
4. Update `standalone/update.json` with the new version, the release
   download URL for `EBOOT.BIN`, and its SHA-256, then push.
5. On the PS3, Settings > Check for updates picks the new version up.

The app checks the downloaded file's SHA-256 against `update.json` before
installing, and keeps a backup of the previous `EBOOT.BIN` as
`EBOOT.BIN.bak` in the app folder. TLS certificate verification is off in
the first version of the updater; the SHA-256 check guards against
corruption and bad downloads, not a determined network attacker.
