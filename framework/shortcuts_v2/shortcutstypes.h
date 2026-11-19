/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * MuseScore Studio
 * Music Composition & Notation
 *
 * Copyright (C) 2021 MuseScore Limited and others
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

#include <set>
#include <string>
#include <vector>
#include <utility>
#include <vector>

#include <QKeySequence>

#include "global/stringutils.h"
#include "rcommand/commandtypes.h"

#include "global/logstream.h"
#include "global/types/translatablestring.h"

namespace muse::shortcuts {
//! FontDaw (shortcut work order v2, A2): a preset as the presets list shows it
//! (internal/fontdawpresets.h). Built-in presets come from shortcuts.cfg; user presets ("Save as…")
//! from <user data>/shortcuts/presets/.
struct PresetInfo {
    std::string id;           //! as availablePresets() gives it; "" is the base
    TranslatableString title; //! empty for upstream's plain-name entries: presetTitle() applies
    std::string group;        //! the heading it is listed under, e.g. "fontdaw", "musescore"
    std::string parent;       //! user presets: the built-in id it is a diff on
    bool complete = false;    //! built-in: a whole key set (fontdaw::applyCompletePreset)
    bool isUser = false;      //! made with "Save as…"; deletable
};

struct Shortcut
{
    rcommand::Command command;
    std::string scope;
    std::vector<std::string> sequences;
    QKeySequence::StandardKey standardKey = QKeySequence::UnknownKey;
    bool autoRepeat = true;

    Shortcut() = default;

    bool isValid() const
    {
        return command.isValid();
    }

    bool operator ==(const Shortcut& sc) const
    {
        return command == sc.command
               && scope == sc.scope
               && sequences == sc.sequences
               && standardKey == sc.standardKey
               && autoRepeat == sc.autoRepeat;
    }

    std::string sequencesAsString() const { return sequencesToString(sequences); }

    static std::string sequencesToString(const std::vector<std::string>& seqs)
    {
        return muse::strings::join(seqs, ", ");
    }

    static std::vector<std::string> sequencesFromString(const std::string& str)
    {
        std::vector<std::string> seqs;
        muse::strings::split(str, seqs, ", ");
        return seqs;
    }

    void clear()
    {
        sequences.clear();
        standardKey = QKeySequence::StandardKey::UnknownKey;
    }
};

using ShortcutList = std::vector<Shortcut>;

inline bool needIgnoreKey(Qt::Key key)
{
    static const std::set<Qt::Key> ignoredKeys {
        Qt::Key_Shift,
        Qt::Key_Control,
        Qt::Key_Meta,
        Qt::Key_Alt,
        Qt::Key_AltGr,
        Qt::Key_CapsLock,
        Qt::Key_NumLock,
        Qt::Key_ScrollLock,
        Qt::Key_unknown
    };

    return ignoredKeys.find(key) != ignoredKeys.end();
}

inline std::pair<Qt::Key, Qt::KeyboardModifiers> correctKeyInput(Qt::Key key, Qt::KeyboardModifiers modifiers)
{
    // replace Backtab with Shift+Tab
    if (key == Qt::Key_Backtab && modifiers == Qt::ShiftModifier) {
        key = Qt::Key_Tab;
    }

    modifiers &= ~Qt::KeypadModifier;

    return { key, modifiers };
}

inline QString sequencesToNativeText(const std::vector<std::string>& sequences)
{
    std::vector<std::string> seqs;

    for (const std::string& sequence : sequences) {
        seqs.push_back(QKeySequence(QString::fromStdString(sequence)).toString(QKeySequence::NativeText).toStdString());
    }

    return QString::fromStdString(Shortcut::sequencesToString(seqs));
}

inline bool canShortcutsConflict(const std::string& scope1, const std::string& scope2)
{
    return scope1 == scope2;
}
}

inline muse::logger::Stream& operator<<(muse::logger::Stream& s, const muse::shortcuts::Shortcut& sc)
{
    s << "{" << sc.scope << ", " << sc.command.toString() << ", `" << sc.sequencesAsString() << "`}";
    return s;
}

inline muse::logger::Stream& operator<<(muse::logger::Stream& s, const std::vector<muse::shortcuts::Shortcut>& v)
{
    s << '[';
    for (size_t i = 0; i < v.size(); ++i) {
        s << v.at(i);
        if (i < v.size() - 1) {
            s << ',';
        }
    }
    s << ']';
    return s;
}
