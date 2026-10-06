# Melee Unlocked: Source Port without Slippi, with peer-to-peer play

A native Windows build of Super Smash Bros. Melee (NTSC 1.02) compiled from
C source, with no Slippi code compiled or linked, no Slippi account, server or file needed, and the
project's own peer-to-peer online modes. Nothing from the game is included: you supply your own ISO.

Not affiliated with, endorsed by, or supported by Nintendo, HAL Laboratory or the Slippi team.

## Current version: 0.8.77

This update brings over the shared game, graphics, input and mod fixes through 0.8.77:

- Fixed Classic Mode team intro portraits.
- Added an option to pick a random installed stage skin each match.
- Improved stage-skin compatibility checks and imports.
- Added offline input delay, expanded native Gecko-code support and an in-game code import button.
- Included the Adventure/credits crash fixes, unlock fixes, save backups and controller improvements.

The separate P2P Direct and P2P Unranked modes are included. Slippi netcode is excluded from both
application and game-library builds.

## What is here

- **The game**: the decompilation of Melee built as a native library (`sourceport/`), with the changes
  it needs as one patch (`sourceport/patches/melee-native.patch`, applied by
  `tools/prepare_native_sources.py` to the public decompilation at the pinned commit).
- **The host runtime** (`port/runtime`, `port/app`): renderer (D3D12 / D3D11), audio, input, memory
  card, settings, unlocked display frame rate.
- **Peer-to-peer session** (`port/runtime/mu_net`, wire format in `docs/mu-net-protocol.md`): ENet over
  UDP, identity keys and an authenticated key exchange, every packet sealed, per-frame input commits
  with redundant history, rollback with a 7 frame window, per-frame state checksums, a signed result
  file after each game.
- **Launcher** (`port/app/launcher*.cpp`): a serverless lobby on the BitTorrent DHT with two modes.
  - **P2P Direct**: ask a player in the lobby or paste an invite, both accept, the two games connect
    to each other directly.
  - **P2P Unranked**: press Find match; searching players are paired automatically by ping.

## Validation (0.8.77)

Checked on one PC:

- Windows Release builds passed, along with all 21 automated host and native-game tests.
- The packaged game booted and ran Classic Mode, with both intro portraits rendering correctly.
- A two-game P2P session passed with 40 ms of added lag, 5 ms jitter and 1% packet loss: no checksum
  mismatches, and matching result transcripts for both games.
- P2P Direct and P2P Unranked launcher matches completed, with each lobby reopening on its original port.

How two players behind routers connect: the lobby already has a working UDP path between the two
launchers. When a match is agreed each launcher closes its lobby socket and its game opens the same
port and dials the address the lobby saw for the other player. The lobby returns when the game closes.

Not yet checked or not yet built:

- No match has been played between two different PCs over the internet. The port handoff is tested on
  one PC only, and there is no relay to fall back on.
- The training packs are no longer switched off in this build, but their menus have not been walked here.
- Character and color are chosen in the launcher; the stage is drawn from the legal list by a shared
  seed. There is no in-game character or stage select for online play yet.
- No replay recording in this build.
- Shared source files still contain Slippi code paths that are compiled out (`MELEE_NO_SLIPPI`,
  `MU_NO_SLIPPI`); they are not yet removed from the text. The crash symbol file keeps internal names.

## Build

See `BUILD.md`.
