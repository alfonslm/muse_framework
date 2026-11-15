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
#include <gtest/gtest.h>

#include <iterator>
#include <set>
#include <string>
#include <utility>

#include "types/sharedhashmap.h"

using namespace muse;

class Global_Types_SharedHashMapTests : public ::testing::Test
{
public:
};

TEST_F(Global_Types_SharedHashMapTests, ErasePos_WhileShared)
{
    // [GIVEN]
    SharedHashMap<int, std::string> original { { 1, "a" }, { 2, "b" }, { 3, "c" } };
    SharedHashMap<int, std::string> copy = original;

    // [WHEN]
    auto it = std::as_const(copy).find(2);
    copy.erase(it);

    // [THEN]
    EXPECT_EQ(copy.size(), 2);
    EXPECT_FALSE(copy.contains(2));
    EXPECT_EQ(original.size(), 3);
    EXPECT_TRUE(original.contains(2));
}

TEST_F(Global_Types_SharedHashMapTests, EraseRange_Full_WhileShared)
{
    // [GIVEN]
    SharedHashMap<int, std::string> original { { 1, "a" }, { 2, "b" } };
    SharedHashMap<int, std::string> copy = original;

    // [WHEN]
    copy.erase(copy.cbegin(), copy.cend());

    // [THEN]
    EXPECT_TRUE(copy.empty());
    EXPECT_EQ(original.size(), 2);
}

TEST_F(Global_Types_SharedHashMapTests, EraseRange_Partial_WhileShared)
{
    // [GIVEN]
    SharedHashMap<int, std::string> original { { 1, "a" }, { 2, "b" }, { 3, "c" }, { 4, "d" } };
    SharedHashMap<int, std::string> copy = original;

    // [WHEN]
    auto first = std::as_const(copy).find(2);
    auto last = std::next(first);
    copy.erase(first, last);

    // [THEN]
    EXPECT_EQ(copy.size(), 3);
    EXPECT_FALSE(copy.contains(2));
    EXPECT_EQ(original.size(), 4);
    EXPECT_TRUE(original.contains(2));
}

TEST_F(Global_Types_SharedHashMapTests, EraseRange_Partial_WhileShared_ErasesExactlyTheRange)
{
    // [GIVEN] A map large enough that a copy of the underlying std::unordered_map
    //         may iterate in a different order (it does with libc++)
    SharedHashMap<int, std::string> original;
    for (int i = 0; i < 100; ++i) {
        original.insert({ i, std::to_string(i) });
    }
    SharedHashMap<int, std::string> copy = original;

    // [GIVEN] A range in the middle, and the keys it covers
    auto first = std::next(std::as_const(copy).begin(), 10);
    auto last = std::next(first, 50);

    std::set<int> keysInRange;
    for (auto it = first; it != last; ++it) {
        keysInRange.insert(it->first);
    }
    ASSERT_EQ(keysInRange.size(), 50);

    // [WHEN]
    copy.erase(first, last);

    // [THEN] Exactly the keys of the range are gone
    EXPECT_EQ(copy.size(), 50);
    for (int i = 0; i < 100; ++i) {
        EXPECT_EQ(copy.contains(i), keysInRange.count(i) == 0) << "key " << i;
    }

    // [THEN] The original is untouched
    EXPECT_EQ(original.size(), 100);
}

TEST_F(Global_Types_SharedHashMapTests, EraseRange_ToEnd_WhileShared)
{
    // [GIVEN]
    SharedHashMap<int, std::string> original;
    for (int i = 0; i < 100; ++i) {
        original.insert({ i, std::to_string(i) });
    }
    SharedHashMap<int, std::string> copy = original;

    auto first = std::next(std::as_const(copy).begin(), 30);

    std::set<int> keysInRange;
    for (auto it = first; it != std::as_const(copy).end(); ++it) {
        keysInRange.insert(it->first);
    }

    // [WHEN]
    copy.erase(first, copy.cend());

    // [THEN]
    EXPECT_EQ(copy.size(), 30);
    for (int i = 0; i < 100; ++i) {
        EXPECT_EQ(copy.contains(i), keysInRange.count(i) == 0) << "key " << i;
    }
    EXPECT_EQ(original.size(), 100);
}

