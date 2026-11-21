/* karabas: force-included in every file of the Heretic build. Replaces src/doomfeatures.h of
   chocolate-doom 2.3.0 (its guard): no multiplayer, no dehacked, no WAD merge; FEATURE_SOUND
   only in the board build (-DFEATURE_SOUND). */
#pragma once
#define DOOM_FEATURES_H
