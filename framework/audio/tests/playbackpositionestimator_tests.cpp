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

#include "audio/common/playbackpositionestimator.h"

using namespace muse;
using namespace muse::audio;

namespace {
constexpr int64_t MS = 1000000; // ns
constexpr int64_t T0 = 1000 * MS; // some start time, not zero
constexpr double BLOCK = 0.010; // 10 ms
constexpr int64_t BLOCK_NS = 10 * MS;
constexpr int64_t LATENCY_NS = 40 * MS;

PlaybackPositionAnchor block(double position, int64_t presentationTimeNs, uint32_t epoch = 0, double duration = BLOCK)
{
    PlaybackPositionAnchor a;
    a.position = position;
    a.duration = duration;
    a.presentationTimeNs = presentationTimeNs;
    a.epoch = epoch;
    return a;
}

PlaybackPositionAnchor seek(double position, uint32_t epoch)
{
    PlaybackPositionAnchor a;
    a.position = position;
    a.epoch = epoch;
    return a;
}

//! Feeds `count` continuous blocks, the block `i` is rendered at T0 + i * BLOCK and heard LATENCY later
void feed(PlaybackPositionEstimator& e, int count, double startPosition = 0.0, int64_t startTimeNs = T0, uint32_t epoch = 0)
{
    for (int i = 0; i < count; ++i) {
        e.addAnchor(block(startPosition + i * BLOCK, startTimeNs + i * BLOCK_NS + LATENCY_NS, epoch));
    }
}
}

TEST(Audio_PlaybackPositionEstimatorTests, HoldsPositionWithoutAnchors)
{
    PlaybackPositionEstimator e;
    EXPECT_DOUBLE_EQ(e.positionAt(T0).raw(), 0.0);

    e.reset(3.5);
    EXPECT_DOUBLE_EQ(e.positionAt(T0).raw(), 3.5);
    EXPECT_DOUBLE_EQ(e.positionAt(T0 + 1000 * MS).raw(), 3.5);
    EXPECT_FALSE(e.hasAnchors());
}

TEST(Audio_PlaybackPositionEstimatorTests, AnchorTakesEffectWhenHeard)
{
    PlaybackPositionEstimator e;
    e.reset(2.0);
    feed(e, 10, 2.0);

    // The blocks are rendered, but nothing is heard yet
    EXPECT_DOUBLE_EQ(e.positionAt(T0).raw(), 2.0);
    EXPECT_DOUBLE_EQ(e.positionAt(T0 + LATENCY_NS - 1 * MS).raw(), 2.0);

    // The first sample is heard
    EXPECT_NEAR(e.positionAt(T0 + LATENCY_NS).raw(), 2.0, 1e-9);

    // 25 ms of audio are heard
    EXPECT_NEAR(e.positionAt(T0 + LATENCY_NS + 25 * MS).raw(), 2.025, 1e-9);
}

TEST(Audio_PlaybackPositionEstimatorTests, FollowsContinuousPlayback)
{
    PlaybackPositionEstimator e;
    feed(e, 200);

    for (int ms = 0; ms < 1990; ms += 7) {
        const double expected = ms / 1000.0;
        EXPECT_NEAR(e.positionAt(T0 + LATENCY_NS + ms * MS).raw(), expected, 1e-6) << "ms: " << ms;
    }
}

TEST(Audio_PlaybackPositionEstimatorTests, GoesOnForAWhileIfNextAnchorIsLate)
{
    PlaybackPositionEstimator e;
    feed(e, 1);

    // the block is over, but the next anchor is not delivered yet
    EXPECT_NEAR(e.positionAt(T0 + LATENCY_NS + 30 * MS).raw(), 0.030, 1e-9);

    // ... but not forever
    EXPECT_NEAR(e.positionAt(T0 + LATENCY_NS + 5000 * MS).raw(), BLOCK + 0.1, 1e-9);
}

TEST(Audio_PlaybackPositionEstimatorTests, SmoothsTimingJitter)
{
    PlaybackPositionEstimator e;

    // +-3 ms of jitter on the presentation time, the positions are exact
    const int64_t jitterMs[] = { 0, 3, -3, 2, -2, 3, -1, -3, 1, 0 };
    for (int i = 0; i < 300; ++i) {
        const int64_t jitter = jitterMs[i % 10] * MS;
        e.addAnchor(block(i * BLOCK, T0 + i * BLOCK_NS + LATENCY_NS + jitter));
    }

    double prev = -1.0;
    for (int ms = 500; ms < 2900; ++ms) {
        const double pos = e.positionAt(T0 + LATENCY_NS + ms * MS).raw();
        const double expected = ms / 1000.0;

        // never goes back
        EXPECT_GE(pos, prev) << "ms: " << ms;

        // much closer to the truth than the jitter
        EXPECT_NEAR(pos, expected, 0.0015) << "ms: " << ms;

        // moves evenly: 1 ms of time is about 1 ms of position
        if (prev >= 0.0) {
            EXPECT_LT(pos - prev, 0.002) << "ms: " << ms;
        }

        prev = pos;
    }
}

