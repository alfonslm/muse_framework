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
#include <fstream>
#include <map>
#include <set>

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <QTemporaryDir>

#include "modularity/ioc.h"
#include "async/asyncable.h"
#include "global/io/file.h"
#include "global/io/dir.h"
#include "global/serialization/json.h"

#include "internal/commandshortcutsregister.h"
#include "internal/fontdawpresets.h"

#include "mocks/shortcutsconfigurationmock.h"
#include "multiwindows/tests/mocks/multiwindowsprovidermock.h"
#include "rcommand/tests/mocks/commandsregistermock.h"

using ::testing::_;
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::ReturnRef;

using namespace muse;
using namespace muse::shortcuts;
using namespace muse::rcommand;

namespace muse::shortcuts {
class Shortcuts_CommandShortcutsRegisterTests : public ::testing::Test, public async::Asyncable
{
public:
    void SetUp() override
    {
        m_userDir = std::make_unique<QTemporaryDir>();
        ASSERT_TRUE(m_userDir->isValid());
        m_userDirPath = m_userDir->path().toStdString();

        m_configuration = std::make_shared<NiceMock<ShortcutsConfigurationMock> >();
        m_multiWindowsProvider = std::make_shared<NiceMock<mi::MultiWindowsProviderMock> >();
        m_commandsRegister = std::make_shared<NiceMock<CommandsRegisterMock> >();

        modularity::globalIoc()->registerExport<IShortcutsConfiguration>("utests", m_configuration);
        modularity::globalIoc()->registerExport<mi::IMultiWindowsProvider>("utests", m_multiWindowsProvider);
        modularity::globalIoc()->registerExport<ICommandsRegister>("utests", m_commandsRegister);

        //! NOTE Every command is available for shortcuts by default
        // FontDaw (A2): every command is registered under its own name (the shortcuts register
        // drops unknown commands from built-in preset files); a test marks one as unknown with
        // m_unknownCommands
        ON_CALL(*m_commandsRegister, commandInfo(_))
        .WillByDefault(Invoke([this](const Command& command) -> const CommandInfo& {
            if (m_unknownCommands.count(command)) {
                return m_nullInfo;
            }
            CommandInfo& info = m_commandInfos[command];
            info.command = command;
            return info;
        }));

        ON_CALL(*m_configuration, defaultShortcutsName())
        .WillByDefault(Return("shortcuts"));

        ON_CALL(*m_configuration, availableShortcutsPresets())
        .WillByDefault(Return(std::vector<std::string> { "shortcuts_azerty" }));

        ON_CALL(*m_configuration, commandShortcutsAppDataPath(_))
        .WillByDefault(Invoke([](const std::string& name) {
            return io::path_t(std::string(SHORTCUTS_TESTDATA_DIR) + "/" + name + ".json");
        }));

        ON_CALL(*m_configuration, commandShortcutsUserAppDataPath(_))
        .WillByDefault(Invoke([this](const std::string& name) {
            return userPath(name);
        }));

        ON_CALL(*m_configuration, currentShortcutsPresetName())
        .WillByDefault(Invoke([this]() {
            return m_currentPresetName;
        }));

        ON_CALL(*m_configuration, currentShortcutsPresetNameChanged())
        .WillByDefault(Return(m_presetNameChanged));

        ON_CALL(*m_multiWindowsProvider, resourceChanged())
        .WillByDefault(Return(m_resourceChanged));

        // FontDaw (A2): the built-in presets with metadata, and the user presets' folder
        ON_CALL(*m_configuration, builtInPresetInfos())
        .WillByDefault(Invoke([]() {
            PresetInfo azerty;
            azerty.id = "shortcuts_azerty";
            return std::vector<PresetInfo> { azerty };
        }));
        ON_CALL(*m_configuration, userPresetsDirPath())
        .WillByDefault(Invoke([this]() {
            return io::path_t(m_userDirPath + "/presets");
        }));
    }

    void TearDown() override
    {
        m_register.reset();

        modularity::globalIoc()->unregister<IShortcutsConfiguration>("utests");
        modularity::globalIoc()->unregister<mi::IMultiWindowsProvider>("utests");
        modularity::globalIoc()->unregister<ICommandsRegister>("utests");
    }

    void initRegister()
    {
        m_register = std::make_unique<CommandShortcutsRegister>();
        m_register->init();
    }

    io::path_t userPath(const std::string& name) const
    {
        return io::path_t(m_userDirPath + "/" + name + ".json");
    }

    void writeUserFile(const std::string& name, const std::string& content)
    {
        std::ofstream file(m_userDirPath + "/" + name + ".json");
        ASSERT_TRUE(file.is_open());
        file << content;
    }

    void changeCurrentPreset(const std::string& name)
    {
        m_currentPresetName = name;
        m_presetNameChanged.send(name);
    }

    static const Shortcut* findShortcut(const ShortcutList& list, const Command& command)
    {
        auto it = std::find_if(list.begin(), list.end(), [&command](const Shortcut& sc) {
            return sc.command == command;
        });

        return it != list.end() ? &(*it) : nullptr;
    }

