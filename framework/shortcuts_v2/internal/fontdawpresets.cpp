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
#include <cctype>

#include "global/io/dir.h"
#include "global/io/file.h"
#include "global/serialization/json.h"
#include "global/stringutils.h"

#include "log.h"

// FontDaw (shortcut work order v2, A2): docs/fontdaw/design/shortcuts/09-layouts-framework.md (FontDawDev);
// tests in tests/fontdawpresets_tests.cpp and tests/commandshortcutsregister_tests.cpp.

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

namespace {
//! false for anything but the expected shapes (the file is untrusted; a wrong type must not reach
//! a JsonValue conversion that assumes it)
bool shortcutsFromJson(const JsonValue& value, ShortcutList& result)
{
    if (!value.isArray()) {
        return false;
    }

    const JsonArray groups = value.toArray();
    for (size_t i = 0; i < groups.size(); ++i) {
        const JsonValue groupValue = groups.at(i);
        if (!groupValue.isObject()) {
            return false;
        }
        const JsonObject group = groupValue.toObject();
        const JsonValue scope = group.value("scope");
        const JsonValue list = group.value("shortcuts");
        if (!scope.isString() || !list.isArray()) {
            return false;
        }

        const JsonArray entries = list.toArray();
        for (size_t j = 0; j < entries.size(); ++j) {
            const JsonValue entryValue = entries.at(j);
            if (!entryValue.isObject()) {
                return false;
            }
            const JsonObject obj = entryValue.toObject();
            const JsonValue command = obj.value("command");
            const JsonValue sequences = obj.value("sequences");
            if (!command.isString() || !sequences.isArray()) {
                return false;
            }

            Shortcut sc;
            sc.scope = scope.toStdString();
            sc.command = rcommand::Command(command.toStdString());
            if (obj.contains("autorepeat")) {
                const JsonValue autorepeat = obj.value("autorepeat");
                if (!autorepeat.isBool()) {
                    return false;
                }
                sc.autoRepeat = autorepeat.toBool();
            }

            const JsonArray seqs = sequences.toArray();
            for (size_t k = 0; k < seqs.size(); ++k) {
                if (!seqs.at(k).isString()) {
                    return false;
                }
                sc.sequences.push_back(seqs.at(k).toStdString());
            }
            result.push_back(std::move(sc));
        }
    }
    return true;
}

JsonArray shortcutsToJson(const ShortcutList& shortcuts)
{
    std::vector<std::string> scopes;
    for (const Shortcut& sc : shortcuts) {
        if (std::find(scopes.begin(), scopes.end(), sc.scope) == scopes.end()) {
            scopes.push_back(sc.scope);
        }
    }

    JsonArray groups;
    for (const std::string& scope : scopes) {
        JsonArray list;
        for (const Shortcut& sc : shortcuts) {
            if (sc.scope != scope) {
                continue;
            }
            JsonArray seqs;
            for (const std::string& seq : sc.sequences) {
                seqs.append(seq);
            }
            JsonObject obj;
            obj.set("command", sc.command.toString());
            obj.set("sequences", seqs);
            if (!sc.autoRepeat) {
                obj.set("autorepeat", false);
            }
            list.append(obj);
        }
        JsonObject group;
        group.set("scope", scope);
        group.set("shortcuts", list);
        groups.append(group);
    }
    return groups;
}
}

std::string fontdaw::validTitle(const std::string& title)
{
    std::string trimmed = title;
    strings::trim(trimmed);
    for (unsigned char c : trimmed) {
        if (c < 0x20 || c == 0x7f) { // control characters and line breaks
            return std::string();
        }
    }
    if (trimmed.empty() || String::fromStdString(trimmed).size() > USER_PRESET_TITLE_MAX) {
        return std::string();
    }
    return trimmed;
}

RetVal<fontdaw::UserPresetFile> fontdaw::readUserPreset(const io::path_t& path, const std::vector<std::string>& builtInIds)
{
    ByteArray data;
    Ret ret = io::File::readFile(path, data);
    if (!ret) {
        return RetVal<UserPresetFile>(ret);
    }

    const JsonDocument doc = JsonDocument::fromJson(data);
    if (!doc.isObject()) {
        LOGE() << "user shortcuts preset is not a JSON object: " << path;
        return RetVal<UserPresetFile>(make_ret(Ret::Code::UnknownError));
    }

    const JsonObject root = doc.rootObject();
    const JsonValue version = root.value("version");
    const JsonValue title = root.value("title");
    const JsonValue parent = root.value("parent");
    if (!version.isNumber() || !title.isString() || !parent.isString()) {
        LOGE() << "user shortcuts preset lacks version, title or parent: " << path;
        return RetVal<UserPresetFile>(make_ret(Ret::Code::UnknownError));
    }

    UserPresetFile file;
    file.version = version.toInt();
    file.title = validTitle(title.toStdString());
    file.parent = parent.toStdString();

    if (file.version < 1 || file.version > USER_PRESET_FILE_VERSION) {
        LOGE() << "user shortcuts preset has an unknown version " << file.version << ": " << path;
        return RetVal<UserPresetFile>(make_ret(Ret::Code::NotSupported));
    }
    if (file.title.empty()) {
        LOGE() << "user shortcuts preset has no usable title: " << path;
        return RetVal<UserPresetFile>(make_ret(Ret::Code::UnknownError));
    }
    if (std::find(builtInIds.begin(), builtInIds.end(), file.parent) == builtInIds.end()) {
        LOGE() << "user shortcuts preset's parent is not a built-in preset (" << file.parent << "): " << path;
        return RetVal<UserPresetFile>(make_ret(Ret::Code::UnknownError));
    }

    if (!shortcutsFromJson(root.value("shortcuts"), file.shortcuts)) {
        LOGE() << "user shortcuts preset has malformed shortcuts: " << path;
        return RetVal<UserPresetFile>(make_ret(Ret::Code::UnknownError));
    }
    return RetVal<UserPresetFile>::make_ok(file);
}

