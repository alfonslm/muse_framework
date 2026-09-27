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

#include <unordered_map>

#include "modularity/ioc.h"
#include "io/ifilesystem.h"
#include "global/iglobalconfiguration.h"

#include "keyswitchmap.h"

namespace muse::vst {
//! NOTE Loads *.json keyswitch maps from Documents/MuseScore4/Keyswitch Maps (no UI yet --
//! see KEYSWITCH-INSTRUCTIONS.md Step 2/4). Any JSON file in that folder without a top-level
//! "keyswitches" key is silently skipped, so a hand-written test track-map-assignments.json
//! can live alongside the actual maps.
class KeyswitchMapRegistry
{
public:
    static KeyswitchMapRegistry& instance();

    const KeyswitchMap* mapById(const std::string& id);
    void reload();

private:
    KeyswitchMapRegistry() = default;

    void ensureLoaded();
    io::path_t mapsDirPath() const;

    GlobalInject<io::IFileSystem> fileSystem;
    GlobalInject<IGlobalConfiguration> globalConfiguration;

    bool m_loaded = false;
    std::unordered_map<std::string, KeyswitchMap> m_mapsById;
};
}
