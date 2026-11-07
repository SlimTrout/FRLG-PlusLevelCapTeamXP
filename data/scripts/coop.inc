@ Brainy Blue Co-op: talking to your partner, link requests and notices.
@ Link modes (Coop_GetLinkMode): 0 = split, 1 = leading, 2 = following.

Coop_EventScript_TalkToPartner::
	lockall
	special Coop_BufferPartnerName
	specialvar VAR_RESULT, Coop_GetLinkMode
	goto_if_eq VAR_RESULT, 1, Coop_EventScript_LeaderMenu
	specialvar VAR_RESULT, Coop_IsTradeRoom
	goto_if_eq VAR_RESULT, TRUE, Coop_EventScript_OfferTrade
Coop_EventScript_AskLinkUp::
	msgbox Coop_Text_AskLinkUp, MSGBOX_YESNO
	goto_if_eq VAR_RESULT, NO, Coop_EventScript_ReleaseAll
	special Coop_SendLinkRequest
	msgbox Coop_Text_AskedPartnerToLink
	releaseall
	end

Coop_EventScript_LeaderMenu::
	msgbox Coop_Text_GiveControl, MSGBOX_YESNO
	goto_if_eq VAR_RESULT, YES, Coop_EventScript_GiveControl
	msgbox Coop_Text_SplitOff, MSGBOX_YESNO
	goto_if_eq VAR_RESULT, YES, Coop_EventScript_Split
	releaseall
	end

Coop_EventScript_GiveControl::
	special Coop_GiveControl
	msgbox Coop_Text_GaveControl
	releaseall
	end

Coop_EventScript_Split::
	special Coop_SplitOff
	msgbox Coop_Text_YouSplitOff
	releaseall
	end

