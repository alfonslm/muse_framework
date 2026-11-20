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

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "audio/common/audiotypes.h"

#include "audio/engine/internal/nodes/signalnode.h"

using namespace muse;
using namespace muse::audio;
using namespace muse::audio::engine;

namespace {
constexpr double PI = 3.14159265358979323846;
constexpr samples_t FRAMES = 512;
constexpr audioch_t CHANNELS = 2;

//! Exposes what the meters are sent, without going through the async channel
class TestSignalNode : public SignalNode
{
public:
    TestSignalNode()
    {
        // Offline: values are still measured, nothing is sent
        setMode(ProcessMode::PlayingOffline);
        setOutputSpec(OutputSpec { 48000, FRAMES, CHANNELS });
    }

    void feed(std::vector<float> buffer)
    {
        doSelfProcess(buffer.data(), FRAMES);
    }

    float pressure(audioch_t ch) const
    {
        return m_signalValuesMap.at(ch).pressure.raw();
    }
};

//! Interleaved stereo square wave with the given amplitude
std::vector<float> square(float amplitude)
{
    std::vector<float> buffer(FRAMES * CHANNELS);
    for (samples_t f = 0; f < FRAMES; ++f) {
        const float v = (f / 32) % 2 ? -amplitude : amplitude;
        buffer[f * CHANNELS] = v;
        buffer[f * CHANNELS + 1] = v;
    }
    return buffer;
}

//! Interleaved stereo sine; 1 kHz at 48 kHz has a sample on every crest
std::vector<float> sine(float amplitude)
{
    std::vector<float> buffer(FRAMES * CHANNELS);
    for (samples_t f = 0; f < FRAMES; ++f) {
        const float v = amplitude * static_cast<float>(std::sin(2.0 * PI * 1000.0 * f / 48000.0));
        buffer[f * CHANNELS] = v;
        buffer[f * CHANNELS + 1] = v;
    }
    return buffer;
}

float dbOf(float linear)
{
    return 20.f * std::log10(linear);
}
}

//! A full-scale signal reads 0 dBFS: the meter value is the sample peak, with no RMS or gain
TEST(Audio_SignalNodeTests, FullScaleSquareReadsZeroDbfs)
{
    TestSignalNode node;
    node.feed(square(1.f));

    EXPECT_NEAR(node.pressure(0), 0.f, 1e-4f);
    EXPECT_NEAR(node.pressure(1), 0.f, 1e-4f);
}

TEST(Audio_SignalNodeTests, HalfScaleSquareReadsMinusSixDbfs)
{
    TestSignalNode node;
    node.feed(square(0.5f));

    EXPECT_NEAR(node.pressure(0), -6.0206f, 1e-3f);
}

TEST(Audio_SignalNodeTests, FullScaleSineReadsZeroDbfs)
{
    TestSignalNode node;
    node.feed(sine(1.f));

    EXPECT_NEAR(node.pressure(0), 0.f, 1e-3f);
}

//! A signal over full scale (the mix is float) reads above 0 dBFS instead of being cut off
TEST(Audio_SignalNodeTests, OverFullScaleReadsAboveZero)
{
    TestSignalNode node;
    node.feed(square(2.f));

    EXPECT_NEAR(node.pressure(0), dbOf(2.f), 1e-3f);
}

//! The first level is reported even when it is just below 0 dBFS
TEST(Audio_SignalNodeTests, FirstLevelIsReported)
{
    TestSignalNode node;
    node.feed(square(0.9f));

    EXPECT_NEAR(node.pressure(0), dbOf(0.9f), 1e-3f);
}

//! A rise is reported at once, however small, so the meter never reads low
TEST(Audio_SignalNodeTests, SmallRiseIsReported)
{
    TestSignalNode node;
    node.feed(square(0.5f));
    node.feed(square(0.5f * std::pow(10.f, 1.f / 20.f))); // +1 dB

    EXPECT_NEAR(node.pressure(0), -6.0206f + 1.f, 1e-3f);
}

//! A small fall is skipped (fewer updates); a fall of 2.5 dB or more is reported
TEST(Audio_SignalNodeTests, SmallFallIsSkippedLargeFallIsReported)
{
    TestSignalNode node;
    node.feed(square(0.5f));
    node.feed(square(0.5f * std::pow(10.f, -1.f / 20.f))); // -1 dB

    EXPECT_NEAR(node.pressure(0), -6.0206f, 1e-3f);

    node.feed(square(0.5f * std::pow(10.f, -6.f / 20.f))); // -6 dB
    EXPECT_NEAR(node.pressure(0), -12.0412f, 1e-3f);
}
