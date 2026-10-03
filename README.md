# Melee Unlocked: Source Port without Slippi, with peer-to-peer play

Private working repository. A native Windows build of Super Smash Bros. Melee (NTSC 1.02) compiled from
C source, with no Slippi code compiled or linked, no Slippi account, server or file needed, and the
project's own peer-to-peer online modes. Nothing from the game is included: you supply your own ISO.

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

## State (2026-10-03)

Checked by automated hidden runs on one PC:

- The build boots and plays as the retail game (title, menus, character select, saves).
- Two instances play a full rollback match from the command line: 0 checksum mismatches and identical
  signed result files on both sides, also through 60 ms of added lag with jitter and 2% packet loss
  (about 100 rollbacks per side). Test: `tools/p2p_pair.py`, fault injection `tools/net_fault_proxy.py`.
- Session unit tests (handshake, tampered and replayed packets, lossy input exchange, stall rule,
  desync flag) and lobby tests (match setup agreement, automatic pairing of two and three players,
  blocklist, old protocol refusal, invites) pass.

Not yet checked or not yet built:

- No match has been played between two different PCs over the internet. There is no relay and no
  hole punching for the game port yet: today a match connects on a LAN, with a public address, or with
  a forwarded port. This is the next piece of work.
- The launcher's two modes are tested at the protocol level only; the full click-through (lobby to a
  running match) has not been run end to end.
- Characters are each player's first main; stage is drawn from the legal list by a shared seed. There
  is no in-game character or stage select for online play yet, and one game per session.
- No replay recording in this build.
- Shared source files still contain Slippi code paths that are compiled out (`MELEE_NO_SLIPPI`,
  `MU_NO_SLIPPI`); they are not yet removed from the text.

## Build

See `BUILD.md`.
