@ Brainy Blue Co-op: talking to your partner, link requests and notices.
@ Link modes (Coop_GetLinkMode): 0 = split, 1 = leading, 2 = following.

Coop_EventScript_TalkToPartner::
	lockall
	special Coop_BufferPartnerName
	specialvar VAR_RESULT, Coop_GetLinkMode
	goto_if_eq VAR_RESULT, 1, Coop_EventScript_LeaderMenu
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
	msgbox Coop_Text_AskForControl, MSGBOX_YESNO
	goto_if_eq VAR_RESULT, YES, Coop_EventScript_AskControl
	msgbox Coop_Text_SplitOff, MSGBOX_YESNO
	goto_if_eq VAR_RESULT, YES, Coop_EventScript_Split
	releaseall
	end

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
	goto_if_eq VAR_0x8004, NO, Coop_EventScript_ReleaseAll
	msgbox Coop_Text_NowFollowing
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
