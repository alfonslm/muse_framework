/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * MuseScore Studio
 * Music Composition & Notation
 *
 * Copyright (C) 2026 MuseScore Limited and others
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "mpe/mpetypes.h"

class QJsonObject;

namespace muse::vst {
struct KeyswitchNote
{
    int16_t pitch = -1;
    int16_t velocity = 100;

    bool isValid() const { return pitch >= 0 && pitch <= 127; }
};

//! NOTE A keyswitch map for one instrument/patch: which note (or note|velocity) to send
//! for each notation articulation/technique, loaded from a user JSON file. See
//! KeyswitchMapLoader::loadFromJson for the file format.
struct KeyswitchMap
{
    std::string id;

    std::unordered_map<mpe::ArticulationType, KeyswitchNote> keyswitches;

    //! NOTE When a note carries more than one mapped articulation, the first type found here wins.
    //! Falls back to a built-in "momentary articulations beat sustained techniques" order when empty.
    std::vector<mpe::ArticulationType> priority;

    mpe::ArticulationType defaultType = mpe::ArticulationType::Standard;

    int leadMs = 5;
    bool writeEveryNote = false;

    const KeyswitchNote* keyswitchForNote(const mpe::ArticulationMap& noteArticulations) const;
};

namespace KeyswitchMapLoader {
//! NOTE Returns an invalid (empty id) map if `object` isn't a keyswitch map at all
//! (no "keyswitches" key) -- lets a map loader skip unrelated JSON files in the same folder.
KeyswitchMap loadFromJson(const QJsonObject& object, const std::string& id);

bool isKeyswitchMapObject(const QJsonObject& object);

//! NOTE Exposed for tests. Resolves both canonical ArticulationType names (e.g. "Pizzicato")
//! and the Keyswitch Creator plugin's lower-case names (e.g. "pizz.", "staccato").
bool articulationTypeFromMapKey(const std::string& key, mpe::ArticulationType& out);
}
}
