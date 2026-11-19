/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * FontDaw
 * The notation-first DAW
 *
 * Copyright (C) 2026 FontDaw contributors
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
#include "fontdawpresets.h"

#include <algorithm>

#include "log.h"

// FontDaw (shortcut work order v2, A2): skeleton. Each body is filled in by the implementation
// step named in docs/fontdaw/design/shortcuts/09-layouts-framework.md (FontDawDev), with tests in
// tests/fontdawpresets_tests.cpp. Until then they leave everything as upstream has it.

using namespace muse;
using namespace muse::shortcuts;

bool fontdaw::isUserPresetId(const std::string& id)
{
    return id.rfind(USER_PRESET_PREFIX, 0) == 0;
}

std::vector<PresetInfo> fontdaw::parseBuiltInPresets(const ValList& entries)
{
    std::vector<PresetInfo> result;
    for (const Val& entry : entries) {
        PresetInfo info;
        if (entry.type() == Val::Type::Map) {
            const ValMap map = entry.toMap();
            auto value = [&map](const char* key) {
                auto it = map.find(key);
                return it != map.end() ? it->second : Val();
            };
            info.id = value("id").toString();
            const std::string title = value("title").toString();
            if (!title.empty()) {
                // translated in the "shortcuts" context, like the scope titles; the app provides the
                // strings to translators (the cfg file is not read by lupdate)
                info.title = TranslatableString("shortcuts", String::fromStdString(title));
            }
            info.group = value("group").toString();
            info.complete = value("complete").toBool();
        } else {
            info.id = entry.toString();
        }

        if (info.id.empty() || isUserPresetId(info.id)) {
            LOGW() << "ignoring a built-in shortcuts preset without an id or with a reserved id: " << info.id;
            continue;
        }

        result.push_back(info);
    }
    return result;
}

RetVal<fontdaw::UserPresetFile> fontdaw::readUserPreset(const io::path_t&, const std::vector<std::string>&)
{
    // TODO(FontDaw shortcuts A2, step 5): read and check as declared
    return RetVal<UserPresetFile>(make_ret(Ret::Code::NotImplemented));
}

Ret fontdaw::writeUserPreset(const io::path_t&, const UserPresetFile&)
{
    // TODO(FontDaw shortcuts A2, step 5)
    return make_ret(Ret::Code::NotImplemented);
}

std::string fontdaw::makeUserPresetId(const std::string&, const std::vector<std::string>&)
{
    // TODO(FontDaw shortcuts A2, step 5)
    return std::string();
}

void fontdaw::applyCompletePreset(ShortcutList& defaults, const ShortcutList& preset)
{
    // rows, scopes and auto-repeat stay those of base + platform; their keys go
    for (Shortcut& sc : defaults) {
        sc.sequences.clear();
    }

    movePresetScopes(defaults, preset);

    for (const Shortcut& entry : preset) {
        auto it = std::find_if(defaults.begin(), defaults.end(), [&entry](const Shortcut& sc) {
            return sc.command == entry.command;
        });
        if (it != defaults.end()) {
            it->sequences = entry.sequences;
        } else {
            defaults.push_back(entry); // a command the base doesn't list: as the preset writes it
        }
    }
}

bool fontdaw::isCompletePreset(const std::string& presetId, const std::vector<PresetInfo>& builtIns)
{
    for (const PresetInfo& info : builtIns) {
        if (info.id == presetId) {
            return info.complete;
        }
    }
    return false;
}

void fontdaw::movePresetScopes(ShortcutList& base, const ShortcutList& preset)
{
    for (const Shortcut& entry : preset) {
        if (entry.scope.empty()) {
            continue;
        }
        for (Shortcut& sc : base) {
            if (sc.command == entry.command) {
                sc.scope = entry.scope;
            }
        }
    }
}

void fontdaw::applyRenames(ShortcutList&, const std::map<std::string, std::string>&)
{
    // TODO(FontDaw shortcuts A2, step 6)
}

bool fontdaw::isRegistered(const rcommand::Command& command, const rcommand::ICommandsRegister& commands)
{
    if (!command.isValid()) {
        return false;
    }

    // an unknown command gets the register's null CommandInfo, whose command is empty
    return commands.commandInfo(command).command == command;
}

void fontdaw::dropUnregistered(ShortcutList& shortcuts, const rcommand::ICommandsRegister& commands)
{
    auto it = std::remove_if(shortcuts.begin(), shortcuts.end(), [&commands](const Shortcut& sc) {
        if (isRegistered(sc.command, commands)) {
            return false;
        }
        LOGW() << "ignoring a shortcut for an unknown command in a built-in preset: " << sc.command;
        return true;
    });
    shortcuts.erase(it, shortcuts.end());
}
