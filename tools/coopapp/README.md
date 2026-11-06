# Brainy Blue Co-op app

Runs Brainy Blue on a built-in GBA emulator core (mGBA) and connects two players.
The ROM must be built from the `coop` branch.

## Playing

The host runs:

    bbcoop host --rom pokefirered.gba

The partner runs (with the host's IP address):

    bbcoop join 203.0.113.5 --rom pokefirered.gba

The host needs TCP port 27450 open (or pick another with `--port`). Each player
uses their own ROM copy and save file (`--save` picks a different one).

Controls: arrows, Z = A, X = B, Enter = Start, Backspace = Select, A = L, S = R,
Escape = quit. A game controller also works.

If the partner disconnects, the game is saved to `<rom>.coop.ss` and the app closes.

## Building

- Linux, or Windows 11 through WSL: `./build_linux.sh`
- A Windows .exe (cross-built from WSL): `./build_windows.sh`

## Status

Working: window, sound, controls, the connection, and each player seeing the
other walk around the same map. Tested here with two copies of the app talking
over a local connection. Not yet: linked following, trades, co-op battles.
