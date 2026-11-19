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
#pragma once

#include <map>
#include <string>
#include <vector>

#include "global/io/path.h"
#include "global/types/retval.h"
#include "global/types/val.h"
#include "rcommand/icommandsregister.h"

#include "../shortcutstypes.h"

//! FontDaw (shortcut work order v2, A2): FontDaw's additions to shortcuts v2's presets, kept in this
//! file so upstream's files carry only one-line hooks (upstream has announced its own user
//! presets; a later merge then touches little). Plain functions, unit-tested on their own.
//!
//! Terms: a *built-in* preset is listed in shortcuts.cfg ("command_shortcuts_presets"); a *user*
//! preset is made with "Save as…" and lives in <user data>/shortcuts/presets/.
namespace muse::shortcuts::fontdaw {
//! User preset ids start with this, so they never equal a built-in id or the base names
//! "shortcuts" / "shortcuts_mac" (architecture check of #272, change 3).
inline constexpr const char* USER_PRESET_PREFIX = "user-";
inline constexpr int USER_PRESET_FILE_VERSION = 1;
inline constexpr size_t USER_PRESET_TITLE_MAX = 64;

bool isUserPresetId(const std::string& id);

//! "command_shortcuts_presets" entries: a string is upstream's form ({id = it}, the title then
//! comes from upstream's presetTitle()); an object is {"id", "title", "group", "complete"}.
//! "command_shortcuts_hide_base": true hides the bare base from presetInfos() (for A3).
std::vector<PresetInfo> parseBuiltInPresets(const ValList& entries);

//! A user preset file (one file holds the preset and the user's later edits to it, change 4):
//!   { "version": 1, "title": "…", "parent": "<built-in id>", "shortcuts": [ <upstream's array> ] }
//! "shortcuts" is a diff on the parent's resolved list. The file is untrusted input.
struct UserPresetFile {
    int version = USER_PRESET_FILE_VERSION;
    std::string title;
    std::string parent;
    ShortcutList shortcuts;
};

//! Reads and checks a user preset file. Fails (and the caller falls back to the base, keeping the
//! file) when: the file is missing or not JSON; version is newer than USER_PRESET_FILE_VERSION; the
//! parent is not a built-in id; the title is empty or longer than USER_PRESET_TITLE_MAX after
//! trimming (titles are untrusted: shown as plain text only).
RetVal<UserPresetFile> readUserPreset(const io::path_t& path, const std::vector<std::string>& builtInIds);
Ret writeUserPreset(const io::path_t& path, const UserPresetFile& file);

//! A unique id for a new user preset: USER_PRESET_PREFIX + a file-safe form of the title, with a
//! number added if taken.
std::string makeUserPresetId(const std::string& title, const std::vector<std::string>& takenIds);

//! Complete built-in presets (architecture check of #246, M1): keep rows, scopes and auto-repeat of
//! `defaults` (base + platform), drop all their sequences, then apply `preset`.
void applyCompletePreset(ShortcutList& defaults, const ShortcutList& preset);

//! "A preset's scope wins" (choice 4): before upstream's mergeShortcuts() applies a built-in preset,
//! move each base command the preset lists under another (non-empty) scope into that scope. The
//! merge then keeps it there, and its same-scope clash rule works on the new scope, whatever the
//! order of entries in either file.
void movePresetScopes(ShortcutList& base, const ShortcutList& preset);

//! Renames (change: in memory, on read): old command URI -> new, from the app's cfg
//! ("command_renames"). Applied to every user list as it is read; files are rewritten only by a
//! later save.
void applyRenames(ShortcutList& shortcuts, const std::map<std::string, std::string>& renames);

//! Built-in files only (change 1): drop entries whose command is not registered or empty (the null
//! CommandInfo has availability All, so "registered" is tested by the command itself). User files
//! keep such entries (an extension may register later, and updateShortcuts() rewrites the file
//! from memory); the list model hides them.
void dropUnregistered(ShortcutList& shortcuts, const rcommand::ICommandsRegister& commands);

//! Whether \p command is non-empty and known to the commands register (its CommandInfo names it).
bool isRegistered(const rcommand::Command& command, const rcommand::ICommandsRegister& commands);
}
