# Neon Lounge

**Current version: 1.20** — [GitHub release](https://github.com/richard1222/PS3-Music-Visualizers/releases/tag/neon-lounge-1.20) · [Brewology listing](https://store.brewology.com/ps3/neon-lounge/)

A standalone native PS3 music-player app (title ID `SVIZ00001`), built with
PSL1GHT, SDL2, minimp3 and the RSX/GCM display backend. This is the app track
of PS3-Music-Visualizers, separate from the built-in Visual Player resources
in the rest of this repo. The interface wears an original-Xbox-dashboard
theme, with a spinning mini Xbox console in the menu.

## What it does

- MP3 playback from the internal HDD (`/dev_hdd0/music`, `/dev_hdd0`) and USB
  (`/dev_usb000`, `/dev_usb001`), with folder browsing and a playlist.
  Playlists can be sorted (Name A-Z / Z-A, Newest, Oldest), shuffled with a
  history-aware queue, and every play is counted — the playlist view builds
  a Most Played queue from your top 100. `.m3u` / `.m3u8` playlist files
  appear in the browser as `[Playlist]` entries and load as the play queue.
  Repeat modes (off / repeat all / repeat one) apply to local music,
  podcasts and YouTube alike, and Settings can save the current queue as
  an `.m3u` playlist file.
- Internet Radio (Radio section): eleven built-in stations — nine SomaFM
  channels (Groove Salad, Lush, Secret Agent, Drone Zone, DEF CON Radio,
  Boot Liquor, Sonic Universe, Underground 80s, Ill Street Lounge), Radio
  Paradise and KEXP. Add your own in `USRDIR/stations.txt`, one per line:
  station name, a TAB, then the stream URL.
- YouTube: search and play music videos and audio, decoded on the console
  itself. Triangle on a result adds it to your playlist, Square saves a
  favorite; playlists and favorites persist between sessions.
- PC Audio: stream whatever is playing on a Windows PC on the same LAN
  (see the PC Audio section below).
- Podcasts (Podcasts section): subscribe to any show by RSS feed URL
  (typed in-app, or listed in `USRDIR/podcasts.txt`), or search the
  iTunes podcast directory from inside the app — no account or API key
  needed. Episodes list newest-first and stream through the same EQ and
  visualizers as everything else. Playback positions are saved as you
  listen (`USRDIR/podcast_positions.txt`), so an episode resumes where
  you stopped it. Episodes stream as MP3; shows that publish only
  AAC/`.m4a` episodes are marked unplayable instead of failing silently.
- A real 5-band parametric EQ (+/-12 dB per band) with six built-in presets
  (Flat, Bass Boost, Treble Boost, Vocal, Bright, Warm) and three custom
  slots you can save yourself. The EQ applies to local music, radio,
  YouTube and PC Audio alike.
- Audio-reactive visualizers — Bars, Peak Rain, Jewel Pulse, Radial
  Sunburst and Waveform (an oscilloscope-style trace) — over a 7-slide
  artwork gallery (12-second auto-advance, Left/Right to step).
- Artwork options (Artwork section): view the gallery, add BMP artwork from
  HDD/USB (1280x720 or smaller, 8 MB max, 7 slides max), remove the current
  artwork (moved to a backup folder, not deleted), and a list of sites to
  get pictures from (X, DeviantArt, Pixiv, ArtStation, Zerochan, Pinterest).
  A built-in FTP server accepts artwork uploads with the same rules.
- Sleep timer (Settings): Off / 15 / 30 / 45 / 60 / 90 minutes, with a
  10-second volume fade-out before playback stops, and a countdown in the
  footer. Sleep at end of track instead stops playback exactly when the
  current song or episode finishes. Menu screens show a clock in the
  corner. An idle screensaver (clock + what's playing, repositioned every
  30 seconds against burn-in) takes over after two minutes without input;
  videos and the visualizer are exempt.
- Check for updates (Settings section): fetches `standalone/update.json`
  from this repo, and if a newer version exists, downloads the new
  `neon_lounge.pkg` from the matching GitHub release, verifies its SHA-256,
  and places it in `/dev_hdd0/packages/` so it can be installed from the
  XMB Package Manager. The app cannot replace its own running program
  file, so updates are delivered as a package instead of an EBOOT swap.
- Donate (sidebar): if Neon Lounge has earned a spot on your XMB, you can
  support development at https://paypal.me/iphoneuser1111 — the Donate
  screen also shows a QR code of that link for scanning with a phone.

Controls: X select / play, O back, START pause, L1/R1 previous/next track,
L2/R2 volume, Left/Right artwork (in the gallery) and volume (in Settings),
Square shuffle on/off (Music views) and save EQ preset (on the EQ preset
row), SELECT playlist sort order (playlist view), Triangle Most Played
(playlist view), switch visualizer (Visualizer view), add to playlist
(YouTube results).

## Building

The build runs in WSL with the PS3 SDK at `/opt/ps3sdk`:

```sh
cd standalone
make           # builds neon_lounge.self
make pkg       # builds neon_lounge.pkg
make host-test # host unit tests (spectrum, library, display guard, system
               # state, thread stacks, clock, update, podcast, queue)
make host-tls-test # local HTTPS integration tests; requires host libcurl,
                   # Python 3 and OpenSSL
```

`update.c` links curl/mbedTLS for the update check; the version compare,
manifest parsing and SHA-256 code are host-testable without the PS3 SDK.

## Assets are not in this repo

Fonts, icons, backgrounds and gallery artwork live in `local/` (git-ignored)
and are not published here. A fresh checkout needs `local/package/` populated
with `USRDIR/art/`, `USRDIR/fonts/` and `local/ICON0.PNG` before `make pkg`
produces a working package. Do not commit artwork you do not have the rights
to publish.

For a fresh asset directory, stage the updater's Mozilla trust bundle with:

```sh
python3 tools/fetch_update_ca.py local/package/USRDIR/certs
```

The helper verifies curl's published SHA-256 and saves the MPL 2.0 license
and source information beside the bundle. It preserves an existing directory.
Packaging fails if the trust bundle is missing.

## Pushing an update

1. Bump `NL_VERSION` in `src/update.h` (for example `1.01` -> `1.02`).
2. Rebuild: `make pkg`. Record the SHA-256 of the new `neon_lounge.pkg`,
   and of the NPDRM `EBOOT.BIN` in `build/pkg/USRDIR/EBOOT.BIN` (the EBOOT
   fields stay in the manifest for older builds).
3. Create a GitHub release tagged `neon-lounge-<version>` (for example
   `neon-lounge-1.02`) and attach `neon_lounge.pkg` and `EBOOT.BIN` as
   release assets.
4. Update `standalone/update.json` with the new version, the release
   download URL for the PKG, and its SHA-256 (keeping the EBOOT fields),
   then push.
5. On the PS3, Settings > Check for updates downloads the new package to
   `/dev_hdd0/packages/`; install it from the XMB Package Manager.

The updater verifies the server certificate and hostname using
`USRDIR/certs/cacert.pem`. Both the manifest and package transfers require
HTTPS, including redirects, and the downloaded package's SHA-256 is checked
against `update.json` before staging. A missing or invalid certificate bundle
fails the update; it never falls back to an unverified connection.

## PC Audio

The Music screen has a PC Audio connect/disconnect button. Start the companion
on a Windows PC on the same LAN, then connect from the PS3. Spotify or YouTube
runs on the PC; this is speaker-output streaming, without native service login,
Spotify Connect certification, or in-app track selection from those services.

```powershell
py -m venv local/pc-audio-venv
local/pc-audio-venv/Scripts/python -m pip install -r tools/pc-audio-requirements.txt
local/pc-audio-venv/Scripts/python tools/pc_audio.py --bind PC_LAN_IP --ps3 PS3_LAN_IP
```

Replace the two placeholders with private IPv4 addresses. Both devices need
local UDP port 45832 discovery and TCP port 45833 connectivity. Discovery sends
a fresh session credential only to the configured console. The audio travels
unencrypted over the trusted LAN; never forward these ports to the internet.
The companion captures the PC's default speaker output, not its microphone,
and saves no audio. Capture starts on connection; Ctrl+C stops the companion.
Notifications and other sounds on that output will be shared too. Keep PS3
capture monitoring off that same output to avoid an audio feedback loop.

Known limitation: the app's discovery trusts the first well-formed responder
on the LAN. A one-time pairing update (a code shown on the PC, entered once
in Neon, after which only the paired PC is accepted) is the planned fix;
until then, use PC Audio on networks you trust.

PC mode uses a separate network thread and a bounded PCM buffer (150 ms startup,
500 ms maximum), with silence on starvation. L2/R2 and Start retain volume and
pause controls; change Spotify/YouTube tracks on the PC. Choose PC Audio again
to disconnect, or choose a local track to return to HDD/USB playback.

Native rendering prefers supported 720p output to avoid software upscaling to
1080p on every frame. It uses RSX vblank pacing, targeting 60 FPS when the display and
render workload permit it. Settings shows measured application FPS. A watchdog
exits the app after ten seconds without a main-loop heartbeat. Ten continuous
seconds below 10 FPS requests a normal exit; isolated slow frames do not.
An update download receives a bounded longer deadline. This cannot guarantee
recovery from a kernel or GPU failure. Hardware frame-rate and recovery checks
are required before release.