Ret fontdaw::writeUserPreset(const io::path_t& path, const UserPresetFile& file)
{
    JsonObject root;
    root.set("version", USER_PRESET_FILE_VERSION);
    root.set("title", file.title);
    root.set("parent", file.parent);
    root.set("shortcuts", shortcutsToJson(file.shortcuts));

    return io::File::writeFile(path, JsonDocument(root).toJson());
}

std::string fontdaw::makeUserPresetId(const std::string& title, const std::vector<std::string>& takenIds)
{
    std::string slug;
    for (unsigned char c : title) {
        if (std::isalnum(c) && c < 0x80) {
            slug += static_cast<char>(std::tolower(c));
        } else if (!slug.empty() && slug.back() != '-') {
            slug += '-';
        }
        if (slug.size() >= 40) {
            break;
        }
    }
    while (!slug.empty() && slug.back() == '-') {
        slug.pop_back();
    }
    if (slug.empty()) {
        slug = "layout";
    }

    const std::string base = std::string(USER_PRESET_PREFIX) + slug;
    std::string id = base;
    for (int n = 2; std::find(takenIds.begin(), takenIds.end(), id) != takenIds.end(); ++n) {
        id = base + "-" + std::to_string(n);
    }
    return id;
}

std::vector<std::string> fontdaw::builtInIds(const IShortcutsConfiguration& config)
{
    std::vector<std::string> ids;
    for (const PresetInfo& info : config.builtInPresetInfos()) {
        ids.push_back(info.id);
    }
    return ids;
}

io::path_t fontdaw::userPresetPath(const IShortcutsConfiguration& config, const std::string& id)
{
    // the id comes from the settings or a file name: never a path of its own
    if (!isUserPresetId(id) || !io::isAllowedFileName(id) || id.find_first_of("/\\") != std::string::npos) {
        LOGE() << "invalid user shortcuts preset id: " << id;
        return io::path_t();
    }
    return config.userPresetsDirPath() + "/" + id + ".json";
}

std::string fontdaw::presetToApply(const std::string& presetName, const IShortcutsConfiguration& config)
{
    if (!isUserPresetId(presetName)) {
        return presetName;
    }

    RetVal<UserPresetFile> file = readUserPreset(userPresetPath(config, presetName), builtInIds(config));
    return file.ret ? file.val.parent : std::string();
}

bool fontdaw::readUserPresetShortcuts(ShortcutList& shortcuts, const io::path_t& path, const IShortcutsConfiguration& config)
{
    RetVal<UserPresetFile> file = readUserPreset(path, builtInIds(config));
    if (!file.ret) {
        return false;
    }
    shortcuts = file.val.shortcuts;
    return true;
}

Ret fontdaw::writeUserPresetShortcuts(const io::path_t& path, const ShortcutList& shortcuts, const IShortcutsConfiguration& config)
{
    RetVal<UserPresetFile> file = readUserPreset(path, builtInIds(config));
    if (!file.ret) {
        // an unusable file is kept as it is, never overwritten
        return file.ret;
    }
    file.val.shortcuts = shortcuts;
    return writeUserPreset(path, file.val);
}

std::vector<PresetInfo> fontdaw::listUserPresets(const IShortcutsConfiguration& config)
{
    std::vector<PresetInfo> result;
    const std::vector<std::string> builtIns = builtInIds(config);

    RetVal<io::paths_t> files = io::Dir::scanFiles(config.userPresetsDirPath(), { "*.json" }, io::ScanMode::FilesInCurrentDir);
    if (!files.ret) {
        return result;
    }

    for (const io::path_t& path : files.val) {
        const std::string id = io::completeBasename(path).toStdString();
        if (!isUserPresetId(id)) {
            continue;
        }
        RetVal<UserPresetFile> file = readUserPreset(path, builtIns);
        if (!file.ret) {
            continue;
        }
        PresetInfo info;
        info.id = id;
        info.title = TranslatableString::untranslatable(String::fromStdString(file.val.title));
        info.parent = file.val.parent;
        info.isUser = true;
        result.push_back(info);
    }

    std::sort(result.begin(), result.end(), [](const PresetInfo& a, const PresetInfo& b) {
        return a.title.str.toStdString() < b.title.str.toStdString();
    });
    return result;
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

void fontdaw::applyRenames(ShortcutList& shortcuts, const std::map<std::string, std::string>& renames)
{
    if (renames.empty()) {
        return;
    }

    for (Shortcut& sc : shortcuts) {
        auto it = renames.find(sc.command.toString());
        if (it != renames.end()) {
            sc.command = rcommand::Command(it->second);
        }
    }
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
