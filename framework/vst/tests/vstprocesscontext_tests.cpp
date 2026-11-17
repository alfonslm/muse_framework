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

#include "pluginterfaces/vst/ivstprocesscontext.h"

#include "vst/internal/vstprocesscontext.h"

using namespace muse;
using namespace muse::vst;
using namespace Steinberg::Vst;

namespace {
audio::MusicalTimeMap twoTempoMap()
{
    // 120 bpm for 4 s (8 quarters), then 60 bpm; 4/4 from the start, 3/4 from quarter 8
    audio::MusicalTimeMap map;
    map.tempo = { { 0.0, 0.0, 2.0 }, { 4.0, 8.0, 1.0 } };
    map.bars = { { 0.0, 4, 4, 0.0 }, { 8.0, 3, 4, 0.0 } };
    return map;
}

class FakePlayhead : public audio::engine::IPlayheadPosition
{
public:
    const audio::TimePosition& currentPosition() const override { return m_position; }
    audio::MusicalPosition musicalPosition(const audio::TimePosition& position) const override
    {
        return map.positionAt(position.time().raw());
    }

    Loop loop() const override { return loopValue; }

    audio::MusicalTimeMap map;
    Loop loopValue;

private:
    audio::TimePosition m_position;
};
}

TEST(Vst_ProcessContextTests, OnlyTimeWithoutMusic)
{
    ProcessContext context {};
    context.state = ProcessContext::kPlaying | ProcessContext::kTempoValid | ProcessContext::kCycleActive;
    context.sampleRate = 48000;

    VstTransportInfo info;
    info.positionSamples = 96000;
    info.continuousSamples = 123;
    info.systemTimeNs = 456;

    fillProcessContext(context, info);

    EXPECT_EQ(context.projectTimeSamples, 96000);
    EXPECT_EQ(context.continousTimeSamples, 123);
    EXPECT_EQ(context.systemTime, 456);
    EXPECT_EQ(context.sampleRate, 48000);
    //! Playing stays; stale musical and cycle flags go
    EXPECT_EQ(context.state, ProcessContext::kPlaying | ProcessContext::kContTimeValid | ProcessContext::kSystemTimeValid);
}

TEST(Vst_ProcessContextTests, MusicalPositionAndCycle)
{
    ProcessContext context {};

    VstTransportInfo info;
    info.musical = twoTempoMap().positionAt(5.0); // 1 s into the 60 bpm part: quarter 9, in the 3/4 bar at 8
    info.hasCycle = true;
    info.cycleStartQuarters = 4.0;
    info.cycleEndQuarters = 10.0;

    fillProcessContext(context, info);

    EXPECT_DOUBLE_EQ(context.tempo, 60.0);
    EXPECT_DOUBLE_EQ(context.projectTimeMusic, 9.0);
    EXPECT_DOUBLE_EQ(context.barPositionMusic, 8.0);
    EXPECT_EQ(context.timeSigNumerator, 3);
    EXPECT_EQ(context.timeSigDenominator, 4);
    EXPECT_DOUBLE_EQ(context.cycleStartMusic, 4.0);
    EXPECT_DOUBLE_EQ(context.cycleEndMusic, 10.0);

    const uint32_t expected = ProcessContext::kTempoValid | ProcessContext::kProjectTimeMusicValid
                              | ProcessContext::kBarPositionValid | ProcessContext::kTimeSigValid
                              | ProcessContext::kCycleValid | ProcessContext::kCycleActive
                              | ProcessContext::kContTimeValid | ProcessContext::kSystemTimeValid;
    EXPECT_EQ(context.state, expected);
}

TEST(Vst_ProcessContextTests, NoCycleWithoutMusic)
{
    ProcessContext context {};

    VstTransportInfo info;
    info.hasCycle = true;
    info.cycleStartQuarters = 4.0;
    info.cycleEndQuarters = 10.0;

    fillProcessContext(context, info);

    EXPECT_FALSE(context.state & ProcessContext::kCycleValid);
    EXPECT_FALSE(context.state & ProcessContext::kCycleActive);
}

TEST(Vst_ProcessContextTests, LoopInQuarters)
{
    FakePlayhead playhead;
    playhead.map = twoTempoMap();

    double start = 0.0;
    double end = 0.0;

    //! No loop
    EXPECT_FALSE(loopInQuarters(playhead, 48000, start, end));

    //! From 1 s (quarter 2) to 6 s (quarter 10)
    playhead.loopValue = { 1.0, 6.0 };
    ASSERT_TRUE(loopInQuarters(playhead, 48000, start, end));
    EXPECT_DOUBLE_EQ(start, 2.0);
    EXPECT_DOUBLE_EQ(end, 10.0);

    //! A loop, but the music's tempo is not known
    playhead.map = audio::MusicalTimeMap();
    EXPECT_FALSE(loopInQuarters(playhead, 48000, start, end));
}

TEST(Vst_ProcessContextTests, SystemTimeRuns)
{
    const int64_t first = vstSystemTimeNs();
    const int64_t second = vstSystemTimeNs();
    EXPECT_GT(first, 0);
    EXPECT_GE(second, first);
}
