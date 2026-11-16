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

#include <cmath>
#include <memory>
#include <vector>

// pragma-pack matches rpcpacker_tests.cpp to avoid an ODR segfault at static init.
#pragma pack(push, 1)
#include "audio/common/audiotypes.h"
#pragma pack(pop)

#include "audio/engine/internal/nodes/fileaudionode.h"

//! NOTE The unit under test is compiled into this file: this test binary does not link the audio engine
#include "audio/engine/internal/nodes/fileaudionode.cpp"

using namespace muse;
using namespace muse::audio;
using namespace muse::audio::engine;
using namespace muse::audio::codec;

namespace {
constexpr double PI = 3.14159265358979323846;

std::shared_ptr<const PcmAudio> makeRamp(sample_rate_t sampleRate, audioch_t channels, samples_t frames)
{
    auto audio = std::make_shared<PcmAudio>();
    audio->sampleRate = sampleRate;
    audio->channels = channels;
    audio->samples.resize(frames * channels);
    for (samples_t f = 0; f < frames; ++f) {
        for (audioch_t ch = 0; ch < channels; ++ch) {
            // frame number in the left channel, its negative in the right one
            audio->samples[f * channels + ch] = (ch == 0 ? 1.f : -1.f) * static_cast<float>(f + 1);
        }
    }
    return audio;
}

std::shared_ptr<const PcmAudio> makeSine(sample_rate_t sampleRate, double hz, samples_t frames)
{
    auto audio = std::make_shared<PcmAudio>();
    audio->sampleRate = sampleRate;
    audio->channels = 1;
    audio->samples.resize(frames);
    for (samples_t f = 0; f < frames; ++f) {
        audio->samples[f] = static_cast<float>(std::sin(2.0 * PI * hz * f / sampleRate));
    }
    return audio;
}

FileAudioNodePtr makeNode(std::shared_ptr<const PcmAudio> audio, sample_rate_t outSampleRate, audioch_t outChannels = 2)
{
    FileAudioNodePtr node = std::make_shared<FileAudioNode>(audio);
    node->setOutputSpec(OutputSpec { outSampleRate, 512, outChannels });
    node->setMode(ProcessMode::Idle);
    node->seek(TimePosition::zero(outSampleRate));
    return node;
}

std::vector<float> render(FileAudioNodePtr node, samples_t frames, samples_t blockSize)
{
    const audioch_t channels = node->outputSpec().audioChannelCount;
    std::vector<float> out(frames * channels, 99.f);
    for (samples_t done = 0; done < frames; done += blockSize) {
        node->process(out.data() + done * channels, std::min(blockSize, frames - done));
    }
    return out;
}
}

TEST(Audio_FileAudioNodeTests, PlaysTheFileFromTimeZero)
{
    FileAudioNodePtr node = makeNode(makeRamp(48000, 2, 1000), 48000);
    EXPECT_DOUBLE_EQ(node->duration().raw(), 1000.0 / 48000.0);

    node->setMode(ProcessMode::Playing);
    const std::vector<float> out = render(node, 1200, 256);

    for (samples_t f = 0; f < 1000; ++f) {
        ASSERT_FLOAT_EQ(out[f * 2], static_cast<float>(f + 1)) << "frame " << f;
        ASSERT_FLOAT_EQ(out[f * 2 + 1], -static_cast<float>(f + 1)) << "frame " << f;
    }
    // After the end of the file: silence
    for (samples_t f = 1000; f < 1200; ++f) {
        ASSERT_FLOAT_EQ(out[f * 2], 0.f) << "frame " << f;
        ASSERT_FLOAT_EQ(out[f * 2 + 1], 0.f) << "frame " << f;
    }
}

TEST(Audio_FileAudioNodeTests, SilentAndStandingStillWhenNotPlaying)
{
    FileAudioNodePtr node = makeNode(makeRamp(48000, 2, 1000), 48000);

    // Idle: silence, and the position does not move
    std::vector<float> out = render(node, 300, 100);
    for (float v : out) {
        ASSERT_FLOAT_EQ(v, 0.f);
    }

    node->setMode(ProcessMode::Playing);
    out = render(node, 10, 10);
    EXPECT_FLOAT_EQ(out[0], 1.f); // still at frame 0
}

