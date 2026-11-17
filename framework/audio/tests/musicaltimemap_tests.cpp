/*
 * SPDX-License-Identifier: GPL-3.0-only
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

#include "audio/common/musicaltimemap.h"

using namespace muse::audio;

class Audio_MusicalTimeMapTests : public ::testing::Test
{
};

TEST_F(Audio_MusicalTimeMapTests, Empty)
{
    MusicalTimeMap map;
    EXPECT_FALSE(map.isValid());

    const MusicalPosition pos = map.positionAt(3.0);
    EXPECT_FALSE(pos.isValid());
    EXPECT_FALSE(pos.hasTimeSig());
}

TEST_F(Audio_MusicalTimeMapTests, ConstantTempo)
{
    // 120 bpm, 4/4
    MusicalTimeMap map;
    map.tempo = { { 0.0, 0.0, 2.0 } };
    map.bars = { { 0.0, 4, 4, 0.0 } };

    MusicalPosition pos = map.positionAt(0.0);
    EXPECT_TRUE(pos.isValid());
    EXPECT_DOUBLE_EQ(pos.bpm, 120.0);
    EXPECT_DOUBLE_EQ(pos.quarters, 0.0);
    EXPECT_DOUBLE_EQ(pos.barStartQuarters, 0.0);
    EXPECT_EQ(pos.timeSigNumerator, 4);
    EXPECT_EQ(pos.timeSigDenominator, 4);

    // 2.75 seconds in: 5.5 quarter notes, in the 2nd bar
    pos = map.positionAt(2.75);
    EXPECT_DOUBLE_EQ(pos.quarters, 5.5);
    EXPECT_DOUBLE_EQ(pos.barStartQuarters, 4.0);

    // exactly on a bar line
    pos = map.positionAt(4.0);
    EXPECT_DOUBLE_EQ(pos.quarters, 8.0);
    EXPECT_DOUBLE_EQ(pos.barStartQuarters, 8.0);
}

TEST_F(Audio_MusicalTimeMapTests, TempoChange)
{
    // 120 bpm for 8 quarter notes (4 s), then 60 bpm
    MusicalTimeMap map;
    map.tempo = { { 0.0, 0.0, 2.0 }, { 4.0, 8.0, 1.0 } };

    MusicalPosition pos = map.positionAt(3.0);
    EXPECT_DOUBLE_EQ(pos.bpm, 120.0);
    EXPECT_DOUBLE_EQ(pos.quarters, 6.0);

    pos = map.positionAt(4.0);
    EXPECT_DOUBLE_EQ(pos.bpm, 60.0);
    EXPECT_DOUBLE_EQ(pos.quarters, 8.0);

    pos = map.positionAt(6.5);
    EXPECT_DOUBLE_EQ(pos.bpm, 60.0);
    EXPECT_DOUBLE_EQ(pos.quarters, 10.5);

    // no bar points: the position is known, the time signature is not
    EXPECT_TRUE(pos.isValid());
    EXPECT_FALSE(pos.hasTimeSig());
}

TEST_F(Audio_MusicalTimeMapTests, Pause)
{
    // 120 bpm, a 2 second caesura after 6 quarter notes (3 s)
    MusicalTimeMap map;
    map.tempo = { { 0.0, 0.0, 2.0 }, { 3.0, 6.0, 2.0 }, { 5.0, 6.0, 2.0 } };
    map.bars = { { 0.0, 4, 4, 0.0 } };

    EXPECT_DOUBLE_EQ(map.positionAt(2.5).quarters, 5.0);

    // the music stands still during the pause, the tempo is still reported
    for (double secs : { 3.0, 3.5, 4.999 }) {
        const MusicalPosition pos = map.positionAt(secs);
        EXPECT_DOUBLE_EQ(pos.quarters, 6.0);
        EXPECT_DOUBLE_EQ(pos.barStartQuarters, 4.0);
        EXPECT_DOUBLE_EQ(pos.bpm, 120.0);
    }

    // and goes on afterwards
    EXPECT_DOUBLE_EQ(map.positionAt(5.0).quarters, 6.0);
    EXPECT_DOUBLE_EQ(map.positionAt(6.0).quarters, 8.0);
    EXPECT_DOUBLE_EQ(map.positionAt(6.0).barStartQuarters, 8.0);
}

TEST_F(Audio_MusicalTimeMapTests, TimeSignatureChange)
{
    // two bars of 4/4, then 6/8 (3 quarter notes per bar)
    MusicalTimeMap map;
    map.tempo = { { 0.0, 0.0, 1.0 } };
    map.bars = { { 0.0, 4, 4, 0.0 }, { 8.0, 6, 8, 0.0 } };

    MusicalPosition pos = map.positionAt(7.0);
    EXPECT_EQ(pos.timeSigNumerator, 4);
    EXPECT_EQ(pos.timeSigDenominator, 4);
    EXPECT_DOUBLE_EQ(pos.barStartQuarters, 4.0);

    pos = map.positionAt(8.0);
    EXPECT_EQ(pos.timeSigNumerator, 6);
    EXPECT_EQ(pos.timeSigDenominator, 8);
    EXPECT_DOUBLE_EQ(pos.barStartQuarters, 8.0);

    pos = map.positionAt(12.5);
    EXPECT_EQ(pos.timeSigNumerator, 6);
    EXPECT_DOUBLE_EQ(pos.barStartQuarters, 11.0);
}

TEST_F(Audio_MusicalTimeMapTests, IrregularBar)
{
    // a pickup of one quarter note in 4/4, then full bars
    MusicalTimeMap map;
    map.tempo = { { 0.0, 0.0, 1.0 } };
    map.bars = { { 0.0, 4, 4, 1.0 }, { 1.0, 4, 4, 0.0 } };

    MusicalPosition pos = map.positionAt(0.5);
    EXPECT_DOUBLE_EQ(pos.barStartQuarters, 0.0);
    EXPECT_EQ(pos.timeSigNumerator, 4);

    pos = map.positionAt(1.0);
    EXPECT_DOUBLE_EQ(pos.barStartQuarters, 1.0);

    pos = map.positionAt(5.5);
    EXPECT_DOUBLE_EQ(pos.barStartQuarters, 5.0);
}

TEST_F(Audio_MusicalTimeMapTests, BeforeFirstPoint)
{
    MusicalTimeMap map;
    map.tempo = { { 1.0, 0.0, 2.0 } };
    map.bars = { { 0.0, 3, 4, 0.0 } };

    const MusicalPosition pos = map.positionAt(0.25);
    EXPECT_DOUBLE_EQ(pos.quarters, 0.0);
    EXPECT_DOUBLE_EQ(pos.bpm, 120.0);
    EXPECT_DOUBLE_EQ(pos.barStartQuarters, 0.0);
    EXPECT_EQ(pos.timeSigNumerator, 3);
}

TEST_F(Audio_MusicalTimeMapTests, NoBarsNoBarPosition)
{
    //! Tempo only: the bar start is not known, and must not be reported
    MusicalTimeMap map;
    map.tempo = { { 0.0, 0.0, 2.0 } };

    const MusicalPosition pos = map.positionAt(3.0);
    EXPECT_TRUE(pos.isValid());
    EXPECT_FALSE(pos.hasBar);
    EXPECT_FALSE(pos.hasTimeSig());

    map.bars = { { 0.0, 4, 4, 0.0 } };
    EXPECT_TRUE(map.positionAt(3.0).hasBar);
}

TEST_F(Audio_MusicalTimeMapTests, SanitizeKeepsAGoodMap)
{
    MusicalTimeMap map;
    map.tempo = { { 0.0, 0.0, 2.0 }, { 3.0, 6.0, 1.0 } };
    map.bars = { { 0.0, 4, 4, 0.0 }, { 4.0, 3, 4, 0.0 } };
    const MusicalTimeMap before = map;

    EXPECT_TRUE(map.sanitize());
    EXPECT_EQ(map, before);
}

TEST_F(Audio_MusicalTimeMapTests, SanitizeDropsAndSorts)
{
    MusicalTimeMap map;
    map.tempo = {
        { 3.0, 6.0, 1.0 },
        { 0.0, 0.0, 2.0 },
        { 4.0, 7.0, 0.0 },                  // no tempo
        { 5.0, 8.0, std::nan("") },          // not a number
        { 6.0, 9.0, -1.0 },                  // negative
    };
    map.bars = {
        { 4.0, 3, 4, 0.0 },
        { 0.0, 4, 4, 0.0 },
        { 8.0, 0, 4, 0.0 },                 // no numerator
    };

    EXPECT_FALSE(map.sanitize());

    ASSERT_EQ(map.tempo.size(), 2u);
    EXPECT_DOUBLE_EQ(map.tempo[0].secs, 0.0);
    EXPECT_DOUBLE_EQ(map.tempo[1].secs, 3.0);

    ASSERT_EQ(map.bars.size(), 2u);
    EXPECT_DOUBLE_EQ(map.bars[0].quarters, 0.0);
    EXPECT_DOUBLE_EQ(map.bars[1].quarters, 4.0);

    //! The lookups work on the result
    EXPECT_DOUBLE_EQ(map.positionAt(4.0).quarters, 7.0);
}
