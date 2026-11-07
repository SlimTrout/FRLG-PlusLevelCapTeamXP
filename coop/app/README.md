# Brainy Blue Co-op app

Runs Brainy Blue on a built-in GBA emulator core (mGBA) and connects two players.
The ROM must be built from the `coop` branch.

## Playing on Windows (ZeroTier)

1. Both players install [ZeroTier](https://www.zerotier.com/download/) and join the
   same ZeroTier network.
2. Put `bbcoop.exe`, `SDL2.dll` and the ROM (`pokefirered.gba`) in one folder.
3. Double-click `bbcoop.exe`. The launcher opens:
   - **Host:** pick the game speed (3x by default) and send your partner the ZeroTier
     IP shown in the launcher (the Copy button copies it). Click **Host game**.
   - **Partner:** type in the host's ZeroTier IP and click **Join game**. The game
     runs at the speed the host picked.
   It doesn't matter who starts first: the partner keeps trying until the host is there
   (the window title says what's going on).
4. The first time the host starts, Windows asks whether to let the app through the
   firewall: allow it, on **both private and public networks** (Windows often counts
   the ZeroTier network as public). If the partner can't connect, that is usually why.

**Controls…** in the launcher changes the keyboard keys (also remembered).

### The EXP. BOTTLE

Both players carry an EXP. BOTTLE (Key Items). It fills up with the exp your partner
earns beating trainers on their own (while you're split up), and with the exp your own
Pokemon miss because of the level cap. Use it on a Pokemon to raise it one level for
exactly the exp that takes; at the level cap it does nothing (and keeps its exp). It's
never used up.

### Save states

As a fail safe on top of the game's own save, both games make a save state at the same
moment every 5 minutes (in `savestates/auto` next to the ROM; the newest 10 are kept).
Either player can make one for both by pressing **F5** (in `savestates/manual`, kept
until you delete them). A save state is only made while both players are walking
around (not in a battle, menu or conversation); if one isn't, it's made as soon as
they are. The window title says when one was made.

To go back to one, both players pick the same save state under **Start from** in the
launcher. The game starts once both have picked the same one (the app says so if they
don't match). Loading a save state also puts the game's own save back to that moment.

The launcher remembers your choices in `bbcoop.ini` next to the exe. Each player
uses their own ROM copy and save file (next to the ROM, with the same name and `.sav`).

## Playing from the command line (Linux, or Windows with options)

The host runs:

    bbcoop host --rom pokefirered.gba --speed 3

The partner runs (with the host's IP address):

    bbcoop join 10.147.17.5 --rom pokefirered.gba

The host needs TCP port 27450 open (or pick another with `--port`; both players use the
same one). `--save` picks a different save file.

Controls: arrows, Z = A, X = B, Enter = Start, Backspace = Select, A = L, S = R, F5 = save state,
close the window to quit. A game controller also works.

The game runs at 3x speed by default (the window title shows the speed). The host picks
it when the app starts (`--speed N`, 1 = normal, up to 10) and the partner's game uses the
host's speed; it can't be changed in the game.

If the partner disconnects, the app closes. Only the host saves, so progress since the host last saved is not kept.
Both players need the same version of the app (it says so if they don't).

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
- A Windows .exe (cross-built from WSL): `./build_windows.sh` makes `windows/bbcoop.exe`
  and `windows/SDL2.dll`. Give both, with the ROM, to each player.

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
