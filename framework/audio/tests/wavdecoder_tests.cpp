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

#include <cstring>
#include <vector>

// pragma-pack matches rpcpacker_tests.cpp to avoid an ODR segfault at static init.
#pragma pack(push, 1)
#include "audio/common/audiotypes.h"
#pragma pack(pop)

#include "audio/engine/internal/codecs/wavdecoder.h"

#include "wavtestutils.h"

//! NOTE The unit under test is compiled into this file: this test binary does not link the audio engine
#include "audio/engine/internal/codecs/wavdecoder.cpp"

using namespace muse;
using namespace muse::audio;
using namespace muse::audio::codec;
using namespace muse::audio::tests;

TEST(Audio_WavDecoderTests, Pcm16Stereo)
{
    const std::vector<double> samples = { 0.0, 0.5, -0.5, 1.0 - 1.0 / 32768.0, -1.0, 0.25 };
    const std::vector<uint8_t> wav = makeWav(44100, 2, 16, false, samples);

    RetVal<PcmAudio> audio = WavDecoder::decode(wav.data(), wav.size());

    ASSERT_TRUE(audio.ret);
    EXPECT_EQ(audio.val.sampleRate, 44100u);
    EXPECT_EQ(audio.val.channels, 2);
    EXPECT_EQ(audio.val.frames(), 3u);
    ASSERT_EQ(audio.val.samples.size(), samples.size());
    for (size_t i = 0; i < samples.size(); ++i) {
        EXPECT_NEAR(audio.val.samples[i], samples[i], 1.0 / 32768.0) << "sample " << i;
    }
}

TEST(Audio_WavDecoderTests, EverySampleFormat)
{
    const std::vector<double> samples = { 0.0, 0.5, -0.5, 0.125, -1.0 };

    struct Case {
        uint16_t bits;
        bool isFloat;
        double tolerance;
    };

    const std::vector<Case> cases = {
        { 8, false, 1.0 / 128 }, { 16, false, 1.0 / 32768 }, { 24, false, 1e-6 },
        { 32, false, 1e-6 }, { 32, true, 1e-7 }, { 64, true, 1e-7 }
    };

    for (const Case& c : cases) {
        for (int extensible = 0; extensible < 2; ++extensible) {
            const std::vector<uint8_t> wav = makeWav(48000, 1, c.bits, c.isFloat, samples, extensible == 1);
            RetVal<PcmAudio> audio = WavDecoder::decode(wav.data(), wav.size());

            ASSERT_TRUE(audio.ret) << c.bits << " bits, float " << c.isFloat << ", extensible " << extensible;
            ASSERT_EQ(audio.val.samples.size(), samples.size());
            for (size_t i = 0; i < samples.size(); ++i) {
                EXPECT_NEAR(audio.val.samples[i], samples[i], c.tolerance) << c.bits << " bits, sample " << i;
            }
        }
    }
}

TEST(Audio_WavDecoderTests, RefusesWhatItCannotRead)
{
    EXPECT_FALSE(WavDecoder::decode(nullptr, 0).ret);

    const std::vector<uint8_t> garbage(64, 0x55);
    EXPECT_FALSE(WavDecoder::decode(garbage.data(), garbage.size()).ret);

    // A format we do not decode (A-law, tag 6)
    std::vector<uint8_t> alaw = makeWav(8000, 1, 8, false, { 0.0, 0.1 });
    const size_t fmtPos = findChunk(alaw, "fmt ");
    ASSERT_NE(fmtPos, std::string::npos);
    alaw[fmtPos + 8] = 6;
    EXPECT_FALSE(WavDecoder::decode(alaw.data(), alaw.size()).ret);

    // Header only, cut off inside the format chunk
    const std::vector<uint8_t> wav = makeWav(44100, 2, 16, false, { 0.0, 0.0 });
    EXPECT_FALSE(WavDecoder::decode(wav.data(), fmtPos + 12).ret);
}

TEST(Audio_WavDecoderTests, DataChunkLongerThanTheFile)
{
    // A recording that was cut off: the data chunk says more than there is. Use what is there.
    std::vector<uint8_t> wav = makeWav(44100, 1, 16, false, { 0.1, 0.2, 0.3, 0.4 });
    const size_t dataPos = findChunk(wav, "data");
    ASSERT_NE(dataPos, std::string::npos);
    wav.resize(wav.size() - 3); // lose one whole frame and half of another

    RetVal<PcmAudio> audio = WavDecoder::decode(wav.data(), wav.size());

    ASSERT_TRUE(audio.ret);
    EXPECT_EQ(audio.val.frames(), 2u);
    EXPECT_NEAR(audio.val.samples[1], 0.2, 1.0 / 32768);
}

TEST(Audio_WavDecoderTests, RefusesAudioLongerThanTheLimit)
{
    const std::vector<double> samples(200, 0.25); // 100 frames, 2 channels
    const std::vector<uint8_t> wav = makeWav(44100, 2, 16, false, samples);

    RetVal<PcmAudio> atLimit = WavDecoder::decode(wav.data(), wav.size(), 200);
    ASSERT_TRUE(atLimit.ret);
    EXPECT_EQ(atLimit.val.frames(), 100u);

    RetVal<PcmAudio> overLimit = WavDecoder::decode(wav.data(), wav.size(), 199);
    EXPECT_FALSE(overLimit.ret);
}
