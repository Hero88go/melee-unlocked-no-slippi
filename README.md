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
  signed result files on both sides, at five settings from no lag to 90 ms of added lag with 12 ms of
  jitter and 3% packet loss. Test: `tools/p2p_pair.py`, fault injection `tools/net_fault_proxy.py`.
- Both launcher modes run end to end with no human: two launchers started off screen find each other,
  agree a match (Find match, and request then accept), start their games, the games play a match with
  0 checksum mismatches and identical result files, and both lobbies come back afterwards.
  Test: `tools/p2p_launcher_pair.py` (add `--mode direct` for request and accept).
- A session plays game after game on one connection until a player quits: three games in a row, and
  two games through 40 ms of lag with loss, each with 0 checksum mismatches and identical result files
  (`tools/p2p_pair.py --games 3`). Chosen characters and colors reach both games.
- Session unit tests (handshake, tampered and replayed packets, lossy input exchange, stall rule,
  desync flag) and lobby tests (match setup agreement, automatic pairing of two and three players,
  blocklist, old protocol refusal, invites) pass.

How two players behind routers connect: the lobby already has a working UDP path between the two
launchers. When a match is agreed each launcher closes its lobby socket and its game opens the same
port and dials the address the lobby saw for the other player, so the game uses the path the lobby
opened. The lobby returns when the game closes.

Not yet checked or not yet built:

- No match has been played between two different PCs over the internet. The port handoff above is
  tested on one PC only. It is expected to fail on routers that give a new public port to the
  reopened socket or drop the mapping within a few seconds, and there is no relay to fall back on.
- Character and color are chosen in the launcher (Profile tab); the stage is drawn from the legal list
  by a shared seed. There is no in-game character or stage select for online play yet.
- No replay recording in this build.
- Shared source files still contain Slippi code paths that are compiled out (`MELEE_NO_SLIPPI`,
  `MU_NO_SLIPPI`); they are not yet removed from the text.

## Build

See `BUILD.md`.
