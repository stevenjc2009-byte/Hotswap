<p align="center">
  <img src="cia/logo-512.png" width="128" alt="">
</p>

<h1 align="center">Hotswap</h1>

<p align="center"><b>Swap your mods. Play.</b></p>

A Nintendo 3DS homebrew app that switches a game between **stock**, a
**community mod** and your **own custom mod**, then launches it. One touch, no
file juggling, no SD card in and out of the PC.

The first release covers **Mario Kart 7** — stock, **CTGP-7**, and whatever you
have built yourself. The swap engine is not tied to one game, so more can
follow.

Works on Old 3DS, New 3DS, 2DS and New 2DS XL.

> **Status: not yet tested on real hardware.** The swap engine is covered by
> 183 automated tests and an 18-check on-device self test in an emulator, but
> no physical console has run it. Treat this as a beta and back up your SD card.

## Requirements

- A 3DS-family console running **Luma3DS** custom firmware
- Mario Kart 7 installed, or the cartridge inserted
- CTGP-7 installed separately, if you want the CTGP-7 button

### Two Luma3DS settings, in two different places

These are separate switches. Turning on one does **not** turn on the other, and
you need both — the first for custom tracks, the second for CTGP-7.

| Setting | Where | Needed for |
|---|---|---|
| **Enable game patching** | Luma config — hold **SELECT** while powering on | Stock and custom mods |
| **Enable plugin loader** | Rosalina menu — press **L + D-Pad Down + SELECT** in a game | **CTGP-7** |

**New 3DS / New 2DS XL owners: you must turn the plugin loader on yourself.** It
is on by default on Old 3DS and 2DS, but off by default on New models. If you
skip it, the CTGP-7 button here will work perfectly, the plugin will be put
exactly where it belongs, and Mario Kart 7 will still boot without CTGP-7 —
because Luma never loaded it. That looks like a broken app but it is this
setting.

## Install

Install `hotswap.cia` with FBI. It appears on the HOME Menu.

## How it works

Luma3DS loads game mods from two folders, and only one set can live in each at
a time:

| What | Where Luma reads it |
|---|---|
| LayeredFS mods (tracks, textures, models) | `/luma/titles/<TITLEID>/` |
| 3GX plugins (this is what CTGP-7 is) | `/luma/plugins/<TITLEID>/` |

This app parks every mod under `/mk7mods/` and moves the one you pick into
place:

```
/mk7mods/ctgp7/plugins/          -> /luma/plugins/<TITLEID>/
/mk7mods/custom/layeredfs/       -> /luma/titles/<TITLEID>/
/mk7mods/<yours>/layeredfs/      -> /luma/titles/<TITLEID>/
```

Nothing is ever copied — folders are **renamed**, which on an SD card is
instant no matter how big the mod is, and cannot be interrupted half-written.

Your Mario Kart 7 title ID is detected automatically. All four regions
(Japan, Europe, Americas, Korea) and both cartridge and digital copies work
from the same build.

## Adding your own mod

Create a folder under `/mk7mods/` and put your LayeredFS files in a
`layeredfs` subfolder, mirroring the game's own structure:

```
/mk7mods/mymod/layeredfs/romfs/Course/...
/mk7mods/mymod/layeredfs/code.ips        (if your mod has one)
```

The app picks up a folder named `custom` first; otherwise it uses the first
mod folder it finds.

## If you already have mods installed

Nothing is moved. On first run the app notices whatever is already in
`/luma/titles/<TITLEID>/`, records it as a mod called **existing**, and leaves
it exactly where it is. It becomes just another option you can swap away from
and back to.

The two folders are read independently, so having **CTGP-7 and your own mod
installed at the same time** is fine — CTGP-7 is recognised as CTGP-7, your mod
becomes **existing**, and both buttons light up.

If it ever finds two copies of the same thing — one parked and one live — it
**stops and changes nothing** rather than guessing which to keep.

## Power loss during a swap

Safe. Every swap writes a journal first, and the swap operation is designed so
that re-running it finishes the job from wherever it stopped. The app recovers
automatically on the next launch, before it draws anything.

## Building

Requires devkitPro with devkitARM, libctru and citro2d.

**Build from the devkitPro MSYS2 shell**, not Git Bash or cmd:

```bash
make cia
```

Run the swap-engine tests on the host with any C compiler:

```bash
gcc -std=c11 -Wall -o test_core test/test_core.c test/plat_host.c source/core/swap.c source/core/mk7titles.c && ./test_core
```

A development build adds emulator test fixtures, an on-device self test and
framebuffer capture. None of it exists in a release build:

```bash
make cia DEV=1
```

## Layout

| Path | What it is |
|---|---|
| `source/core/` | The swap engine. Portable C, no libctru, fully tested on the host. |
| `source/plat_3ds.c` | Hardware filesystem layer (`FSUSER_RenameDirectory`). |
| `source/detect.c` | Runtime title-ID, region and cartridge detection. |
| `source/ui.c` | Bottom-screen touch UI. |
| `test/` | Host test harness, including rename fault injection. |
| `tools/make_art.py` | Regenerates the icon, banner and logo. |

## Credits

- **Luma3DS** — the custom firmware whose LayeredFS and plugin loader make all
  of this possible.
- **CTGP-7** by PabloMK7 and the CTGP-7 team. This app does not bundle,
  modify or redistribute any part of CTGP-7 — it only moves the plugin you
  already installed into and out of the folder Luma reads.
