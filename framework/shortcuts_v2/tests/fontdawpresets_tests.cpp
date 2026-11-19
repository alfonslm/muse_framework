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
#include <gmock/gmock.h>

#include <fstream>

#include <QTemporaryDir>

#include "internal/fontdawpresets.h"
#include "mocks/shortcutsconfigurationmock.h"
#include "rcommand/tests/mocks/commandsregistermock.h"

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

// step 3: "registered" means the register knows the command by name; an empty command never is
TEST(Shortcuts_FontDawPresetsTests, IsRegistered)
{
    using ::testing::_;
    using ::testing::Invoke;
    using ::testing::NiceMock;

    NiceMock<rcommand::CommandsRegisterMock> commands;
    rcommand::CommandInfo known;
    known.command = rcommand::Command("test://known");
    static rcommand::CommandInfo null;
    ON_CALL(commands, commandInfo(_)).WillByDefault(Invoke([&known](const rcommand::Command& c) -> const rcommand::CommandInfo& {
        return c == known.command ? known : null;
    }));

    EXPECT_TRUE(fontdaw::isRegistered(rcommand::Command("test://known"), commands));
    EXPECT_FALSE(fontdaw::isRegistered(rcommand::Command("test://unknown"), commands));
    EXPECT_FALSE(fontdaw::isRegistered(rcommand::Command(), commands));

    ShortcutList list;
    Shortcut a;
    a.command = rcommand::Command("test://known");
    Shortcut b;
    b.command = rcommand::Command("test://unknown");
    Shortcut e;
    list = { a, b, e };
    fontdaw::dropUnregistered(list, commands);
    ASSERT_EQ(list.size(), 1u);
    EXPECT_EQ(list.at(0).command, rcommand::Command("test://known"));
}

TEST(Shortcuts_FontDawPresetsTests, MakeUserPresetId)
{
    EXPECT_EQ(fontdaw::makeUserPresetId("My keys!", {}), "user-my-keys");
    EXPECT_EQ(fontdaw::makeUserPresetId("My keys", { "user-my-keys" }), "user-my-keys-2");
    EXPECT_EQ(fontdaw::makeUserPresetId("My keys", { "user-my-keys", "user-my-keys-2" }), "user-my-keys-3");
    EXPECT_EQ(fontdaw::makeUserPresetId("Åäö", {}), "user-layout");
    // never a built-in id: always prefixed
    EXPECT_EQ(fontdaw::makeUserPresetId("shortcuts_azerty", { "shortcuts_azerty" }), "user-shortcuts-azerty");
}

TEST(Shortcuts_FontDawPresetsTests, ValidTitle)
{
    EXPECT_EQ(fontdaw::validTitle("  Mine  "), "Mine");
    EXPECT_EQ(fontdaw::validTitle("   "), "");
    EXPECT_EQ(fontdaw::validTitle(std::string(64, 'x')), std::string(64, 'x'));
    EXPECT_EQ(fontdaw::validTitle(std::string(65, 'x')), "");
}

TEST(Shortcuts_FontDawPresetsTests, ApplyRenames)
{
    ShortcutList list;
    Shortcut a;
    a.command = rcommand::Command("test://old");
    a.sequences = { "X" };
    Shortcut b;
    b.command = rcommand::Command("test://kept");
    list = { a, b };

    fontdaw::applyRenames(list, { { "test://old", "test://new" } });

    ASSERT_EQ(list.size(), 2u);
    EXPECT_EQ(list.at(0).command, rcommand::Command("test://new"));
    EXPECT_EQ(list.at(0).sequences, std::vector<std::string> { "X" });
    EXPECT_EQ(list.at(1).command, rcommand::Command("test://kept"));
}

// Fable check of the series: a malformed user preset file is rejected (and kept), never read with
// the wrong types
TEST(Shortcuts_FontDawPresetsTests, ReadUserPreset_RejectsMalformedFiles)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const std::string head = R"("version": 1, "title": "Mine", "parent": "shortcuts_azerty")";
    const std::vector<std::pair<std::string, std::string> > files = {
        { "no-shortcuts", "{ " + head + " }" },
        { "shortcuts-number", "{ " + head + R"(, "shortcuts": 5 })" },
        { "group-not-object", "{ " + head + R"(, "shortcuts": [ 7 ] })" },
        { "group-shortcuts-not-array", "{ " + head + R"(, "shortcuts": [ { "scope": "TEST", "shortcuts": "x" } ] })" },
        { "entry-without-sequences", "{ " + head + R"(, "shortcuts": [ { "scope": "TEST", "shortcuts": [ { "command": "test://a" } ] } ] })" },
        { "sequence-not-string", "{ " + head + R"(, "shortcuts": [ { "scope": "TEST", "shortcuts": [ { "command": "test://a", "sequences": [ 1 ] } ] } ] })" },
        { "version-string", R"({ "version": "1", "title": "Mine", "parent": "shortcuts_azerty", "shortcuts": [] })" },
        { "title-number", R"({ "version": 1, "title": 3, "parent": "shortcuts_azerty", "shortcuts": [] })" },
        { "root-array", "[]" },
    };

    for (const auto& [name, content] : files) {
        const std::string path = dir.path().toStdString() + "/" + name + ".json";
        {
            std::ofstream out(path);
            out << content;
        }
        EXPECT_FALSE(fontdaw::readUserPreset(io::path_t(path), { "shortcuts_azerty" }).ret) << name;
    }

    // the well-formed one is read
    const std::string ok = dir.path().toStdString() + "/ok.json";
    {
        std::ofstream out(ok);
        out << "{ " + head + R"(, "shortcuts": [ { "scope": "TEST", "shortcuts": [ { "command": "test://a", "sequences": [ "X" ] } ] } ] })";
    }
    RetVal<fontdaw::UserPresetFile> file = fontdaw::readUserPreset(io::path_t(ok), { "shortcuts_azerty" });
    ASSERT_TRUE(file.ret);
    ASSERT_EQ(file.val.shortcuts.size(), 1u);
    EXPECT_EQ(file.val.shortcuts.at(0).sequences, std::vector<std::string> { "X" });
}

// An id is never a path of its own
TEST(Shortcuts_FontDawPresetsTests, UserPresetPath_OnlyForSafeIds)
{
    using ::testing::NiceMock;
    using ::testing::Return;

    NiceMock<ShortcutsConfigurationMock> config;
    ON_CALL(config, userPresetsDirPath()).WillByDefault(Return(io::path_t("/u/shortcuts/presets")));

    EXPECT_EQ(fontdaw::userPresetPath(config, "user-mine"), io::path_t("/u/shortcuts/presets/user-mine.json"));
    EXPECT_TRUE(fontdaw::userPresetPath(config, "user-../../x").empty());
    EXPECT_TRUE(fontdaw::userPresetPath(config, "user-a/b").empty());
    EXPECT_TRUE(fontdaw::userPresetPath(config, "user-a\\b").empty());
    EXPECT_TRUE(fontdaw::userPresetPath(config, "user-a:b").empty());
    EXPECT_TRUE(fontdaw::userPresetPath(config, "shortcuts_azerty").empty());
}

TEST(Shortcuts_FontDawPresetsTests, ValidTitle_RejectsControlCharacters)
{
    EXPECT_EQ(fontdaw::validTitle("Two\nlines"), "");
    EXPECT_EQ(fontdaw::validTitle("Tab\there"), "");
    EXPECT_EQ(fontdaw::validTitle(std::string("Bell\x07")), "");
    EXPECT_EQ(fontdaw::validTitle("Åäö fine"), "Åäö fine");
}

