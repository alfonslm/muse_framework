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

#include <string>
#include <vector>

#include "modularity/imoduleinterface.h"
#include "shortcutstypes.h"
#include "async/channel.h"
#include "async/notification.h"
#include "types/ret.h"
#include "types/retval.h"
#include "io/path.h"

namespace muse::shortcuts {
class ICommandShortcutsRegister : MODULE_GLOBAL_INTERFACE
{
    INTERFACE_ID(ICommandShortcutsRegister)
public:
    virtual ~ICommandShortcutsRegister() = default;

    virtual const ShortcutList& shortcuts() const = 0;
    virtual Ret updateShortcuts(const ShortcutList& shortcuts) = 0;
    virtual void resetShortcuts() = 0;
    virtual async::Notification shortcutsChanged() const = 0;

    virtual Ret setAdditionalShortcuts(const std::string& context, const ShortcutList& shortcuts) = 0;

    virtual ShortcutList shortcutsForSequence(const std::string& sequence) const = 0;
    virtual const Shortcut& shortcut(const rcommand::Command& command) const = 0;
    virtual const Shortcut& defaultShortcut(const rcommand::Command& command) const = 0;

    virtual Ret importFromFile(const io::path_t& filePath) = 0;
    virtual Ret exportToFile(const io::path_t& filePath) const = 0;

    virtual std::vector<std::string> availablePresets() const = 0;

    virtual std::string currentPresetName() const = 0;
    virtual void setCurrentPresetName(const std::string& presetName) = 0;
    virtual async::Channel<std::string> currentPresetNameChanged() const = 0;

    virtual bool isPresetEdited(const std::string& presetName) const = 0;
    virtual bool canDeletePreset(const std::string& presetName) const = 0;
    virtual void deletePreset(const std::string& presetName) = 0;

    // FontDaw (shortcut work order v2, A2)
    //! availablePresets() with metadata: the base (unless hidden by the cfg), the built-in presets in
    //! shortcuts.cfg order, then the user presets by title. availablePresets() is unchanged.
    virtual std::vector<PresetInfo> presetInfos() const = 0;
    //! "Save as…": the current merged list as a new user preset (one file, a diff on the current
    //! built-in preset, or on the current user preset's parent), made current. Returns its id.
    virtual RetVal<std::string> savePresetAs(const std::string& title) = 0;
    //! A preset was added or deleted: every window's drop-down refreshes.
    virtual async::Notification presetsChanged() const = 0;

    // for testflow tests
    virtual void reload(bool onlyDef = false) = 0;
};
}
