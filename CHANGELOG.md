# Changelog

All notable changes to Hotswap are recorded here. Format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and versions follow
[Semantic Versioning](https://semver.org/spec/v2.0.0.html).

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

[1.0.0]: https://github.com/stevenjc2009-byte/Hotswap/releases/tag/v1.0.0
