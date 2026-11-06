#ifndef GUARD_LEVEL_CAP_H
#define GUARD_LEVEL_CAP_H

// Returns the highest level the player's Pokémon may currently reach through
// experience. Returns MAX_LEVEL when the Level Cap key is off.
u8 GetCurrentLevelCap(void);

// TRUE if the mon is at or above the current cap (and not already at MAX_LEVEL).
bool8 IsMonAtLevelCap(struct Pokemon *mon);

// Clamps an experience gain so the mon cannot pass the current level cap.
u32 GetCappedExpGain(struct Pokemon *mon, u32 expGain);

#endif // GUARD_LEVEL_CAP_H
