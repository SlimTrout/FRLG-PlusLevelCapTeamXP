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
Tab = switch between normal and fast speed, Escape = quit. A game controller also works.

The game runs at 3x speed by default (the window title shows the speed). Tab switches
to normal speed and back; `--speed N` sets the fast speed (1 for normal by default).

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
  Pokemon gets a second one (a different Pokemon of the same type and evolution
  stage, at the same level) so each player has someone to face. Winning marks the
  trainer as beaten for both players.
- **Story battles pull the partner in.** If one player starts a story battle (the
  rival, a gym leader, the Elite Four, the Champion, a boss) while the two are split
  up, the partner sees "... is starting a big battle! You're joining in.", is taken
  to the other player, follows them and joins the battle. Losing the first rival
  battle doesn't white out, as in the normal game.
- **Getting ready for a story battle.** Before a split partner is pulled in, they're
  told the battle is coming and get a menu: HEAL (not with the No Pokemon Center
  rule), POKéMON BOXES, ITEM STORAGE and READY. The battle starts once they pick READY;
  the player who started it waits ("... is getting ready to join this battle").
- **Losing your Pokemon.** If all of one player's Pokemon faint, they watch the rest of
  the battle. If their partner doesn't lose, they white out normally once back on the
  map (splitting up from their partner).
- **The bag works in linked battles.** Each player uses their own bag. Potions and
  other medicine work as usual, and the partner's screen shows the healing.
- **Catching.** Each player's ball goes at the wild Pokemon across from them (or the
  other one if that one's gone). The Pokemon goes to whoever threw the ball, into
  their team or their PC, and into their Pokedex, and they're offered a nickname
  back on the map. With Nuzlocke on, the catcher's own
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

- **Pokemon Center healing.** When the leader of a linked pair heals at a Pokemon
  Center, the follower's Pokemon are healed too.
- **Trading.** Upstairs in any Pokemon Center, talk to your partner (split up, not
  linked) to ask for a trade. If they say yes, each of you picks a Pokemon, sees what
  you'd get, and confirms; then the trade plays out as in the original game (trade
  evolutions too). B stops waiting at any point.

- **Gifts for both.** When one player is given something by the game (an NPC's gift,
  a key item, a TM, a gift Pokemon like Eevee or Lapras), the same goes straight
  into the other's bag or team (or PC, if full), without a message. Item balls and hidden items stay each player's own, and the
  Game Corner prizes and rooftop vending machines are bought by each player. Starters
  are still picked by each player.
- **Legendary Pokemon** (the birds, Mewtwo, Mew, Deoxys, Lugia, Ho-Oh, Latias and
  Latios) stay in place for the other player after one player catches or defeats them.
- **One-player battles.** Legendary and other set-piece Pokemon (Snorlax and so on),
  ghosts in Pokemon Tower, the old man's catching demonstration and roaming Pokemon are
  fought alone: a linked pair splits up for them. The Safari Zone too: a linked pair
  splits up at its entrance, and each player pays and goes in on their own.
- **No natural double battles.** Trainers who'd fight as a pair (Twins and so on)
  fight as ordinary single battles (or as a co-op battle when the players are linked).

- **Rules.** The guest always plays by the host's Key System settings (difficulty,
  Nuzlocke, exp, level caps, team exp and the rest) while connected.


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
