# Pokémon FireRed and LeafGreen+ — Level Cap, Team Exp & Randomizer fork

This is a fork of [FRLG+ by Deokishisu](https://github.com/Deokishisu/FRLG-Plus). Several features are ported from [Modern Emerald by resetes12](https://github.com/resetes12/pokeemerald). Everything below is a change from FRLG+; the rest of the game is FRLG+ as released.

**Saves from FRLG+ or earlier versions of this fork aren't compatible. Start a new game.** Trading the new Pokémon to other games isn't supported.

## Changes from FRLG+

### New Key System options
- **Level Cap** (off/on): Pokémon can't level past the strongest Pokémon of the next Gym Leader (Brock 14 … Giovanni 50). With all 8 badges, the Champion's level (63) applies to the whole League, rising to 75 once rematches unlock. The cap follows the Difficulty key (higher on Challenge, lower on Easy). Capped Pokémon still gain EVs, and Rare Candy won't work on them.
- **Team Exp.** (off/1/2×/full): Pokémon that didn't battle still get experience, like the modern Exp. Share. Fainted Pokémon and Eggs get none.
- **Exp. Modifier** now goes up to 4×: 0×, 1/2×, 1×, 2×, 3×, 4×.

### Randomizer (always on)
Seeded from your Trainer ID, so every new game is different and a save always stays the same.
- Wild Pokémon (grass, water, fishing, Rock Smash, Safari Zone) become a random Pokémon of the same evolution stage, differently in each area. No legendaries.
- The three starters are random unevolved Pokémon; Oak names the one in the ball you check. The rival keeps his usual team.
- Gift Pokémon and Eggs become random non-legendaries of the same stage, and the "obtained" messages name the right Pokémon.
- Static encounters: non-legendaries swap with non-legendaries of the same stage; legendaries swap with other legendaries.
- Abilities: each species borrows another same-stage species' ability (a Pokémon keeps its ability slot).
- Items from item balls, hidden items and NPC gifts are randomized. Key items, HMs, Poké Balls and items any evolution needs are left alone; TMs become a TM you don't own yet when possible.

### Battle
- **Physical/special split**: every move is physical, special or status on its own (categories from Modern Emerald). Weather and Flash Fire also boost physical Fire and Water moves, and Thick Fat, Hustle, type-boosting items and Counter/Mirror Coat follow the category.

### 41 new Pokémon
Mime Jr., Munchlax, Bonsly, Mantyke, Happiny, Chingling, Budew, Roserade, Dusknoir, Ambipom, Electivire, Froslass, Gallade, Gliscor, Honchkrow, Lickilicky, Magmortar, Magnezone, Mamoswine, Mismagius, Porygon-Z, Probopass, Rhyperior, Tangrowth, Togekiss, Weavile, Yanmega, Leafeon, Glaceon, Sylveon, Annihilape, Farigiraf, Dudunsparce, Regidrago, Regieleki, Regigigas, Arceus, Wyrdeer, Kleavor, Ursaluna and Bloodmoon Ursaluna.
- Data, sprites, icons, cries and Pokédex entries come from Modern Emerald, using official stats. There's no Fairy type, so Gen 3 typings are used (Sylveon is Normal, Togekiss is Normal/Flying).
- They have National Dex numbers after Deoxys and Pokédex habitat pages. Completion checks still use the original 386, and evolving into a new Pokémon doesn't need the National Dex.
- New abilities: Transistor and Dragon's Maw (1.5× Electric/Dragon moves) and Multitype (extra boost with a matching type-boosting item). Sylveon gets Cute Charm, since Pixilate needs the Fairy type.
- Modern Emerald counts 44 new Pokémon; the other three are Deoxys formes, which FRLG+ already has.

### Evolutions
See **[docs/EVOLUTION_CHANGES.md](docs/EVOLUTION_CHANGES.md)** for the full list. In short:
- Trade evolutions now happen by level-up; those that needed a held item happen at a set level while holding it.
- Magnezone, Probopass (Thunder Stone), Leafeon (Leaf Stone), Glaceon (Water Stone while holding Never-Melt-Ice), Sylveon (Moon Stone), Milotic (Water Stone) and Roserade (Sun Stone) evolve with stones.
- Tangela, Yanma, Piloswine, Girafarig and Dunsparce learn the move they need for their new evolutions.

### Smaller changes and fixes
- Oak's starter text and the Lapras, Eevee and Tentacool gift messages now name the Pokémon dynamically.
- New Help System entries for Level Cap and Team Exp.
- Fixed the Advanced Keys page reading its labels from the wrong list.
- Build fixes for Linux/WSL: a missing `#include <cstdint>` in `tools/mapjson` and a wrong-case tileset path in `src/tileset_anims.c`.

---

## Original FRLG+ readme

# Pokémon FireRed and LeafGreen+

This is the repository for the Pokémon FireRed and LeafGreen+ hack, based on [pret's decompilation of Pokémon FireRed and LeafGreen](https://github.com/pret/pokefirered).
* [**View the FAQ**](https://github.com/Deokishisu/FRLG-Plus/blob/master/FAQ.md)
* [**View the full features list**](https://github.com/Deokishisu/FRLG-Plus/blob/master/FEATURES.md)
* [**Download the patch**](https://github.com/Deokishisu/FRLG-Plus/releases/latest)
* Please direct questions and comments to [**the PokéCommunity thread**](https://www.pokecommunity.com/showthread.php?t=454382).

* Support Development
  * [I have a Ko-fi link](https://ko-fi.com/deokishisu), tips are appreciated!

## See also

For other pret projects, see [pret.github.io](https://pret.github.io/).

## Discord server for pret

You can find pret on [Discord](https://discord.gg/d5dubZ3). This is not the Discord server for this hack.
