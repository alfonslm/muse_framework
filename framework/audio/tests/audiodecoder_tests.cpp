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

#include <algorithm>
#include <vector>

// pragma-pack matches rpcpacker_tests.cpp to avoid an ODR segfault at static init (FD-040).
#pragma pack(push, 1)
#include "audio/common/audiotypes.h"
#pragma pack(pop)

#include "audio/engine/internal/codecs/audiodecoder.h"

#include "wavtestutils.h"

//! NOTE The units under test are compiled into this file: this test binary does not link the audio engine.
//! wavdecoder.cpp is already compiled into wavdecoder_tests.cpp of the same binary, so it is not included here.
#include "audio/engine/internal/codecs/wavaudiodecoder.cpp"
#include "audio/engine/internal/codecs/audiodecoder.cpp"

using namespace muse;
using namespace muse::audio;
using namespace muse::audio::codec;
using namespace muse::audio::tests;

namespace {
struct Format {
    uint16_t channels;
    uint16_t bits;
    bool isFloat;
};

std::vector<double> testSamples(size_t frames, uint16_t channels)
{
    std::vector<double> samples;
    for (size_t i = 0; i < frames * channels; ++i) {
        samples.push_back(((i * 37) % 200) / 100.0 - 1.0);
    }
    return samples;
}

const Format FORMATS[] = {
    { 1, 16, false }, { 2, 16, false }, { 1, 24, false }, { 2, 24, false }, { 1, 32, true }, { 2, 32, true },
};
}

TEST(Audio_AudioDecoderTests, DecodeAllEqualsWavDecoder)
{
    for (const Format& f : FORMATS) {
        const std::vector<uint8_t> wav = makeWav(48000, f.channels, f.bits, f.isFloat, testSamples(10000, f.channels));

        RetVal<IAudioDecoderPtr> decoder = makeDecoder(wav.data(), wav.size());
        ASSERT_TRUE(decoder.ret) << f.bits << " bit, " << f.channels << " ch";
        RetVal<PcmAudio> all = decodeAll(*decoder.val);
        RetVal<PcmAudio> expected = WavDecoder::decode(wav.data(), wav.size());

        ASSERT_TRUE(all.ret);
        ASSERT_TRUE(expected.ret);
        EXPECT_EQ(all.val.sampleRate, expected.val.sampleRate);
        EXPECT_EQ(all.val.channels, expected.val.channels);
        EXPECT_EQ(all.val.samples, expected.val.samples);
        EXPECT_EQ(all.val.frames(), 10000u);
    }
}

TEST(Audio_AudioDecoderTests, InfoMatchesFile)
{
    const std::vector<uint8_t> wav = makeWav(22050, 2, 16, false, testSamples(100, 2));
    RetVal<IAudioDecoderPtr> decoder = makeDecoder(wav.data(), wav.size());
    ASSERT_TRUE(decoder.ret);

    const AudioStreamInfo info = decoder.val->info();
    EXPECT_EQ(info.sampleRate, 22050u);
    EXPECT_EQ(info.channels, 2);
    EXPECT_EQ(info.frames, 100u);
}

TEST(Audio_AudioDecoderTests, ReadInOddChunksAndSeek)
{
    for (const Format& f : FORMATS) {
        const std::vector<uint8_t> wav = makeWav(44100, f.channels, f.bits, f.isFloat, testSamples(1000, f.channels));
        const PcmAudio expected = WavDecoder::decode(wav.data(), wav.size()).val;

        RetVal<IAudioDecoderPtr> decoder = makeDecoder(wav.data(), wav.size());
        ASSERT_TRUE(decoder.ret);
        IAudioDecoder& d = *decoder.val;

        std::vector<float> got;
        std::vector<float> buf(7 * f.channels);
        const samples_t sizes[] = { 7, 3, 1, 5 };
        size_t i = 0;
        while (true) {
            const samples_t want = sizes[i++ % 4];
            const samples_t n = d.readFrames(buf.data(), want);
            got.insert(got.end(), buf.begin(), buf.begin() + n * f.channels);
            if (n < want) {
                break;
            }
        }
        EXPECT_EQ(got, expected.samples);
        EXPECT_EQ(d.readFrames(buf.data(), 7), 0u);

        // seek to the middle and read the rest
        ASSERT_TRUE(d.seek(500));
        std::vector<float> rest(500 * f.channels);
        EXPECT_EQ(d.readFrames(rest.data(), 700), 500u);
        EXPECT_TRUE(std::equal(rest.begin(), rest.end(), expected.samples.begin() + 500 * f.channels));
    }
}

TEST(Audio_AudioDecoderTests, SeekToEndAndPastEnd)
{
    const std::vector<uint8_t> wav = makeWav(44100, 1, 16, false, testSamples(100, 1));
    RetVal<IAudioDecoderPtr> decoder = makeDecoder(wav.data(), wav.size());
    ASSERT_TRUE(decoder.ret);
    IAudioDecoder& d = *decoder.val;

    float buf[4];
    ASSERT_TRUE(d.seek(40));
    EXPECT_FALSE(d.seek(101)); // fails, position stays
    EXPECT_EQ(d.readFrames(buf, 4), 4u);

    EXPECT_TRUE(d.seek(100)); // the end is valid
    EXPECT_EQ(d.readFrames(buf, 4), 0u);
}

TEST(Audio_AudioDecoderTests, RefusesWhatItDoesNotRecognise)
{
    const std::vector<uint8_t> garbage = { 'g', 'a', 'r', 'b', 'a', 'g', 'e', '!', '!', '!', '!', '!', '!', '!' };
    EXPECT_FALSE(makeDecoder(garbage.data(), garbage.size()).ret);
    EXPECT_FALSE(makeDecoder(nullptr, 0).ret);
    const uint8_t one = 0;
    EXPECT_FALSE(makeDecoder(&one, 0).ret);

    const std::vector<uint8_t> wav = makeWav(44100, 2, 16, false, testSamples(100, 2));
    EXPECT_FALSE(makeDecoder(wav.data(), 8).ret);   // truncated before the format
    EXPECT_FALSE(makeDecoder(wav.data(), 12).ret);  // only the RIFF header
    EXPECT_FALSE(makeDecoder(wav.data(), 20).ret);  // cut inside the first chunk
}

TEST(Audio_AudioDecoderTests, DecodeAllRefusesTooLongAudio)
{
    const std::vector<uint8_t> wav = makeWav(44100, 2, 16, false, testSamples(100, 2));
    RetVal<IAudioDecoderPtr> decoder = makeDecoder(wav.data(), wav.size());
    ASSERT_TRUE(decoder.ret);

    EXPECT_FALSE(decodeAll(*decoder.val, 199).ret);
    EXPECT_TRUE(decodeAll(*decoder.val, 200).ret);
}
