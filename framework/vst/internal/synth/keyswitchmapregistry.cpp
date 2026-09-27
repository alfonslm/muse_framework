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

#include "keyswitchmapregistry.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include "log.h"

using namespace muse::io;

namespace muse::vst {
KeyswitchMapRegistry& KeyswitchMapRegistry::instance()
{
    static KeyswitchMapRegistry inst;
    return inst;
}

path_t KeyswitchMapRegistry::mapsDirPath() const
{
    return globalConfiguration()->userDataPath() + "/Keyswitch Maps";
}

void KeyswitchMapRegistry::reload()
{
    m_loaded = false;
    m_mapsById.clear();
    ensureLoaded();
}

void KeyswitchMapRegistry::ensureLoaded()
{
    if (m_loaded) {
        return;
    }

    m_loaded = true;

    const path_t dir = mapsDirPath();
    if (!fileSystem()->exists(dir)) {
        return;
    }

    RetVal<paths_t> files = fileSystem()->scanFiles(dir, { "*.json" }, ScanMode::FilesInCurrentDir);
    if (!files.ret) {
        LOGW() << "Unable to scan keyswitch maps dir " << dir << ": " << files.ret.toString();
        return;
    }

    for (const path_t& file : files.val) {
        RetVal<ByteArray> content = fileSystem()->readFile(file);
        if (!content.ret) {
            LOGW() << "Unable to read keyswitch map file " << file << ": " << content.ret.toString();
            continue;
        }

        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(content.val.toQByteArrayNoCopy(), &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) {
            LOGW() << "Unable to parse keyswitch map file " << file << ": " << err.errorString();
            continue;
        }

        const QJsonObject object = doc.object();
        if (!KeyswitchMapLoader::isKeyswitchMapObject(object)) {
            //! NOTE Not a keyswitch map (e.g. the plain-text track-map-assignments.json test file
            //! living in the same folder) -- skip quietly.
            continue;
        }

        const std::string id = filename(file, false).toStdString();
        KeyswitchMap map = KeyswitchMapLoader::loadFromJson(object, id);
        if (map.id.empty()) {
            continue;
        }

        m_mapsById[id] = std::move(map);
    }
}

const KeyswitchMap* KeyswitchMapRegistry::mapById(const std::string& id)
{
    ensureLoaded();

    auto it = m_mapsById.find(id);
    return it != m_mapsById.cend() ? &it->second : nullptr;
}
}
