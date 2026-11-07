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
- Display glitch fixed: gCoopTextPages was a COMMON symbol, which this linker script
  puts at 0x03008000 (past the end of IWRAM, which mirrors onto the GPU register
  buffer at 0x03000000). Every text page changed DISPCNT's low byte. Now EWRAM_DATA.
  Keep new globals static or EWRAM_DATA/initialized; never plain `u8 gX;`.
- Story pull prompt (Task_CoopStoryPullPrompt in battle_setup.c): the starter's trainer
  script is paused while it asks; yes starts the pull, no waits for A.
- Bag: items allowed in co-op (BATTLE_IS_REAL_LINK). The player's game uses the item
  as normal (ExecuteTableBasedItemEffect notes the party slot and move); the choice
  goes to the host as item + slot + move (BtlController_EmitCoopItemReturnValue); the
  host uses the guest's item on its copy (Coop_OnItemChosen). At the item's turn,
  BattleScript_CoopUseItem sends HP, status and PP to both games' copies
  (SetMonData with the slot bit) and redraws the healthbox and status icon.
- Catching: ball target = across, else its flank partner; the ball anim carries the
  target and ball (BtlController_EmitCoopBallThrowAnim; LinkPartner now plays it).
  BattleScript_CoopSuccessBallThrow (no dex screen/nickname); givecaughtmon records
  the mon and catcher; CB2_EndCoopBattle sends who caught it (and the mon, if the
  guest did); Coop_RestoreParty gives it to the catcher and sets their dex flags.
- Money: co-op battles pay the single (not double) reward to each player; the host
  sends the amount in the end-of-battle ready block. Trainer wins now use the normal
  won script (the link check there was missing BATTLE_IS_REAL_LINK).
- Evolutions: own Pokemon that went up a level evolve after a won battle
  (CB2_CoopEvolutions, before the usual end of battle).
- Nuzlocke in co-op: the host's game skips its own Nuzlocke handling for catches; each
  game applies its player's rules after the battle (CoopNuzlockeAfterBattle: the
  catcher's first-in-area check, dupe flags and tracker; for wild battles the player's
  own wild mon counts as their encounter). Nuzlocke catchers get the naming screen
  (DoNamingScreen -> CB2_CoopAfterNickname) before the mon is given.
- The "Gotcha!" message names the actual caught mon in co-op (buffer 1).
- Not done: the optional nickname prompt for co-op catches without Nuzlocke.
- Guest follows the host's Key System settings (FollowHostRules in Coop_Update; iv/ev
  calc changes recalc the party).
- Story battles: the starter sends COOP_CMD_STORY_ASK and waits (Task_CoopStoryPullWait);
  the partner runs Coop_EventScript_StoryGetReady (heal / boxes / item storage / ready)
  and answers COOP_CMD_STORY_READY; then the usual STORY_PULL. Fixed: WarpToLeader now
  refreshes sFollowFrame, or a warp right after following started looked finished at
  once and the battle start was lost to the map reload.
- A player whose own Pokemon all fainted in a battle that wasn't lost whites out after
  it (Coop_SetWhiteOutPending -> Coop_EventScript_WhiteOut -> Coop_DoWhiteOut, which
  splits first).
- Shared gifts: ScrCmd_additem/ScrCmd_givemon call Coop_ShareItemGift/MonGift; the gift
  goes through shared vars VAR_0x40D0 + 4 * playerIndex (counter, kind, id, amount)
  and the partner runs Coop_EventScript_PartnerGiftItem/Mon. Item balls and hidden items
  call special Coop_DontShareNextItem first; not shared in the Game Corner prize room,
  on the Celadon roof (vending) or (Pokemon) in Oak's lab. Gift detection is ignored
  for the first 600 frames after connecting (the guest is taking the host's vars).
- Coop_ForceSoloBattle (split + notice) at the start of Safari, ghost, old man,
  scripted wild, legendary, Marowak, roamer and similar battles; the Safari entrance and
  zone maps split a linked pair (like Oak's lab).
- Natural doubles removed: no BATTLE_TYPE_DOUBLE from trainer data; double trainer
  scripts run as single ones; trainers who'd need two Pokemon see the player as usual.
- Legendary flags are per player in the app (world_sync.h); protocol version 5.
- Co-op catches without Nuzlocke: Coop_EventScript_NicknameCaught on the map after.
- Gifts now always go straight into the partner's bag/team (or PC) silently; three gift
  slots per player (VAR_0x40D0 + 10 * index: counter, then 3 x kind/id/amount) so a few
  given at once all arrive.
- Healing: the nurse script calls special Coop_ShareHeal (COOP_CMD_HEAL -> the follower
  heals).
- Trading (Pokemon Center 2F, layout LAYOUT_POKEMON_CENTER_2F): TRADE_ASK/ACCEPT/DECLINE
  commands; both run Coop_EventScript_DoTrade: ChoosePartyMon, then Task_TradeExchange
  over the co-op link (block: cancel/answer + the Pokemon) for the choice and for the
  confirm; then the in-game trade scene with the partner's Pokemon in gEnemyParty[0]
  (trade_scene.c uses the partner's name while Coop_IsTrading()).
