<p align="center">
  <img src="cia/logo-512.png" width="128" alt="">
</p>

<h1 align="center">Hotswap</h1>

<p align="center"><b>Swap your mods. Play.</b></p>

A Nintendo 3DS homebrew app that switches a game between **stock**, a
**community mod** and your **own custom mods**, then launches it. One touch, no
file juggling, no SD card in and out of the PC.

Pick a game, pick a mod. The game list only shows games you actually have —
installed on the SD card or sitting in the cartridge slot.

The first release knows **Mario Kart 7** — stock, **CTGP-7**, and whatever you
have built yourself. Nothing in the swap engine is tied to one game, so more
can be added.

Works on Old 3DS, New 3DS, 2DS and New 2DS XL.

> **Status: beta. Back up your SD card.** The swap engine is covered by 237
> automated tests and an 18-check on-device self test in an emulator.
>
> Version 1.0.0 hung the console when it launched a game, and has been
> withdrawn — if you installed it, replace it with this build. That fault is
> fixed here, but the fix itself has not been confirmed on a physical console
> yet, because the bug is in a code path an emulator cannot reproduce.

## Requirements

- A 3DS-family console running **Luma3DS** custom firmware
- A supported game installed, or its cartridge inserted
- CTGP-7 installed separately, if you want the CTGP-7 row

### Two Luma3DS settings, in two different places

These are separate switches. Turning on one does **not** turn on the other, and
you need both — the first for custom tracks, the second for CTGP-7.

| Setting | Where | Needed for |
|---|---|---|
| **Enable game patching** | Luma config — hold **SELECT** while powering on | Stock and custom mods |
| **Enable plugin loader** | Rosalina menu — press **L + D-Pad Down + SELECT** in a game | **CTGP-7** |

**New 3DS / New 2DS XL owners: you must turn the plugin loader on yourself.** It
is on by default on Old 3DS and 2DS, but off by default on New models. If you
skip it, the CTGP-7 row here will work perfectly, the plugin will be put
exactly where it belongs, and Mario Kart 7 will still boot without CTGP-7 —
because Luma never loaded it. That looks like a broken app but it is this
setting.

## Install

**[Download `hotswap1.1.0.cia`](https://github.com/stevenjc2009-byte/Hotswap/releases/download/v1.1.0/hotswap1.1.0.cia)** — or scan this with FBI.

<p align="center">
  <img src="docs/install-qr.png" width="220" alt="QR code linking to hotswap1.1.0.cia">
</p>

**With the QR code:** open FBI on your 3DS, choose **Remote Install → Scan QR
Code**, and point the camera at the image above.

**By hand:** copy `hotswap1.1.0.cia` to your SD card, open FBI, browse to it and
install.

Either way it appears on the HOME Menu when it is done. See
[CHANGELOG.md](CHANGELOG.md) for what is in this version.

## Using it

Two screens, both driven by the touch screen:

1. **Choose a game.** Only games this console actually has are listed. If a
   game is both installed and in the cartridge slot, the cartridge is the one
   that gets launched.
2. **Choose a mod.** Stock is always first — it is the way back. Then the
   community mod for that game, then every mod folder of your own. The live
   one is marked **ACTIVE**.

Touch a mod and the swap happens, then the game starts. **B** goes back to the
game list, **START** exits. If there are more mods than fit, **Up / Down**
scroll the list.

## How it works

Luma3DS loads game mods from two folders, and only one set can live in each at
a time:

| What | Where Luma reads it |
|---|---|
| LayeredFS mods (tracks, textures, models) | `/luma/titles/<TITLEID>/` |
| 3GX plugins (this is what CTGP-7 is) | `/luma/plugins/<TITLEID>/` |

This app parks every mod under `/hotswap/<game>/` — one subtree per game, so
games never see each other's files — and moves the one you pick into place:

```
/hotswap/mk7/ctgp7/plugins/       -> /luma/plugins/<TITLEID>/
/hotswap/mk7/custom/layeredfs/    -> /luma/titles/<TITLEID>/
/hotswap/mk7/<yours>/layeredfs/   -> /luma/titles/<TITLEID>/
```

Nothing is ever copied — folders are **renamed**, which on an SD card is
instant no matter how big the mod is, and cannot be interrupted half-written.

The title ID is detected automatically. All four Mario Kart 7 regions
(Japan, Europe, Americas, Korea) and both cartridge and digital copies work
from the same build.

### Upgrading from an earlier version

Version 1 kept everything in `/mk7mods/`. On first run the app moves that
folder to `/hotswap/mk7/` and renames `mk7swap.state` and `mk7swap.journal`
to `state` and `journal`. It is three renames, it happens before anything else
reads the card, and it is a no-op on every boot after the first. If a swap was
interrupted before the upgrade, the journal moves with the rest and recovery
still finishes the job.

## Adding your own mod

Create a folder under your game's folder — `/hotswap/mk7/` for Mario Kart 7 —
and put your LayeredFS files in a `layeredfs` subfolder, mirroring the game's
own structure:

```
/hotswap/mk7/mymod/layeredfs/romfs/Course/...
/hotswap/mk7/mymod/layeredfs/code.ips        (if your mod has one)
```

The folder name is what shows up in the list, so name it something you will
recognise. A 3GX plugin of your own goes in a `plugins` subfolder instead, and
a mod can have both.

## If you already have mods installed

Nothing is moved. On first run the app notices whatever is already in
`/luma/titles/<TITLEID>/`, records it as a mod called **existing**, and leaves
it exactly where it is. It becomes just another option you can swap away from
and back to.

The two folders are read independently, so having **CTGP-7 and your own mod
installed at the same time** is fine — CTGP-7 is recognised as CTGP-7, your mod
becomes **existing**, and both get their own row.

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
gcc -std=c11 -Wall -o test_core test/test_core.c test/plat_host.c source/core/swap.c source/core/games.c && ./test_core
```

A development build adds emulator test fixtures, an on-device self test and
framebuffer capture. None of it exists in a release build:

```bash
make cia DEV=1
```

## Layout

| Path | What it is |
|---|---|
| `source/core/swap.c` | The swap engine. Portable C, no libctru, fully tested on the host. |
| `source/core/games.c` | The registry of supported games and their title IDs. |
| `source/plat_3ds.c` | Hardware filesystem layer (`FSUSER_RenameDirectory`). |
| `source/detect.c` | Finds which supported games this console has. |
| `source/ui.c` | Bottom-screen touch UI — one scrolling list, both screens. |
| `test/` | Host test harness, including rename fault injection. |
| `tools/make_art.py` | Regenerates the icon, banner and logo. |

## Credits

- **Luma3DS** — the custom firmware whose LayeredFS and plugin loader make all
  of this possible.
- **CTGP-7** by PabloMK7 and the CTGP-7 team. This app does not bundle,
  modify or redistribute any part of CTGP-7 — it only moves the plugin you
  already installed into and out of the folder Luma reads.