TEST(Audio_FileAudioNodeTests, SeekIsExact)
{
    FileAudioNodePtr node = makeNode(makeRamp(48000, 2, 48000), 48000);
    node->setMode(ProcessMode::Playing);

    render(node, 4096, 512);

    node->seek(TimePosition::fromTime(0.5, 48000));
    std::vector<float> out = render(node, 64, 64);
    EXPECT_FLOAT_EQ(out[0], 24001.f);
    EXPECT_FLOAT_EQ(out[63 * 2], 24064.f);

    // The same position again (loop, replay): the same audio
    node->seek(TimePosition::fromTime(0.5, 48000));
    const std::vector<float> again = render(node, 64, 16);
    EXPECT_EQ(out, again);

    // A seek given at another sample rate lands at the same time
    node->seek(TimePosition::fromTime(0.5, 44100));
    out = render(node, 1, 1);
    EXPECT_FLOAT_EQ(out[0], 24001.f);

    // Past the end: silence, no reading outside the audio
    node->seek(TimePosition::fromTime(10.0, 48000));
    out = render(node, 64, 64);
    for (float v : out) {
        ASSERT_FLOAT_EQ(v, 0.f);
    }
}

TEST(Audio_FileAudioNodeTests, ChannelLayouts)
{
    // Mono file on a stereo output: the same signal on both sides
    FileAudioNodePtr mono = makeNode(makeRamp(48000, 1, 100), 48000, 2);
    mono->setMode(ProcessMode::Playing);
    std::vector<float> out = render(mono, 10, 10);
    EXPECT_FLOAT_EQ(out[4 * 2], 5.f);
    EXPECT_FLOAT_EQ(out[4 * 2 + 1], 5.f);

    // Stereo file on a mono output: the average (left = n, right = -n)
    FileAudioNodePtr stereo = makeNode(makeRamp(48000, 2, 100), 48000, 1);
    stereo->setMode(ProcessMode::Playing);
    out = render(stereo, 10, 10);
    EXPECT_FLOAT_EQ(out[4], 0.f);
}

TEST(Audio_FileAudioNodeTests, ConvertsTheSampleRate)
{
    const double hz = 1000.0;

    for (const auto& [fileRate, outRate] : std::vector<std::pair<sample_rate_t, sample_rate_t> > {
        { 44100, 48000 }, { 48000, 44100 }, { 96000, 44100 }, { 22050, 48000 } }) {
        FileAudioNodePtr node = makeNode(makeSine(fileRate, hz, fileRate), outRate, 1); // one second
        node->setMode(ProcessMode::Playing);

        const samples_t frames = outRate / 2;
        const std::vector<float> out = render(node, frames, 480);

        // Compare with the ideal tone, away from the very start (the first samples have half a kernel)
        double maxError = 0.0;
        for (samples_t f = 200; f < frames; ++f) {
            const double expected = std::sin(2.0 * PI * hz * f / outRate);
            maxError = std::max(maxError, std::abs(out[f] - expected));
        }
        EXPECT_LT(maxError, 2e-3) << fileRate << " -> " << outRate;

        // The block size must not change a single sample: the conversion keeps no state
        node->seek(TimePosition::zero(outRate));
        const std::vector<float> otherBlocks = render(node, frames, 127);
        EXPECT_EQ(out, otherBlocks) << fileRate << " -> " << outRate;
    }
}

TEST(Audio_FileAudioNodeTests, KeepsTheTimeWhenTheOutputRateChanges)
{
    // 1 kHz, played to 0.25 s at 48 kHz; then the output switches to 44.1 kHz (as an export does)
    FileAudioNodePtr node = makeNode(makeSine(48000, 1000.0, 48000), 48000, 1);
    node->setMode(ProcessMode::Playing);
    render(node, 12000, 480);

    node->setOutputSpec(OutputSpec { 44100, 512, 1 });
    const std::vector<float> out = render(node, 441, 441);

    for (samples_t f = 0; f < 441; ++f) {
        const double expected = std::sin(2.0 * PI * 1000.0 * (0.25 + static_cast<double>(f) / 44100.0));
        ASSERT_NEAR(out[f], expected, 2e-3) << "frame " << f;
    }
}

TEST(Audio_FileAudioNodeTests, EmptyAudio)
{
    FileAudioNodePtr node = makeNode(std::make_shared<PcmAudio>(), 48000);
    node->setMode(ProcessMode::Playing);

    const std::vector<float> out = render(node, 64, 64);
    for (float v : out) {
        ASSERT_FLOAT_EQ(v, 0.f);
    }
    EXPECT_TRUE(node->readyToPlay());
    EXPECT_FALSE(node->hasPendingChunks());
}
