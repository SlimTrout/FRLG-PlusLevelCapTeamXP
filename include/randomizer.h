#ifndef GUARD_RANDOMIZER_H
#define GUARD_RANDOMIZER_H

// Seeded randomizer ported from Modern Emerald (tx_randomizer_and_challenges).
// Every result is derived from the player's Trainer ID, so each new game gets its
// own randomization and it stays the same for the whole playthrough.

enum
{
    RANDOMIZE_WILD,     // grass, water, fishing, rock smash: same stage, differs per area
    RANDOMIZE_GIFT,     // gift Pokemon and eggs: same stage, never legendary
    RANDOMIZE_STATIC,   // static encounters: legendaries swap with legendaries
    RANDOMIZE_ABILITY,  // which species a Pokemon borrows its ability from
};

u16 RandomizerSeededModulo(u32 value, u16 modulo);
u16 GetRandomizedSpecies(u16 species, u8 kind);
u16 GetRandomizedStarter(u8 starterId);
u16 GetRandomizedItem(u16 itemId);

// Gift text: scripts buffer the original species' name after giving it, so the
// last gift's original species is shown as what was actually received.
u16 GetGiftSpeciesForDisplay(u16 species);
void ResetGiftSpeciesForDisplay(void);
u16 RandomizeGiftSpecies(u16 species);
void SetRandomizedStarterVar(void);
void RandomizeObtainedItem(void);
void RandomizeHiddenItem(void);

#endif // GUARD_RANDOMIZER_H