    static std::vector<std::string> seqs(std::initializer_list<std::string> list)
    {
        return std::vector<std::string>(list);
    }

protected:
    std::unique_ptr<QTemporaryDir> m_userDir;
    std::string m_userDirPath;

    std::string m_currentPresetName;
    async::Channel<std::string> m_presetNameChanged;
    async::Channel<std::string> m_resourceChanged;

    std::shared_ptr<NiceMock<ShortcutsConfigurationMock> > m_configuration;
    std::shared_ptr<NiceMock<mi::MultiWindowsProviderMock> > m_multiWindowsProvider;
    std::shared_ptr<NiceMock<CommandsRegisterMock> > m_commandsRegister;
    CommandInfo m_commandInfo;
    std::map<Command, CommandInfo> m_commandInfos; // FontDaw (A2)
    std::set<Command> m_unknownCommands; // FontDaw (A2)
    CommandInfo m_nullInfo; // FontDaw (A2)

    std::unique_ptr<CommandShortcutsRegister> m_register;
};

TEST_F(Shortcuts_CommandShortcutsRegisterTests, LoadsDefaultShortcuts)
{
    //! [GIVEN] Default shortcuts name, no preset, no user file

    //! [WHEN] The register is initialized
    initRegister();

    //! [THEN] The base shortcuts are loaded as is
    const ShortcutList& shortcuts = m_register->shortcuts();
    EXPECT_EQ(shortcuts.size(), 4u);

    const Shortcut* a = findShortcut(shortcuts, Command("test://a"));
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(a->sequences, seqs({ "Ctrl+A" }));
    EXPECT_EQ(a->scope, "TEST");

    const Shortcut* d = findShortcut(shortcuts, Command("test://d"));
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->scope, "OTHER");
}

TEST_F(Shortcuts_CommandShortcutsRegisterTests, AppliesPlatformDiff)
{
    //! [GIVEN] The platform default shortcuts file is a diff from the base one
    ON_CALL(*m_configuration, defaultShortcutsName())
    .WillByDefault(Return("shortcuts_mac"));

    //! [WHEN] The register is initialized
    initRegister();

    //! [THEN] The overridden shortcut is changed, the rest are intact
    const ShortcutList& shortcuts = m_register->shortcuts();
    EXPECT_EQ(shortcuts.size(), 4u);

    const Shortcut* b = findShortcut(shortcuts, Command("test://b"));
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(b->sequences, seqs({ "Alt+B" }));

    const Shortcut* a = findShortcut(shortcuts, Command("test://a"));
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(a->sequences, seqs({ "Ctrl+A" }));
}

TEST_F(Shortcuts_CommandShortcutsRegisterTests, AppliesPresetDiff)
{
    //! [GIVEN] The azerty preset is selected
    m_currentPresetName = "shortcuts_azerty";

    //! [WHEN] The register is initialized
    initRegister();

    //! [THEN] The preset diff is applied on top of the base shortcuts
    const ShortcutList& shortcuts = m_register->shortcuts();
    EXPECT_EQ(shortcuts.size(), 4u);

    const Shortcut* c = findShortcut(shortcuts, Command("test://c"));
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(c->sequences, seqs({ "&", "Num+1" }));

    const Shortcut* b = findShortcut(shortcuts, Command("test://b"));
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(b->sequences, seqs({ "Ctrl+B" }));
}

TEST_F(Shortcuts_CommandShortcutsRegisterTests, AppliesPresetDiffOverPlatformDiff)
{
    //! [GIVEN] The platform diff and the preset are set at the same time
    ON_CALL(*m_configuration, defaultShortcutsName())
    .WillByDefault(Return("shortcuts_mac"));

    m_currentPresetName = "shortcuts_azerty";

    //! [WHEN] The register is initialized
    initRegister();

    //! [THEN] Both diffs are applied
    const ShortcutList& shortcuts = m_register->shortcuts();

    const Shortcut* b = findShortcut(shortcuts, Command("test://b"));
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(b->sequences, seqs({ "Alt+B" }));

    const Shortcut* c = findShortcut(shortcuts, Command("test://c"));
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(c->sequences, seqs({ "&", "Num+1" }));
}

