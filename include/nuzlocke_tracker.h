#ifndef GUARD_NUZLOCKE_TRACKER_H
#define GUARD_NUZLOCKE_TRACKER_H

// Nuzlocke tracker: replaces the L-button Help System.
// Every Nuzlocke area remembers the first Pokémon met there (and whether it was caught),
// and the tracker lists the areas in the order a normal run visits them.

#define NUZLOCKE_TRACKER_SLOTS   0x57  // area IDs 0 (starter) to 0x56 (see NuzlockeLUT)
#define NUZLOCKE_TRACKER_CAUGHT  0x8000
#define NUZLOCKE_TRACKER_STARTER 0

void NuzlockeTracker_RecordEncounter(u8 mapsec, u16 species, bool8 caught);
void NuzlockeTracker_RecordStarter(u16 species);

void NuzlockeTracker_Open(void);
bool8 NuzlockeTracker_HandleInput(void);

#endif // GUARD_NUZLOCKE_TRACKER_H
