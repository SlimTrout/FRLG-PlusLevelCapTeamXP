# Co-op battles: working notes

Approach (agreed with the user): reuse FireRed's own link battle model. The host's game
is the battle master and runs the battle engine; every controller command it emits is
sent as a link block to both games (the master receives its own blocks too); each
game's controllers show it, and the guest's player controller sends its choices back.
Opponents are the computer, run by the master's opponent (AI) controllers. Exp, items
and catching are switched back on for co-op battles. Linked battles give exp (user
confirmed), using each player's own settings.

## Layout (same battler numbers in both games)

| battler | position      | host's game        | guest's game        |
|---------|---------------|--------------------|---------------------|
| 0       | PLAYER_LEFT   | Player (host)      | LinkPartner         |
| 1       | OPPONENT_LEFT | Opponent (AI)      | LinkOpponent        |
| 2       | PLAYER_RIGHT  | LinkPartner        | Player (guest)      |
| 3       | OPPONENT_RIGHT| Opponent (AI)      | LinkOpponent        |

Battle party in both games: slots 0-2 = host's first 3 Pokemon, 3-5 = guest's first 3
(battler 0 starts with slot 0, battler 2 with slot 3). The real parties are backed up
before the battle and each player's own half is written back afterwards.

## Link transport ("co-op link", gWirelessCommType stays 0)

link.c: SendBlock / IsLinkTaskFinished / GetMultiplayerId get a co-op branch.
- SendBlock: copy into gBlockSendBuffer, bump mailbox send seq, mark pending; also
  deliver to self (gBlockRecvBuffer[myId], gBlockReceivedStatus[myId] = TRUE).
- App: on a new send seq, read gBlockSendBuffer, send MSG_BLOCK, clear pending.
  On receipt, write gBlockRecvBuffer[senderId] and set gBlockReceivedStatus[senderId]
  when that slot is free (queue otherwise).
- gLinkStatus: player count 2, master bit for the host. gReceivedRemoteLinkPlayers = 1.
  gLinkPlayers[0..1]: linkType 0x2211 (TryReceiveLinkBattleData checks it).
- GetBlockReceivedStatus / ResetBlockReceivedFlag(s) use the cable branch as is.

## Milestones
1. Linked wild double: both leads vs two wild Pokemon (one per player's table), each
   player picks own moves, exp per player. (in progress)
2. Bag, catching, running.
3. Linked trainer battles (single-Pokemon trainers get a second).
4. Story battles pull the partner in; whiting out.

## Things to patch (found so far)
- CB2_HandleStartBattle: replace link exchange with a co-op start (parties, enemy party,
  RNG seed) - own CB2.
- InitLinkBtlControllers: co-op layout above.
- Cmd_getexp: link battles skip exp.
- Party menu / HasNoMonsToSwitch: a player may only switch within their own 3.
- Battle end for link battles: goes through link-specific quit; needs co-op path that
  writes each player's half back and returns to the field.

## Progress log
- Co-op link transport works (link.c branches, coop/app/link_relay.h; HandleLinkConnection
  must be skipped while the co-op link is open or it zeroes gLinkStatus).
- Linked wild battle starts on both games (leader's encounter -> COOP_CMD_WILD_BATTLE,
  follower rolls its own wild mon), shows "Wild X and Y appeared", both players'
  Pokemon go out, host and guest each pick a move, and the turn plays out the same on
  both screens. Tested in coophost (scratchpad sp/b5.txt).
- Next: only one player-side healthbox is visible; battle end (win/run/faint) and
  returning to the field; exp; party write-back; switching limited to own half.
- Done: battle end (win/run) returns both games to the field via the end-link command;
  host sends the guest's three Pokemon back after the battle; exp per owner's settings
  (Cmd_getexp wrapper swaps keyFlags; LinkPartner applies exp on the host's copy);
  flags/vars pointers cleared when save blocks move (battle start) so world sync pauses.
- Still to do in milestone 1: second player-side healthbox not visible; move learning
  with 4 moves for the guest's Pokemon prompts on the host; evolutions after battle.
- Fixed: missing healthbox (the link VS-letter intro hijacked sprite 0; co-op now uses the
  normal intro and background); running in doubles (speed vs the faster wild Pokemon);
  both players choose at the same time (multi-battle style selection).
- Guest's Pokemon with 4 moves don't prompt the host; after the battle the guest's game
  opens the move reminder for each such Pokemon (Coop_EventScript_LearnMoves).
- Next: story battles auto-link (needs linked trainer battles first), bag/catching.
- Linked trainer battles work (leader runs into a trainer -> COOP_CMD_TRAINER_BATTLE;
  host makes the trainer's party and sends it; a one-Pokemon party gets a copy; the
  starter's game continues the trainer's script afterwards; trainer flag syncs).
  Tested with Bug Catcher Rick (coophost scratch tr5).
- Story pull works: a split player starting a story trainer (IsStoryTrainer) sends
  COOP_CMD_STORY_PULL(_NO_WHITEOUT); the partner shows a notice, warps to them, follows
  and joins. Tested with the early Route 22 rival (tr10). Fixes found on the way: the
  trainer claim was cleared during the battle transition (now waits for
  BattleSetup_IsStarting), and the joining player's buttons are blocked while joining.
- Next: bag/catching, guest money, evolutions after battle.
