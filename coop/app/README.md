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

If the partner disconnects, the app closes. Only the host saves, so progress since the host last saved is not kept.

## Battles (first stage)

- **Linked wild battles are double battles.** When the leader runs into a wild
  Pokemon, both players go into the same battle: the host's lead Pokemon on the left,
  the guest's on the right, against two wild Pokemon (one from each player's own
  encounter table). Each player picks their own moves. Running works or fails for both.
- **Exp** goes to each player's Pokemon by that player's own exp, team exp and level
  cap settings.
- When one of your Pokemon faints you pick your next one from your own team.
- **Linked trainer battles.** When the leader is spotted by (or talks to) a trainer,
  both players fight that trainer together as a double battle. A trainer with only one
  Pokemon gets a second copy so each player has someone to face. Winning marks the
  trainer as beaten for both players.
- **Story battles pull the partner in.** If one player starts a story battle (the
  rival, a gym leader, the Elite Four, the Champion, a boss) while the two are split
  up, the partner sees "... is starting a big battle! You're joining in.", is taken
  to the other player, follows them and joins the battle. Losing the first rival
  battle doesn't white out, as in the normal game.
- **Story battles ask first.** The player who starts the story battle is told their
  partner will be pulled in and chooses when: "Bring ... in now?" Saying no shows
  "Press A when you're ready", and A asks again.
- **The bag works in linked battles.** Each player uses their own bag. Potions and
  other medicine work as usual, and the partner's screen shows the healing.
- **Catching.** Each player's ball goes at the wild Pokemon across from them (or the
  other one if that one's gone). The Pokemon goes to whoever threw the ball, into
  their team or their PC, and into their Pokedex. With Nuzlocke on, the catcher's own
  rules apply: the catch has to be their first in the area (or it faints), it's added
  to their tracker, and they name it right after the battle. A linked wild battle also
  counts as each player's encounter for the area (their own wild Pokemon).
- **Prize money.** Both players get the prize money for beating a trainer.
- **Evolutions.** After a linked battle that was won, each game shows its own
  player's evolutions.

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


## Shared world and saving

- **One story for both players.** Badges, beaten trainers, items picked up and story
  events are shared. When the guest joins, their game takes the host's progress, and
  from then on anything either player does shows up in the other's game (an item ball
  your partner picked up disappears from your map too).
- **Your own stuff stays yours.** Team, bag, money, PC, Pokédex and Nuzlocke
  encounters are kept per player.
- **Only the host saves.** The guest's Start menu has no Save. When the host saves,
  the guest's game saves at the same moment.
- **Trainers.** While one player is talking to or fighting a trainer, that trainer
  ignores the other player. Once it's beaten, it's beaten for both.
- **Items on the ground are your own.** Item balls and hidden items aren't shared:
  each player picks up their own (Oak's starter balls and the fossils included). A
  follower can't pick things up, so split off to grab them.
- **If one player disconnects,** the game closes for both. Anything since the host
  last saved isn't kept.

## Status

Working: window, sound, controls, the connection, seeing each other on the map,
linking up, following, handing over the lead and splitting off.
Also working: linked wild and trainer battles, story battles pulling the partner in.
Not yet: trades.
