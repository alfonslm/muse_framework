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

// FontDaw (shortcut work order v2, A2): tests for internal/fontdawpresets.*

#include <gtest/gtest.h>

#include "internal/fontdawpresets.h"

using namespace muse;
using namespace muse::shortcuts;

namespace {
Val object(std::initializer_list<std::pair<const std::string, Val> > fields)
{
    return Val(ValMap(fields));
}
}

// Upstream's form, a plain name, is still read; its title is left empty (presetTitle() applies)
TEST(Shortcuts_FontDawPresetsTests, ParseBuiltIn_PlainNames)
{
    const std::vector<PresetInfo> infos = fontdaw::parseBuiltInPresets({ Val("shortcuts_azerty") });

    ASSERT_EQ(infos.size(), 1u);
    EXPECT_EQ(infos.at(0).id, "shortcuts_azerty");
    EXPECT_TRUE(infos.at(0).title.isEmpty());
    EXPECT_TRUE(infos.at(0).group.empty());
    EXPECT_FALSE(infos.at(0).complete);
    EXPECT_FALSE(infos.at(0).isUser);
}

TEST(Shortcuts_FontDawPresetsTests, ParseBuiltIn_Objects)
{
    const std::vector<PresetInfo> infos = fontdaw::parseBuiltInPresets({
        object({ { "id", Val("shortcuts_musescore47") }, { "title", Val("MuseScore 4.7") },
                 { "group", Val("musescore") }, { "complete", Val(true) } }),
        object({ { "id", Val("shortcuts_fontdaw_right") }, { "title", Val("Mouse in the right hand") },
                 { "group", Val("fontdaw") } }),
    });

    ASSERT_EQ(infos.size(), 2u);
    EXPECT_EQ(infos.at(0).id, "shortcuts_musescore47");
    EXPECT_EQ(infos.at(0).title.str, String(u"MuseScore 4.7"));
    EXPECT_EQ(infos.at(0).group, "musescore");
    EXPECT_TRUE(infos.at(0).complete);

    EXPECT_EQ(infos.at(1).id, "shortcuts_fontdaw_right");
    EXPECT_EQ(infos.at(1).group, "fontdaw");
    EXPECT_FALSE(infos.at(1).complete);
}

// The order of shortcuts.cfg is kept, plain names and objects mixed
TEST(Shortcuts_FontDawPresetsTests, ParseBuiltIn_MixedKeepsOrder)
{
    const std::vector<PresetInfo> infos = fontdaw::parseBuiltInPresets({
        object({ { "id", Val("b") }, { "title", Val("B") } }), Val("a"), object({ { "id", Val("c") } }),
    });

    ASSERT_EQ(infos.size(), 3u);
    EXPECT_EQ(infos.at(0).id, "b");
    EXPECT_EQ(infos.at(1).id, "a");
    EXPECT_EQ(infos.at(2).id, "c");
    EXPECT_TRUE(infos.at(2).title.isEmpty());
}

// An entry without an id, or with the user presets' reserved prefix, is not a built-in preset
TEST(Shortcuts_FontDawPresetsTests, ParseBuiltIn_SkipsEmptyAndReservedIds)
{
    const std::vector<PresetInfo> infos = fontdaw::parseBuiltInPresets({
        Val(""), object({ { "title", Val("No id") } }), Val("user-mine"), Val("ok"),
    });

    ASSERT_EQ(infos.size(), 1u);
    EXPECT_EQ(infos.at(0).id, "ok");
}

TEST(Shortcuts_FontDawPresetsTests, UserPresetIdPrefix)
{
    EXPECT_TRUE(fontdaw::isUserPresetId("user-my-layout"));
    EXPECT_FALSE(fontdaw::isUserPresetId("shortcuts_azerty"));
    EXPECT_FALSE(fontdaw::isUserPresetId(""));
    EXPECT_FALSE(fontdaw::isUserPresetId("myuser-x"));
}