TEST_F(Shortcuts_CommandShortcutsRegisterTests, AppliesUserDiff)
{
    //! [GIVEN] A user diff file for the default shortcuts
    writeUserFile("shortcuts", R"([ { "scope": "TEST", "shortcuts": [ { "command": "test://a", "sequences": ["X"] } ] } ])");

    //! [WHEN] The register is initialized
    initRegister();

    //! [THEN] The user diff is applied, the rest is default
    const ShortcutList& shortcuts = m_register->shortcuts();
    EXPECT_EQ(shortcuts.size(), 4u);

    const Shortcut* a = findShortcut(shortcuts, Command("test://a"));
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(a->sequences, seqs({ "X" }));

    const Shortcut* b = findShortcut(shortcuts, Command("test://b"));
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(b->sequences, seqs({ "Ctrl+B" }));
}

TEST_F(Shortcuts_CommandShortcutsRegisterTests, UserDiffIsPerPreset)
{
    //! [GIVEN] A user diff file for the default shortcuts, but the azerty preset is selected
    writeUserFile("shortcuts", R"([ { "scope": "TEST", "shortcuts": [ { "command": "test://a", "sequences": ["X"] } ] } ])");
    m_currentPresetName = "shortcuts_azerty";

    //! [WHEN] The register is initialized
    initRegister();

    //! [THEN] The default user diff is not applied
    const Shortcut* a = findShortcut(m_register->shortcuts(), Command("test://a"));
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(a->sequences, seqs({ "Ctrl+A" }));
}

TEST_F(Shortcuts_CommandShortcutsRegisterTests, SetShortcutsWritesOnlyDiff)
{
    //! [GIVEN] The register with default shortcuts
    initRegister();

    //! [WHEN] One shortcut is modified
    ShortcutList modified = m_register->shortcuts();
    for (Shortcut& sc : modified) {
        if (sc.command == Command("test://a")) {
            sc.sequences = seqs({ "Y" });
        }
    }

    ASSERT_TRUE(m_register->updateShortcuts(modified));

    //! [THEN] The user file contains only the modified shortcut
    ByteArray data;
    ASSERT_TRUE(io::File::readFile(userPath("shortcuts"), data));

    JsonArray root = JsonDocument::fromJson(data).rootArray();
    ASSERT_EQ(root.size(), 1u);

    JsonObject scopeObj = root.at(0).toObject();
    EXPECT_EQ(scopeObj.value("scope").toStdString(), "TEST");

    JsonArray scopeArr = scopeObj.value("shortcuts").toArray();
    ASSERT_EQ(scopeArr.size(), 1u);
    EXPECT_EQ(scopeArr.at(0).toObject().value("command").toString().toStdString(), "test://a");

    //! [THEN] After reload the modified shortcut is restored, the rest are default
    m_register->reload();

    const ShortcutList& shortcuts = m_register->shortcuts();
    EXPECT_EQ(shortcuts.size(), 4u);

    const Shortcut* a = findShortcut(shortcuts, Command("test://a"));
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(a->sequences, seqs({ "Y" }));

    const Shortcut* b = findShortcut(shortcuts, Command("test://b"));
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(b->sequences, seqs({ "Ctrl+B" }));
}

TEST_F(Shortcuts_CommandShortcutsRegisterTests, RevertingAllChangesRemovesUserFile)
{
    //! [GIVEN] The register with a modified shortcut
    initRegister();

    ShortcutList defaultShortcuts = m_register->shortcuts();

    ShortcutList modified = defaultShortcuts;
    for (Shortcut& sc : modified) {
        if (sc.command == Command("test://a")) {
            sc.sequences = seqs({ "Y" });
        }
    }

    ASSERT_TRUE(m_register->updateShortcuts(modified));
    ASSERT_TRUE(io::File::exists(userPath("shortcuts")));

    //! [WHEN] The shortcut is reverted back to the default value
    ASSERT_TRUE(m_register->updateShortcuts(defaultShortcuts));

    //! [THEN] The user file is removed
    EXPECT_FALSE(io::File::exists(userPath("shortcuts")));
    EXPECT_EQ(m_register->shortcuts().size(), 4u);
}

TEST_F(Shortcuts_CommandShortcutsRegisterTests, ClearedShortcutIsSaved)
{
    //! [GIVEN] The register with default shortcuts
    initRegister();

    //! [WHEN] One shortcut is cleared
    ShortcutList modified = m_register->shortcuts();
    for (Shortcut& sc : modified) {
        if (sc.command == Command("test://a")) {
            sc.sequences = {};
        }
    }

    ASSERT_TRUE(m_register->updateShortcuts(modified));

    //! [THEN] The explicit clear survives reload
    m_register->reload();

    const Shortcut* a = findShortcut(m_register->shortcuts(), Command("test://a"));
    ASSERT_NE(a, nullptr);
    EXPECT_TRUE(a->sequences.empty());
}

TEST_F(Shortcuts_CommandShortcutsRegisterTests, ResetShortcutsRemovesUserFile)
{
    //! [GIVEN] The register with a modified shortcut
    initRegister();

    ShortcutList modified = m_register->shortcuts();
    modified.front().sequences = seqs({ "Y" });
    ASSERT_TRUE(m_register->updateShortcuts(modified));
    ASSERT_TRUE(io::File::exists(userPath("shortcuts")));

    //! [WHEN] The shortcuts are reset
    m_register->resetShortcuts();

    //! [THEN] The user file is removed and the defaults are restored
    EXPECT_FALSE(io::File::exists(userPath("shortcuts")));

    const Shortcut* a = findShortcut(m_register->shortcuts(), Command("test://a"));
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(a->sequences, seqs({ "Ctrl+A" }));
}

TEST_F(Shortcuts_CommandShortcutsRegisterTests, PresetChangeReloadsShortcuts)
{
    //! [GIVEN] The register with default shortcuts
    initRegister();

    bool changedNotified = false;
    m_register->shortcutsChanged().onNotify(this, [&changedNotified]() {
        changedNotified = true;
    });

    //! [WHEN] The current preset is changed
    changeCurrentPreset("shortcuts_azerty");

    //! [THEN] The register is reloaded with the preset diff applied
    EXPECT_TRUE(changedNotified);

    const Shortcut* c = findShortcut(m_register->shortcuts(), Command("test://c"));
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(c->sequences, seqs({ "&", "Num+1" }));

    //! [WHEN] The preset is changed back
    changeCurrentPreset("");

    //! [THEN] The defaults are restored
    c = findShortcut(m_register->shortcuts(), Command("test://c"));
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(c->sequences, seqs({ "1", "Num+1" }));
}

TEST_F(Shortcuts_CommandShortcutsRegisterTests, UserDiffIsWrittenForActivePreset)
{
    //! [GIVEN] The register with the azerty preset selected
    m_currentPresetName = "shortcuts_azerty";
    initRegister();

    //! [WHEN] One shortcut is modified
    ShortcutList modified = m_register->shortcuts();
    for (Shortcut& sc : modified) {
        if (sc.command == Command("test://b")) {
            sc.sequences = seqs({ "Z" });
        }
    }

    ASSERT_TRUE(m_register->updateShortcuts(modified));

    //! [THEN] The user diff is written for the preset, not for the default shortcuts
    EXPECT_TRUE(io::File::exists(userPath("shortcuts_azerty")));
    EXPECT_FALSE(io::File::exists(userPath("shortcuts")));

    //! [THEN] After switching to the default shortcuts and back, the modification is restored
    changeCurrentPreset("");
    const Shortcut* b = findShortcut(m_register->shortcuts(), Command("test://b"));
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(b->sequences, seqs({ "Ctrl+B" }));

    changeCurrentPreset("shortcuts_azerty");
    b = findShortcut(m_register->shortcuts(), Command("test://b"));
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(b->sequences, seqs({ "Z" }));
}

TEST_F(Shortcuts_CommandShortcutsRegisterTests, IsPresetEditedReflectsUserFile)
{
    //! [GIVEN] The register with default shortcuts, no user file
    initRegister();
    EXPECT_FALSE(m_register->isPresetEdited(""));

    //! [WHEN] One shortcut is modified
    ShortcutList modified = m_register->shortcuts();
    modified.front().sequences = seqs({ "Y" });
    ASSERT_TRUE(m_register->updateShortcuts(modified));

    //! [THEN] The active set is edited
    EXPECT_TRUE(m_register->isPresetEdited(""));

    //! [WHEN] The shortcuts are reset
    m_register->resetShortcuts();

    //! [THEN] The active set is not edited anymore
    EXPECT_FALSE(m_register->isPresetEdited(""));
}

TEST_F(Shortcuts_CommandShortcutsRegisterTests, BuiltinPresetsCannotBeDeleted)
{
    //! [GIVEN] The register and an edited builtin preset
    writeUserFile("shortcuts_azerty", R"([ { "scope": "TEST", "shortcuts": [ { "command": "test://a", "sequences": ["X"] } ] } ])");
    initRegister();

    //! [THEN] Neither the default set nor builtin presets can be deleted
    EXPECT_FALSE(m_register->canDeletePreset(""));
    EXPECT_FALSE(m_register->canDeletePreset("shortcuts_azerty"));

    //! [WHEN] Trying to delete a builtin preset anyway
    m_register->deletePreset("shortcuts_azerty");

    //! [THEN] Its user file is intact
    EXPECT_TRUE(io::File::exists(userPath("shortcuts_azerty")));
}

TEST_F(Shortcuts_CommandShortcutsRegisterTests, DeletePresetRemovesUserFileAndFallsBackToDefault)
{
    //! [GIVEN] A custom preset (user file without a builtin twin) is selected
    writeUserFile("my_preset", R"([ { "scope": "TEST", "shortcuts": [ { "command": "test://a", "sequences": ["X"] } ] } ])");
    m_currentPresetName = "my_preset";
    initRegister();

    EXPECT_TRUE(m_register->canDeletePreset("my_preset"));

    //! [WHEN] The preset is deleted
    EXPECT_CALL(*m_configuration, setCurrentShortcutsPresetName(std::string()));
    m_register->deletePreset("my_preset");

    //! [THEN] The user file is removed and the current preset is switched to default
    EXPECT_FALSE(io::File::exists(userPath("my_preset")));
}

// FontDaw (shortcut work order v2, A2, step 2): the presets list with metadata
TEST_F(Shortcuts_CommandShortcutsRegisterTests, PresetInfos_BaseFirstThenBuiltInsInOrder)
{
    PresetInfo b;
    b.id = "shortcuts_b";
    b.group = "musescore";
    PresetInfo a;
    a.id = "shortcuts_a";
    ON_CALL(*m_configuration, isBaseHidden()).WillByDefault(Return(false));
    ON_CALL(*m_configuration, builtInPresetInfos()).WillByDefault(Return(std::vector<PresetInfo> { b, a }));
    initRegister();

    const std::vector<PresetInfo> infos = m_register->presetInfos();

    ASSERT_EQ(infos.size(), 3u);
    EXPECT_EQ(infos.at(0).id, "");
    EXPECT_EQ(infos.at(1).id, "shortcuts_b");
    EXPECT_EQ(infos.at(1).group, "musescore");
    EXPECT_EQ(infos.at(2).id, "shortcuts_a");
}

TEST_F(Shortcuts_CommandShortcutsRegisterTests, PresetInfos_BaseHidden)
{
    PresetInfo a;
    a.id = "shortcuts_a";
    ON_CALL(*m_configuration, isBaseHidden()).WillByDefault(Return(true));
    ON_CALL(*m_configuration, builtInPresetInfos()).WillByDefault(Return(std::vector<PresetInfo> { a }));
    initRegister();

    const std::vector<PresetInfo> infos = m_register->presetInfos();

    ASSERT_EQ(infos.size(), 1u);
    EXPECT_EQ(infos.at(0).id, "shortcuts_a");
}

// availablePresets() is unchanged: still the configuration's plain ids, without the base
TEST_F(Shortcuts_CommandShortcutsRegisterTests, AvailablePresets_Unchanged)
{
    initRegister();
    EXPECT_EQ(m_register->availablePresets(), std::vector<std::string> { "shortcuts_azerty" });
}

// FontDaw (shortcut work order v2, A2, step 3): unknown commands
TEST_F(Shortcuts_CommandShortcutsRegisterTests, BuiltInPresetDropsUnknownCommand)
{
    //! [GIVEN] The azerty preset rebinds test://c, which is not registered
    m_unknownCommands.insert(Command("test://c"));
    m_currentPresetName = "shortcuts_azerty";

    //! [WHEN] The register is initialized
    initRegister();

    //! [THEN] The preset's entry is ignored: test://c keeps the base's keys (the base file is upstream's
    //! and is not filtered)
    const Shortcut* c = findShortcut(m_register->shortcuts(), Command("test://c"));
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(c->sequences, seqs({ "1", "Num+1" }));
}

TEST_F(Shortcuts_CommandShortcutsRegisterTests, UserFileKeepsUnknownCommand)
{
    //! [GIVEN] The user file binds a command that is not registered (an extension not loaded yet)
    m_unknownCommands.insert(Command("test://later"));
    writeUserFile("shortcuts", R"([ { "scope": "TEST", "shortcuts": [ { "command": "test://later", "sequences": ["L"] } ] } ])");

    //! [WHEN] The register is initialized and the shortcuts are written back
    initRegister();
    const Shortcut* later = findShortcut(m_register->shortcuts(), Command("test://later"));
    ASSERT_NE(later, nullptr);
    EXPECT_EQ(later->sequences, seqs({ "L" }));

    ShortcutList all = m_register->shortcuts();
    m_register->updateShortcuts(all);

    //! [THEN] The key is still in the user file
    m_register->reload();
    later = findShortcut(m_register->shortcuts(), Command("test://later"));
    ASSERT_NE(later, nullptr);
    EXPECT_EQ(later->sequences, seqs({ "L" }));
}

// FontDaw (shortcut work order v2, A2, step 4a): a built-in preset's scope wins
TEST_F(Shortcuts_CommandShortcutsRegisterTests, PresetScopeWins_AndClashesInTheNewScope)
{
    for (const char* preset : { "fontdaw_scopemove", "fontdaw_scopemove_reversed" }) {
        //! [GIVEN] A preset moves test://a from TEST to OTHER and gives it D, which test://d has in OTHER
        m_currentPresetName = preset;

        //! [WHEN] The register is initialized
        initRegister();

        //! [THEN] test://a is in OTHER with D, and test://d no longer has D, whatever the order of entries
        const Shortcut* a = findShortcut(m_register->shortcuts(), Command("test://a"));
        ASSERT_NE(a, nullptr) << preset;
        EXPECT_EQ(a->scope, "OTHER") << preset;
        EXPECT_EQ(a->sequences, seqs({ "D" })) << preset;

        const Shortcut* d = findShortcut(m_register->shortcuts(), Command("test://d"));
        ASSERT_NE(d, nullptr) << preset;
        EXPECT_TRUE(d->sequences.empty()) << preset;
    }
}

TEST_F(Shortcuts_CommandShortcutsRegisterTests, PresetEntryWithoutScopeInherits)
{
    //! [GIVEN] A preset entry without a scope
    m_currentPresetName = "fontdaw_noscope";

    //! [WHEN] The register is initialized
    initRegister();

    //! [THEN] The command keeps the base's scope and gets the preset's keys
    const Shortcut* a = findShortcut(m_register->shortcuts(), Command("test://a"));
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(a->scope, "TEST");
    EXPECT_EQ(a->sequences, seqs({ "Ctrl+Shift+A" }));
}

// A user file keeps "scope from the defaults", which are then the preset's
TEST_F(Shortcuts_CommandShortcutsRegisterTests, UserEditKeepsThePresetScope)
{
    m_currentPresetName = "fontdaw_scopemove";
    writeUserFile("fontdaw_scopemove", R"([ { "scope": "TEST", "shortcuts": [ { "command": "test://a", "sequences": ["Ctrl+Alt+A"] } ] } ])");
    initRegister();

    const Shortcut* a = findShortcut(m_register->shortcuts(), Command("test://a"));
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(a->scope, "OTHER");
    EXPECT_EQ(a->sequences, seqs({ "Ctrl+Alt+A" }));
}

// FontDaw (shortcut work order v2, A2, step 4b): complete built-in presets
namespace {
std::vector<PresetInfo> completePreset(const std::string& id)
{
    PresetInfo info;
    info.id = id;
    info.complete = true;
    return { info };
}
}

TEST_F(Shortcuts_CommandShortcutsRegisterTests, CompletePreset_DropsBaseKeysKeepsScopes)
{
    //! [GIVEN] A complete preset that lists only test://b
    ON_CALL(*m_configuration, builtInPresetInfos()).WillByDefault(Return(completePreset("fontdaw_complete")));
    m_currentPresetName = "fontdaw_complete";

    //! [WHEN] The register is initialized
    initRegister();

    //! [THEN] Only test://b has a key; the other commands keep their rows and the base's scopes
    const ShortcutList& shortcuts = m_register->shortcuts();
    const Shortcut* b = findShortcut(shortcuts, Command("test://b"));
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(b->sequences, seqs({ "X" }));

    for (const char* name : { "test://a", "test://c" }) {
        const Shortcut* sc = findShortcut(shortcuts, Command(name));
        ASSERT_NE(sc, nullptr) << name;
        EXPECT_TRUE(sc->sequences.empty()) << name;
        EXPECT_EQ(sc->scope, "TEST") << name;
    }
    const Shortcut* d = findShortcut(shortcuts, Command("test://d"));
    ASSERT_NE(d, nullptr);
    EXPECT_TRUE(d->sequences.empty());
    EXPECT_EQ(d->scope, "OTHER");
}

// On a Mac the platform diff is applied first; a complete preset replaces its keys too, so the
// app ships a macOS file of its own for a complete preset
TEST_F(Shortcuts_CommandShortcutsRegisterTests, CompletePreset_ReplacesThePlatformDiff)
{
    ON_CALL(*m_configuration, defaultShortcutsName()).WillByDefault(Return("shortcuts_mac"));
    ON_CALL(*m_configuration, builtInPresetInfos()).WillByDefault(Return(completePreset("fontdaw_complete")));
    m_currentPresetName = "fontdaw_complete";
    initRegister();

    const Shortcut* b = findShortcut(m_register->shortcuts(), Command("test://b"));
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(b->sequences, seqs({ "X" })); // not the platform diff's Alt+B
}

TEST_F(Shortcuts_CommandShortcutsRegisterTests, CompletePreset_UserEditsOnTop)
{
    ON_CALL(*m_configuration, builtInPresetInfos()).WillByDefault(Return(completePreset("fontdaw_complete")));
    m_currentPresetName = "fontdaw_complete";
    writeUserFile("fontdaw_complete", R"([ { "scope": "TEST", "shortcuts": [ { "command": "test://a", "sequences": ["Ctrl+Q"] } ] } ])");
    initRegister();

    const Shortcut* a = findShortcut(m_register->shortcuts(), Command("test://a"));
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(a->sequences, seqs({ "Ctrl+Q" }));
    const Shortcut* b = findShortcut(m_register->shortcuts(), Command("test://b"));
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(b->sequences, seqs({ "X" }));
}

// FontDaw (shortcut work order v2, A2, step 5): user presets
namespace {
const char* MINE = R"({ "version": 1, "title": "Mine", "parent": "shortcuts_azerty",
    "shortcuts": [ { "scope": "TEST", "shortcuts": [ { "command": "test://a", "sequences": ["Ctrl+M"] } ] } ] })";
}

class Shortcuts_FontDawUserPresetTests : public Shortcuts_CommandShortcutsRegisterTests
{
protected:
    void writeUserPreset(const std::string& id, const std::string& content)
    {
        io::Dir::mkpath(m_userDirPath + "/presets");
        std::ofstream file(m_userDirPath + "/presets/" + id + ".json");
        ASSERT_TRUE(file.is_open());
        file << content;
    }

    io::path_t presetPath(const std::string& id) const
    {
        return io::path_t(m_userDirPath + "/presets/" + id + ".json");
    }

    fontdaw::UserPresetFile readPreset(const std::string& id) const
    {
        RetVal<fontdaw::UserPresetFile> file = fontdaw::readUserPreset(presetPath(id), { "shortcuts_azerty" });
        EXPECT_TRUE(file.ret) << id;
        return file.val;
    }

    void followPresetChanges()
    {
        ON_CALL(*m_configuration, setCurrentShortcutsPresetName(_))
        .WillByDefault(Invoke([this](const std::string& name) { changeCurrentPreset(name); }));
    }
};

TEST_F(Shortcuts_FontDawUserPresetTests, ResolvesOnItsParent)
{
    writeUserPreset("user-mine", MINE);
    m_currentPresetName = "user-mine";
    initRegister();

    const Shortcut* a = findShortcut(m_register->shortcuts(), Command("test://a"));
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(a->sequences, seqs({ "Ctrl+M" }));
    const Shortcut* c = findShortcut(m_register->shortcuts(), Command("test://c"));
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(c->sequences, seqs({ "&", "Num+1" })); // the parent's (azerty's) key
}

TEST_F(Shortcuts_FontDawUserPresetTests, SaveAs_DiffOnParent_MadeCurrent_Listed)
{
    followPresetChanges();
    m_currentPresetName = "shortcuts_azerty";
    initRegister();

    ShortcutList all = m_register->shortcuts();
    for (Shortcut& sc : all) {
        if (sc.command == Command("test://b")) {
            sc.sequences = { "Ctrl+Y" };
        }
    }
    m_register->updateShortcuts(all);

    int presetsChanged = 0;
    m_register->presetsChanged().onNotify(this, [&presetsChanged]() { ++presetsChanged; });

    RetVal<std::string> id = m_register->savePresetAs("  My keys!  ");
    ASSERT_TRUE(id.ret);
    EXPECT_EQ(id.val, "user-my-keys");
    EXPECT_EQ(m_currentPresetName, "user-my-keys");
    EXPECT_GE(presetsChanged, 1);

    const fontdaw::UserPresetFile file = readPreset("user-my-keys");
    EXPECT_EQ(file.title, "My keys!");
    EXPECT_EQ(file.parent, "shortcuts_azerty");
    ASSERT_EQ(file.shortcuts.size(), 1u);
    EXPECT_EQ(file.shortcuts.at(0).command, Command("test://b"));

    const Shortcut* b = findShortcut(m_register->shortcuts(), Command("test://b"));
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(b->sequences, seqs({ "Ctrl+Y" }));

    bool listed = false;
    for (const PresetInfo& info : m_register->presetInfos()) {
        if (info.id == "user-my-keys") {
            listed = true;
            EXPECT_TRUE(info.isUser);
            EXPECT_EQ(info.parent, "shortcuts_azerty");
        }
    }
    EXPECT_TRUE(listed);
}

TEST_F(Shortcuts_FontDawUserPresetTests, SaveAs_IdNeverClashes_AndBaseIsNoParent)
{
    followPresetChanges();
    writeUserPreset("user-mine", MINE);
    m_currentPresetName = "user-mine";
    initRegister();

    // saving "Mine" again from a user preset: a new id, the same parent
    RetVal<std::string> id = m_register->savePresetAs("Mine");
    ASSERT_TRUE(id.ret);
    EXPECT_EQ(id.val, "user-mine-2");
    EXPECT_EQ(readPreset("user-mine-2").parent, "shortcuts_azerty");

    // from the base there is no built-in parent: refused
    changeCurrentPreset("");
    EXPECT_FALSE(m_register->savePresetAs("From base").ret);

    // an empty or too long title is refused
    changeCurrentPreset("shortcuts_azerty");
    EXPECT_FALSE(m_register->savePresetAs("   ").ret);
    EXPECT_FALSE(m_register->savePresetAs(std::string(65, 'x')).ret);
}

TEST_F(Shortcuts_FontDawUserPresetTests, SaveAs_LeavesOutAdditionalShortcuts)
{
    followPresetChanges();
    m_currentPresetName = "shortcuts_azerty";
    initRegister();

    Shortcut extra;
    extra.scope = "TEST";
    extra.command = Command("test://extra");
    extra.sequences = { "E" };
    m_register->setAdditionalShortcuts("context", { extra });

    RetVal<std::string> id = m_register->savePresetAs("With extra");
    ASSERT_TRUE(id.ret);
    for (const Shortcut& sc : readPreset(id.val).shortcuts) {
        EXPECT_NE(sc.command, Command("test://extra"));
    }
}

// Re-check of #272, addition 1: editing a user preset back to its parent's keys keeps the preset
TEST_F(Shortcuts_FontDawUserPresetTests, EmptiedPresetIsKept)
{
    writeUserPreset("user-mine", MINE);
    m_currentPresetName = "user-mine";
    initRegister();

    ShortcutList all = m_register->shortcuts();
    for (Shortcut& sc : all) {
        if (sc.command == Command("test://a")) {
            sc.sequences = { "Ctrl+A" }; // the parent's key
        }
    }
    m_register->updateShortcuts(all);

    EXPECT_TRUE(io::File::exists(presetPath("user-mine")));
    EXPECT_TRUE(readPreset("user-mine").shortcuts.empty());
}

// Re-check of #272, addition 2: reset of a user preset = back to its parent
TEST_F(Shortcuts_FontDawUserPresetTests, ResetGoesBackToTheParent)
{
    writeUserPreset("user-mine", MINE);
    m_currentPresetName = "user-mine";
    initRegister();

    m_register->resetShortcuts();

    EXPECT_TRUE(io::File::exists(presetPath("user-mine")));
    const fontdaw::UserPresetFile file = readPreset("user-mine");
    EXPECT_EQ(file.title, "Mine");
    EXPECT_TRUE(file.shortcuts.empty());
    const Shortcut* a = findShortcut(m_register->shortcuts(), Command("test://a"));
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(a->sequences, seqs({ "Ctrl+A" }));
}

// Re-check of #272, addition 3: import into a user preset replaces only its keys
TEST_F(Shortcuts_FontDawUserPresetTests, ImportKeepsTitleAndParent)
{
    writeUserPreset("user-mine", MINE);
    m_currentPresetName = "user-mine";
    initRegister();

    ASSERT_TRUE(m_register->importFromFile(io::path_t(std::string(SHORTCUTS_TESTDATA_DIR) + "/shortcuts_mac.json")));

    const fontdaw::UserPresetFile file = readPreset("user-mine");
    EXPECT_EQ(file.title, "Mine");
    EXPECT_EQ(file.parent, "shortcuts_azerty");
    ASSERT_EQ(file.shortcuts.size(), 1u);
    EXPECT_EQ(file.shortcuts.at(0).command, Command("test://b"));
}

TEST_F(Shortcuts_FontDawUserPresetTests, DeleteFallsBackToTheParent)
{
    writeUserPreset("user-mine", MINE);
    m_currentPresetName = "user-mine";
    initRegister();

    EXPECT_TRUE(m_register->canDeletePreset("user-mine"));
    int presetsChanged = 0;
    m_register->presetsChanged().onNotify(this, [&presetsChanged]() { ++presetsChanged; });

    EXPECT_CALL(*m_configuration, setCurrentShortcutsPresetName(std::string("shortcuts_azerty")));
    m_register->deletePreset("user-mine");

    EXPECT_FALSE(io::File::exists(presetPath("user-mine")));
    EXPECT_EQ(presetsChanged, 1);
}

// An unusable file falls back to the base and is kept, never overwritten
TEST_F(Shortcuts_FontDawUserPresetTests, UnusableFilesFallBackToTheBase)
{
    const std::vector<std::pair<std::string, std::string> > bad = {
        { "user-newer", R"({ "version": 2, "title": "Newer", "parent": "shortcuts_azerty", "shortcuts": [] })" },
        { "user-noparent", R"({ "version": 1, "title": "No parent", "parent": "shortcuts_gone", "shortcuts": [] })" },
        { "user-notitle", R"({ "version": 1, "title": "  ", "parent": "shortcuts_azerty", "shortcuts": [] })" },
        { "user-notjson", "not json" },
    };

    for (const auto& [id, content] : bad) {
        writeUserPreset(id, content);
        m_currentPresetName = id;
        initRegister();

        const Shortcut* c = findShortcut(m_register->shortcuts(), Command("test://c"));
        ASSERT_NE(c, nullptr) << id;
        EXPECT_EQ(c->sequences, seqs({ "1", "Num+1" })) << id; // the base's key, not azerty's

        ShortcutList all = m_register->shortcuts();
        m_register->updateShortcuts(all);

        ByteArray data;
        ASSERT_TRUE(io::File::readFile(presetPath(id), data)) << id;
        EXPECT_EQ(std::string(data.constChar(), data.size()), content) << id; // unchanged

        for (const PresetInfo& info : m_register->presetInfos()) {
            EXPECT_NE(info.id, id) << "an unusable preset is not listed";
        }
    }
}

// Re-check of #272: another window changing the shortcuts files refreshes every drop-down
TEST_F(Shortcuts_FontDawUserPresetTests, PresetsChangedOnResourceChanged)
{
    initRegister();
    int presetsChanged = 0;
    m_register->presetsChanged().onNotify(this, [&presetsChanged]() { ++presetsChanged; });

    m_resourceChanged.send("CommandShortcuts");
    EXPECT_EQ(presetsChanged, 1);
}

// FontDaw (shortcut work order v2, A2, step 6): a command renamed upstream keeps the user's key
TEST_F(Shortcuts_CommandShortcutsRegisterTests, RenamesAppliedInMemoryOnRead)
{
    //! [GIVEN] The user's file names test://old, which upstream renamed to test://a
    ON_CALL(*m_configuration, commandRenames())
    .WillByDefault(Return(std::map<std::string, std::string> { { "test://old", "test://a" } }));
    const std::string content = R"([ { "scope": "TEST", "shortcuts": [ { "command": "test://old", "sequences": ["Ctrl+O"] } ] } ])";
    writeUserFile("shortcuts", content);

    //! [WHEN] The register is initialized
    initRegister();

    //! [THEN] test://a has the user's key, and the file is not rewritten
    const Shortcut* a = findShortcut(m_register->shortcuts(), Command("test://a"));
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(a->sequences, seqs({ "Ctrl+O" }));
    EXPECT_EQ(findShortcut(m_register->shortcuts(), Command("test://old")), nullptr);

    ByteArray data;
    ASSERT_TRUE(io::File::readFile(userPath("shortcuts"), data));
    EXPECT_EQ(std::string(data.constChar(), data.size()), content);
}
}