TEST(Audio_PlaybackPositionEstimatorTests, LoopJumpsWhenItIsHeard)
{
    PlaybackPositionEstimator e;

    // loop [0.0, 0.1): 10 blocks, then back to the start
    feed(e, 10, 0.0, T0);
    feed(e, 10, 0.0, T0 + 10 * BLOCK_NS);

    // The engine has already wrapped, but the end of the loop is still to be heard
    EXPECT_NEAR(e.positionAt(T0 + LATENCY_NS + 95 * MS).raw(), 0.095, 1e-6);

    // Does not run over the loop end
    EXPECT_LE(e.positionAt(T0 + LATENCY_NS + 100 * MS - 1).raw(), 0.1);

    // The start of the loop is heard
    EXPECT_NEAR(e.positionAt(T0 + LATENCY_NS + 105 * MS).raw(), 0.005, 1e-6);
}

TEST(Audio_PlaybackPositionEstimatorTests, SeekTakesEffectImmediately)
{
    PlaybackPositionEstimator e;
    feed(e, 100);

    EXPECT_NEAR(e.positionAt(T0 + LATENCY_NS + 50 * MS).raw(), 0.050, 1e-6);

    // seek to 10 secs
    e.addAnchor(seek(10.0, 1));
    EXPECT_DOUBLE_EQ(e.positionAt(T0 + LATENCY_NS + 51 * MS).raw(), 10.0);

    // new blocks are rendered from T0 + 100 ms
    feed(e, 100, 10.0, T0 + 100 * MS, 1);

    // ... holds the position until they are heard
    EXPECT_DOUBLE_EQ(e.positionAt(T0 + 120 * MS).raw(), 10.0);
    EXPECT_NEAR(e.positionAt(T0 + 100 * MS + LATENCY_NS + 30 * MS).raw(), 10.030, 1e-6);
}

TEST(Audio_PlaybackPositionEstimatorTests, DropsAnchorsMadeBeforeSeek)
{
    PlaybackPositionEstimator e;
    feed(e, 5);

    e.addAnchor(seek(10.0, 1));

    // made before the seek, delivered after it
    e.addAnchor(block(5 * BLOCK, T0 + 5 * BLOCK_NS + LATENCY_NS, 0));
    e.addAnchor(block(6 * BLOCK, T0 + 6 * BLOCK_NS + LATENCY_NS, 0));

    EXPECT_FALSE(e.hasAnchors());
    EXPECT_DOUBLE_EQ(e.positionAt(T0 + 1000 * MS).raw(), 10.0);
}

TEST(Audio_PlaybackPositionEstimatorTests, SeekBackIsNotBlockedByMonotonicity)
{
    PlaybackPositionEstimator e;
    feed(e, 100, 5.0);
    EXPECT_NEAR(e.positionAt(T0 + LATENCY_NS + 500 * MS).raw(), 5.5, 1e-6);

    e.addAnchor(seek(1.0, 1));
    feed(e, 100, 1.0, T0 + 600 * MS, 1);

    EXPECT_NEAR(e.positionAt(T0 + 600 * MS + LATENCY_NS + 20 * MS).raw(), 1.020, 1e-6);
}

TEST(Audio_PlaybackPositionEstimatorTests, CompensationShiftsTheTime)
{
    PlaybackPositionEstimator e;
    feed(e, 200);

    const int64_t now = T0 + LATENCY_NS + 500 * MS;

    // 150 ms of latency unknown to the driver: what is heard now was "presented" 150 ms ago
    {
        PlaybackPositionEstimator e2 = e;
        EXPECT_NEAR(e2.positionAt(now - 150 * MS).raw(), 0.350, 1e-6);
    }

    // 20 ms of display latency: the frame drawn now is seen 20 ms later
    {
        PlaybackPositionEstimator e2 = e;
        EXPECT_NEAR(e2.positionAt(now + 20 * MS).raw(), 0.520, 1e-6);
    }
}

TEST(Audio_PlaybackPositionEstimatorTests, LatencyChangeIsFollowed)
{
    PlaybackPositionEstimator e;
    feed(e, 100);

    // the output latency grows by 200 ms (e.g. the output goes to another device)
    for (int i = 100; i < 300; ++i) {
        e.addAnchor(block(i * BLOCK, T0 + i * BLOCK_NS + LATENCY_NS + 200 * MS));
    }

    EXPECT_NEAR(e.positionAt(T0 + LATENCY_NS + 200 * MS + 2000 * MS).raw(), 2.0, 1e-6);
}
