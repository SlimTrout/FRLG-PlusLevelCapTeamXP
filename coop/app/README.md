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

## Linked play

- **Link up:** walk up to your partner and press A, then choose to link. Your partner
  is asked to accept; whoever asked leads.
- **Following:** the follower has no character of its own. Its screen follows the
  leader everywhere (doors, stairs, ledges, map edges, cutscenes), and it sees the
  leader's dialogue: the text box turns its pages and closes when the leader's does.
- **Leader:** press R to let your partner lead, or to split off.
- **Follower:** press A or R to ask to lead, or to split off. Start still opens the menu.
- While following you don't trigger wild Pokémon or trainers; the leader's steps do.

## Status

Working: window, sound, controls, the connection, seeing each other on the map,
linking up, following, handing over the lead and splitting off.
Not yet: co-op double battles, trades, story-battle rules.