@ The follower presses A (it can't move or talk while following).
Coop_EventScript_FollowerMenu::
	lockall
	special Coop_BufferPartnerName
	message Coop_Text_FollowerWhatToDo
	waitmessage
	multichoice 18, 6, MULTICHOICE_COOP_FOLLOWER, FALSE
	switch VAR_RESULT
	case 0, Coop_EventScript_Split
	case 1, Coop_EventScript_AskControl
	closemessage
	releaseall
	end

Coop_Text_FollowerWhatToDo::
	.string "You're following {STR_VAR_1}.\n"
	.string "What would you like to do?$"

Coop_Text_MenuSplitUp::
	.string "SPLIT UP$"

Coop_Text_MenuAskToLead::
	.string "ASK TO LEAD$"

Coop_Text_MenuCancel::
	.string "CANCEL$"

Coop_EventScript_AskControl::
	special Coop_AskForControl
	msgbox Coop_Text_AskedForControl
	releaseall
	end

Coop_EventScript_LinkRequest::
	lockall
	special Coop_BufferPartnerName
	msgbox Coop_Text_PartnerWantsLink, MSGBOX_YESNO
	copyvar VAR_0x8004, VAR_RESULT
	special Coop_AnswerLinkRequest
	releaseall
	end

Coop_EventScript_ControlRequest::
	lockall
	special Coop_BufferPartnerName
	msgbox Coop_Text_PartnerWantsControl, MSGBOX_YESNO
	copyvar VAR_0x8004, VAR_RESULT
	special Coop_AnswerControlRequest
	goto_if_eq VAR_0x8004, NO, Coop_EventScript_ReleaseAll
	msgbox Coop_Text_GaveControl
	releaseall
	end

Coop_EventScript_SaveWithHost::
	lockall
	special Coop_BufferPartnerName
	special Coop_SaveWithHost
	waitstate
	msgbox Coop_Text_SavedWithHost
	releaseall
	end

@ After a co-op battle: one of the guest's Pokemon leveled up past new moves while it
@ already knew four (VAR_0x8004 = its party slot). The move reminder lets the player
@ pick which one to learn.
Coop_EventScript_LearnMoves::
	lockall
	bufferpartymonnick STR_VAR_1, VAR_0x8004
	msgbox Coop_Text_TryingToLearnMoves
	special DisplayMoveTutorMenu
	waitstate
	releaseall
	end

Coop_EventScript_Notice::
	lockall
	msgbox gStringVar4
	releaseall
	end

Coop_EventScript_ReleaseAll::
	releaseall
	end

Coop_Text_AskLinkUp::
	.string "Link up with {STR_VAR_1}?\n"
	.string "You'll lead the way.$"

Coop_Text_AskedPartnerToLink::
	.string "You asked {STR_VAR_1} to link up.$"

Coop_Text_PartnerWantsLink::
	.string "{STR_VAR_1} wants to link up\n"
	.string "and lead the way. OK?$"

Coop_Text_NowFollowing::
	.string "You're following {STR_VAR_1}.$"

Coop_Text_GiveControl::
	.string "Let {STR_VAR_1} lead the way?$"

Coop_Text_GaveControl::
	.string "{STR_VAR_1} is leading now.$"

Coop_Text_SplitOff::
	.string "Split off from {STR_VAR_1}?$"

Coop_Text_YouSplitOff::
	.string "You split off from {STR_VAR_1}.$"

Coop_Text_AskForControl::
	.string "Ask {STR_VAR_1} to let you lead?$"

Coop_Text_AskedForControl::
	.string "You asked {STR_VAR_1} to let\n"
	.string "you lead.$"

Coop_Text_PartnerWantsControl::
	.string "{STR_VAR_1} wants to lead\n"
	.string "the way. OK?$"

Coop_Text_PartnerLinkedYouLead::
	.string "{STR_VAR_1} linked up!\n"
	.string "You're leading the way.$"

Coop_Text_PartnerDeclinedLink::
	.string "{STR_VAR_1} didn't link up.$"

Coop_Text_YouHaveControl::
	.string "{STR_VAR_1} let you lead.\n"
	.string "You're leading the way.$"

Coop_Text_PartnerKeptControl::
	.string "{STR_VAR_1} kept the lead.$"

Coop_Text_PartnerSplitOff::
	.string "{STR_VAR_1} split off.$"

Coop_Text_SavedWithHost::
	.string "{STR_VAR_1} saved the game.\n"
	.string "Your game was saved too.$"

Coop_Text_SplitForStarters::
	.string "You split up from {STR_VAR_1}\n"
	.string "to pick your own POKéMON.$"

Coop_Text_TryingToLearnMoves::
	.string "{STR_VAR_1} is trying to learn\n"
	.string "new moves!$"

Coop_Text_PulledIntoStoryBattle::
	.string "{STR_VAR_1} is starting a big battle!\n"
	.string "You're joining in.$"

@ The partner is about to start a story battle: heal and use the PC, then say ready.
Coop_EventScript_StoryGetReady::
	lockall
	special Coop_BufferPartnerName
	msgbox Coop_Text_StoryGetReady
Coop_EventScript_StoryReadyMenu::
	message Coop_Text_StoryWhatToDo
	waitmessage
	multichoice 17, 4, MULTICHOICE_COOP_STORY_READY, FALSE
	switch VAR_RESULT
	case 0, Coop_EventScript_StoryHeal
	case 1, Coop_EventScript_StoryPokemonStorage
	case 2, Coop_EventScript_StoryItemStorage
	case 3, Coop_EventScript_StoryReady
	goto Coop_EventScript_StoryReadyMenu
	end

Coop_EventScript_StoryHeal::
	special Coop_CanHealForStory
	goto_if_eq VAR_RESULT, FALSE, Coop_EventScript_StoryCantHeal
	special HealPlayerParty
	playse SE_USE_ITEM
	msgbox Coop_Text_StoryHealed
	goto Coop_EventScript_StoryReadyMenu
	end

Coop_EventScript_StoryCantHeal::
	msgbox Coop_Text_StoryCantHeal
	goto Coop_EventScript_StoryReadyMenu
	end

Coop_EventScript_StoryPokemonStorage::
	playse SE_PC_LOGIN
	msgbox Text_OpenedPkmnStorage
	special ShowPokemonStorageSystemPC
	waitstate
	goto Coop_EventScript_StoryReadyMenu
	end

Coop_EventScript_StoryItemStorage::
	playse SE_PC_LOGIN
	msgbox Text_AccessedPlayersPC
	special PlayerPC
	waitstate
	goto Coop_EventScript_StoryReadyMenu
	end

Coop_EventScript_StoryReady::
	special Coop_BufferPartnerName
	msgbox Coop_Text_StoryOnTheWay
	special Coop_StorySendReady
	releaseall
	end

@ This player's Pokemon all fainted in a co-op battle the partner kept fighting.
Coop_EventScript_WhiteOut::
	lockall
	msgbox Coop_Text_WhitedOut
	fadescreen FADE_TO_BLACK
	releaseall
	special Coop_DoWhiteOut
	end

Coop_Text_StoryPullWaiting::
	.string "{STR_VAR_1} is getting ready to\n"
	.string "join this battle…$"

Coop_Text_StoryGetReady::
	.string "{STR_VAR_1} is about to start a\n"
	.string "big battle, and you're joining in!\p"
	.string "Get ready, then you'll be brought\n"
	.string "over to {STR_VAR_1}.$"

Coop_Text_StoryWhatToDo::
	.string "What would you like to do?$"

Coop_Text_StoryHealed::
	.string "Your POKéMON were healed.$"

Coop_Text_StoryCantHeal::
	.string "Healing is off with the No POKéMON\n"
	.string "CENTER rule.$"

Coop_Text_StoryOnTheWay::
	.string "Off to {STR_VAR_1}!$"

Coop_Text_WhitedOut::
	.string "{PLAYER} has no more POKéMON that\n"
	.string "can fight!\p"
	.string "{PLAYER} whited out!$"

Coop_Text_StoryMenuHeal::
	.string "HEAL$"

Coop_Text_StoryMenuPokemon::
	.string "POKéMON BOXES$"

Coop_Text_StoryMenuItems::
	.string "ITEM STORAGE$"

Coop_Text_StoryMenuReady::
	.string "READY$"

@ A Pokemon caught in a co-op battle (without Nuzlocke): offer a nickname.
Coop_EventScript_NicknameCaught::
	lockall
	special Coop_PrepareCaughtNickname
	copyvar VAR_0x8008, VAR_RESULT
	bufferspeciesname STR_VAR_1, VAR_TEMP_1
	msgbox Text_GiveNicknameToThisMon, MSGBOX_YESNO
	goto_if_eq VAR_RESULT, NO, Coop_EventScript_NicknameCaughtDone
	goto_if_eq VAR_0x8008, 1, Coop_EventScript_NicknameCaughtBox
	call EventScript_GetGiftMonPartySlot
	call EventScript_ChangePokemonNickname
	goto Coop_EventScript_NicknameCaughtDone
Coop_EventScript_NicknameCaughtBox::
	call EventScript_NameReceivedBoxMon
Coop_EventScript_NicknameCaughtDone::
	releaseall
	end

Coop_Text_SoloBattle::
	.string "That battle was just for you, so\n"
	.string "you and {STR_VAR_1} split up.$"

Coop_Text_PartyLimit::
	.string "In co-op, each player carries 3\n"
	.string "POKéMON. The rest went to the PC.$"

Coop_Text_SplitForSafari::
	.string "You and {STR_VAR_1} split up to\n"
	.string "visit the SAFARI ZONE on your own.$"

@ Upstairs in a Pokemon Center: trade with the partner.
Coop_EventScript_OfferTrade::
	msgbox Coop_Text_AskTrade, MSGBOX_YESNO
	goto_if_eq VAR_RESULT, NO, Coop_EventScript_AskLinkUp
	special Coop_SendTradeRequest
	message Coop_Text_AskedToTrade
	waitmessage
	special Coop_WaitTradeAnswer
	waitstate
	goto_if_eq VAR_RESULT, FALSE, Coop_EventScript_TradeDeclined
	goto Coop_EventScript_DoTrade
	end

Coop_EventScript_TradeDeclined::
	msgbox Coop_Text_TradeDeclined
	releaseall
	end

Coop_EventScript_TradeRequest::
	lockall
	special Coop_BufferPartnerName
	msgbox Coop_Text_PartnerWantsTrade, MSGBOX_YESNO
	copyvar VAR_0x8004, VAR_RESULT
	special Coop_AnswerTradeRequest
	goto_if_eq VAR_0x8004, NO, Coop_EventScript_ReleaseAll
	goto Coop_EventScript_DoTrade
	end

Coop_EventScript_DoTrade::
	msgbox Coop_Text_ChooseTradeMon
	special ChoosePartyMon
	waitstate
	copyvar VAR_0x8005, VAR_0x8004
	special Coop_BufferPartnerName
	message Coop_Text_WaitingForPartner
	waitmessage
	special Coop_TradeExchangeMons
	waitstate
	goto_if_eq VAR_RESULT, FALSE, Coop_EventScript_TradeCanceled
	msgbox Coop_Text_ConfirmTrade, MSGBOX_YESNO
	copyvar VAR_0x8006, VAR_RESULT
	special Coop_BufferPartnerName
	message Coop_Text_WaitingForPartner
	waitmessage
	special Coop_TradeConfirm
	waitstate
	goto_if_eq VAR_RESULT, FALSE, Coop_EventScript_TradeCanceled
	closemessage
	special Coop_BeginTrade
	special DoInGameTradeScene
	waitstate
	releaseall
	end

Coop_EventScript_TradeCanceled::
	msgbox Coop_Text_TradeCanceled
	releaseall
	end

Coop_Text_AskTrade::
	.string "Do you want to trade POKéMON\n"
	.string "with {STR_VAR_1}?$"

Coop_Text_AskedToTrade::
	.string "You asked {STR_VAR_1} to trade.\n"
	.string "(B: stop waiting)$"

Coop_Text_TradeDeclined::
	.string "{STR_VAR_1} didn't want to trade.$"

Coop_Text_PartnerWantsTrade::
	.string "{STR_VAR_1} wants to trade\n"
	.string "POKéMON. OK?$"

Coop_Text_ChooseTradeMon::
	.string "Which POKéMON will you trade?$"

Coop_Text_WaitingForPartner::
	.string "Waiting for {STR_VAR_1}…\n"
	.string "(B: stop waiting)$"

Coop_Text_ConfirmTrade::
	.string "Trade your {STR_VAR_2} for\n"
	.string "{STR_VAR_1}'s {STR_VAR_3}?$"

Coop_Text_TradeCanceled::
	.string "The trade was canceled.$"
