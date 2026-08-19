# Changelog

All notable changes to Hotswap are recorded here. Format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and versions follow
[Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.2.0] - 2026-08-19

### Fixed

- **CTGP-7 no longer reads "Not installed" on a console that plainly has it.**
  Hotswap was looking for CTGP-7's plugin where Hotswap parks plugins,
  `/luma/plugins/<TITLEID>/`. CTGP-7 does not put it there. It keeps it at its
  own fixed path, `/CTGP-7/resources/CTGP-7.3gx`, and tells the plugin loader
  about it at launch time. That file is now what the check looks at.

- **Switching to CTGP-7 now actually starts CTGP-7.** Selecting it moves no
  files, because there are none to move. Instead, pressing **Launch** points
  Luma3DS's plugin loader (`plg:ldr`) at CTGP-7's plugin for that one launch —
  which is exactly what CTGP-7's own launcher does. Switching back to Stock
  needs no action at all: the instruction is consumed by the launch it was
  armed for, and the next one gets nothing.

  This needs Luma's **plugin loader** switched on as well as game patching. If
  it is off, Hotswap says so rather than booting the game stock and leaving you
  to wonder why the mod did nothing.

### Added

- **An options menu.** **SELECT** opens it from anywhere; **B** puts you back
  exactly where you were.

- **Check for updates**, in that menu. It asks GitHub for the newest release,
  and if that is newer than the copy you are running it downloads the release's
  CIA, installs it, and restarts Hotswap into the new version. Already on the
  newest release? It says so and changes nothing.

- **Every game is scanned for mods when the app opens**, so a mod installed
  since last time shows up without having to go looking for it. Each game's row
  says what is waiting for it — "Europe - SD - CTGP-7 + 2 mods".

- **Each game's own icon art**, drawn at the right-hand end of its row.

### Changed

- **Choosing a mod no longer closes the app.** It swaps, says "Swap complete",
  and leaves you on the list. Playing is now a separate decision, made with the
  **Launch** button under the list.

## [1.1.0] - 2026-08-19

### Fixed

- **Launching a game no longer hangs the console.** In 1.0.0, touching any row
  left the console on a black screen with the HOME button dead, and only a hard
  reset recovered it. Hotswap was firing the title jump itself
  (`APT_PrepareToDoApplicationJump` / `APT_DoApplicationJump`) part-way through
  running, but libctru performs that jump during its own shutdown. The result
  was a jump pending that the library knew nothing about, followed by a normal
  shutdown on top of it. Hotswap now hands the target to libctru
  (`aptSetChainloader`) and leaves through the one ordinary exit path.

  **Your SD card was never at risk from this.** The swap finished before the
  launch was ever attempted, and no mod files are touched by the launch step.

### Changed

- The "Swapped, but could not start the game" message can no longer appear.
  There is nothing left that can fail between the swap finishing and the game
  starting.

## [1.0.0] - 2026-08-19

First public release.

### Added

- **Two-screen flow.** Pick a game, then pick a mod for it. The game list only
  shows games the console actually has — installed on the SD card or sitting in
  the cartridge slot. **B** goes back to the game list, **START** exits.
- **Mario Kart 7 support** across all four regions (Japan, Europe, Americas,
  Korea) and both cartridge and digital copies, from a single build. Title IDs
  are detected at runtime, never hardcoded to one console.
- **Three kinds of mod, in one list.** Stock is always first — it is the way
  back. Then CTGP-7, then every mod folder of your own. The live one is
  marked **ACTIVE**.
- **CTGP-7 handled as what it is** — a Luma3DS 3GX plugin, not a LayeredFS file
  set. Hotswap manages the plugin slot (`/luma/plugins/<TITLEID>/`) and the
  LayeredFS slot (`/luma/titles/<TITLEID>/`) independently, so having CTGP-7
  and your own mod installed at the same time works and each keeps its own row.
  No CTGP-7 title ID is hardcoded and nothing of CTGP-7 is bundled or modified.
- **Adoption of an existing setup.** On first run, whatever is already sitting
  in the Luma folders is recorded as a mod called `existing` and left exactly
  where it is. Nothing is moved and nothing is lost.
- **Crash-safe swapping.** Every swap writes a journal first, and the swap
  itself is idempotent — re-running it finishes the job from wherever it
  stopped. Recovery runs automatically on the next launch, before anything is
  drawn. Pulling the power mid-swap is survivable.
- **Never clobbers.** If both a parked and a live copy of the same slot hold
  content, Hotswap stops and changes nothing rather than guessing which to keep.
- **Rename, never copy.** Mods are moved by FAT32 directory rename, so a 2 GB
  mod swaps as fast as a 2 KB one and cannot be interrupted half-written.
- **Scrolling mod list** with **Up / Down** for consoles with more mods than
  fit on screen.
- Automatic migration from the earlier `/mk7mods/` layout to `/hotswap/mk7/`,
  for anyone who ran a build from source before this release. Three renames,
  performed before anything else reads the card, and a no-op on every boot
  after the first. An interrupted swap survives the migration.

### Notes

- The swap engine is portable C with no libctru dependency and is covered by
  **237 host assertions**, including fault injection that interrupts a real
  swap at every individual rename.
- An additional **18 on-device assertions** exercise the real
  `FSUSER_RenameDirectory` path in an emulator.
- **Not yet tested on physical hardware.** Treat this release as a beta and
  back up your SD card before using it.

[1.2.0]: https://github.com/stevenjc2009-byte/Hotswap/releases/tag/v1.2.0
[1.1.0]: https://github.com/stevenjc2009-byte/Hotswap/releases/tag/v1.1.0
[1.0.0]: https://github.com/stevenjc2009-byte/Hotswap/releases/tag/v1.0.0