TEST_F(Global_Types_SharedHashMapTests, EraseRange_Empty_WhileShared_IsNoOp)
{
    // [GIVEN]
    SharedHashMap<int, std::string> original { { 1, "a" }, { 2, "b" } };
    SharedHashMap<int, std::string> copy = original;

    // [WHEN]
    auto it = std::as_const(copy).find(1);
    copy.erase(it, it);

    // [THEN]
    EXPECT_EQ(copy.size(), 2);
    EXPECT_EQ(original.size(), 2);
}

TEST_F(Global_Types_SharedHashMapTests, InsertHint_AtEnd_WhileShared)
{
    // [GIVEN]
    SharedHashMap<int, std::string> original { { 1, "a" } };
    SharedHashMap<int, std::string> copy = original;

    // [WHEN]
    copy.insert(copy.cend(), { 2, "b" });

    // [THEN]
    EXPECT_EQ(copy.size(), 2);
    EXPECT_TRUE(copy.contains(2));
    EXPECT_EQ(original.size(), 1);
    EXPECT_FALSE(original.contains(2));
}

TEST_F(Global_Types_SharedHashMapTests, InsertHint_AtExistingElement_WhileShared)
{
    // [GIVEN]
    SharedHashMap<int, std::string> original { { 1, "a" } };
    SharedHashMap<int, std::string> copy = original;

    // [WHEN]
    copy.insert(std::as_const(copy).find(1), { 2, "b" });

    // [THEN]
    EXPECT_EQ(copy.size(), 2);
    EXPECT_TRUE(copy.contains(2));
    EXPECT_EQ(original.size(), 1);
    EXPECT_FALSE(original.contains(2));
}

TEST_F(Global_Types_SharedHashMapTests, InsertHint_IntoEmptyMap_WhileShared)
{
    // [GIVEN]
    SharedHashMap<int, std::string> original;
    SharedHashMap<int, std::string> copy = original;

    // [WHEN] cbegin() == cend() on an empty map
    ASSERT_EQ(copy.cbegin(), copy.cend());
    copy.insert(copy.cend(), { 1, "a" });

    // [THEN]
    EXPECT_EQ(copy.size(), 1);
    EXPECT_TRUE(original.empty());
}

TEST_F(Global_Types_SharedHashMapTests, EmplaceHint_WhileShared)
{
    // [GIVEN]
    SharedHashMap<int, std::string> original { { 1, "a" } };
    SharedHashMap<int, std::string> copy = original;

    // [WHEN]
    copy.emplace_hint(copy.cend(), 2, "b");

    // [THEN]
    EXPECT_EQ(copy.size(), 2);
    EXPECT_TRUE(copy.contains(2));
    EXPECT_EQ(original.size(), 1);
}

TEST_F(Global_Types_SharedHashMapTests, TryEmplaceHint_NewKey_WhileShared)
{
    // [GIVEN]
    SharedHashMap<int, std::string> original { { 1, "a" } };
    SharedHashMap<int, std::string> copy = original;

    // [WHEN]
    copy.try_emplace(copy.cend(), 2, "b");

    // [THEN]
    EXPECT_EQ(copy.size(), 2);
    EXPECT_EQ(copy.at(2), "b");
    EXPECT_EQ(original.size(), 1);
}

TEST_F(Global_Types_SharedHashMapTests, TryEmplaceHint_ExistingKey_WhileShared_DoesNotOverwrite)
{
    // [GIVEN]
    SharedHashMap<int, std::string> original { { 1, "a" } };
    SharedHashMap<int, std::string> copy = original;

    // [WHEN]
    copy.try_emplace(std::as_const(copy).find(1), 1, "z");

    // [THEN]
    EXPECT_EQ(copy.size(), 1);
    EXPECT_EQ(copy.at(1), "a");
}

TEST_F(Global_Types_SharedHashMapTests, Merge_WithMapSharingStorage)
{
    // [GIVEN]
    SharedHashMap<int, std::string> a { { 1, "a" }, { 2, "b" } };
    SharedHashMap<int, std::string> b = a; // shares storage with a

    // [WHEN]
    a.insert({ 3, "c" }); // detaches a from b
    a.merge(b);

    // [THEN]
    EXPECT_EQ(a.size(), 3);
    EXPECT_TRUE(a.contains(1));
    EXPECT_TRUE(a.contains(2));
    EXPECT_TRUE(a.contains(3));
    EXPECT_EQ(b.size(), 2);
    EXPECT_TRUE(b.contains(1));
    EXPECT_TRUE(b.contains(2));
}
