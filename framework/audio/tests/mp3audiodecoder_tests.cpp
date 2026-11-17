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

#include <algorithm>
#include <thread>
#include <vector>

// pragma-pack matches rpcpacker_tests.cpp to avoid an ODR segfault at static init (FD-040).
#pragma pack(push, 1)
#include "audio/common/audiotypes.h"
#pragma pack(pop)

#include "audio/engine/internal/codecs/audiodecoder.h"
#include "audio/engine/internal/codecs/mp3audiodecoder.h"

#include "codectestutils.h"

using namespace muse;
using namespace muse::audio;
using namespace muse::audio::codec;
using namespace muse::audio::tests;

namespace {
constexpr size_t MP3_FRAME_SAMPLES = 1152;

struct Decoded {
    bool ok = false;
    PcmAudio audio;
};

//! Opens `bytes` through makeDecoder() and decodes everything; !ok if either refuses
Decoded decodeBytes(const std::vector<uint8_t>& bytes, size_t maxSamples = WavDecoder::MAX_SAMPLES)
{
    Decoded result;
    RetVal<IAudioDecoderPtr> decoder = makeDecoder(bytes.data(), bytes.size());
    if (!decoder.ret) {
        return result;
    }
    RetVal<PcmAudio> all = decodeAll(*decoder.val, maxSamples);
    if (!all.ret) {
        return result;
    }
    result.ok = true;
    result.audio = std::move(all.val);
    return result;
}

std::vector<float> referenceWav()
{
    const std::vector<uint8_t> wav = readCodecTestFile("reference.wav");
    RetVal<PcmAudio> pcm = WavDecoder::decode(wav.data(), wav.size());
    return pcm.ret ? pcm.val.samples : std::vector<float>();
}

//! `bytes` without its leading ID3v2 tag (the size is 4 syncsafe bytes at offset 6)
std::vector<uint8_t> withoutId3(const std::vector<uint8_t>& bytes)
{
    const size_t tag = 10 + ((size_t(bytes[6] & 0x7F) << 21) | (size_t(bytes[7] & 0x7F) << 14)
                             | (size_t(bytes[8] & 0x7F) << 7) | size_t(bytes[9] & 0x7F));
    return std::vector<uint8_t>(bytes.begin() + tag, bytes.end());
}
}

//! Measured (libmpg123 1.32.10): 48000 Hz, 2 ch, 24000 frames (same as reference.wav), lag 0 on both
//! channels, max difference to reference.wav 0.0394 (lossy encoding); the limit has a margin of 2.5x
TEST(Audio_Mp3AudioDecoderTests, ReferenceWithTagIsGaplessAndOnTheBeat)
{
    const std::vector<uint8_t> bytes = readCodecTestFile("reference.mp3");
    ASSERT_FALSE(bytes.empty());
    EXPECT_EQ(detectFormat(bytes.data(), bytes.size()), AudioFileFormat::Mp3);

    RetVal<IAudioDecoderPtr> decoder = makeDecoder(bytes.data(), bytes.size());
    ASSERT_TRUE(decoder.ret);
    EXPECT_EQ(decoder.val->info().sampleRate, 48000);
    EXPECT_EQ(decoder.val->info().channels, 2);
    EXPECT_EQ(decoder.val->info().frames, 24000);

    RetVal<PcmAudio> all = decodeAll(*decoder.val);
    ASSERT_TRUE(all.ret);
    EXPECT_EQ(all.val.frames(), 24000u);

    const std::vector<float> ref = referenceWav();
    ASSERT_EQ(ref.size(), all.val.samples.size());
    EXPECT_EQ(bestLag(ref, all.val.samples, 2, 0, 3000), 0);
    EXPECT_LT(maxDifference(ref, all.val.samples), 0.1f);
}

//! Measured: 25344 frames (24000 + 1344, the encoder delay is kept), lag 1105 frames (23 ms) on both
//! channels; shifted back by that lag the max difference to reference.wav is the same 0.0394.
//! FontDaw's own MP3 export has no tag either (FD-110), so this is what its import does today.
TEST(Audio_Mp3AudioDecoderTests, FileWithoutTagKeepsEncoderDelay)
{
    const std::vector<uint8_t> bytes = readCodecTestFile("notag.mp3");
    ASSERT_FALSE(bytes.empty());

    Decoded d = decodeBytes(bytes);
    ASSERT_TRUE(d.ok);
    EXPECT_EQ(d.audio.sampleRate, 48000);
    EXPECT_EQ(d.audio.channels, 2);

    const std::vector<float> ref = referenceWav();
    const long lag = bestLag(ref, d.audio.samples, 2, 0, 3000);
    EXPECT_GT(lag, 0);
    EXPECT_LT(lag, 1500);
    EXPECT_GT(d.audio.frames(), 24000u);
}

TEST(Audio_Mp3AudioDecoderTests, WithoutId3TagStillDetectedAndDecodesTheSame)
{
    const std::vector<uint8_t> bytes = readCodecTestFile("reference.mp3");
    ASSERT_GT(bytes.size(), 20u);
    const std::vector<uint8_t> stripped = withoutId3(bytes);
    ASSERT_LT(stripped.size(), bytes.size());

    EXPECT_EQ(detectFormat(stripped.data(), stripped.size()), AudioFileFormat::Mp3);

    Decoded a = decodeBytes(bytes);
    Decoded b = decodeBytes(stripped);
    ASSERT_TRUE(a.ok);
    ASSERT_TRUE(b.ok);
    EXPECT_EQ(a.audio.samples, b.audio.samples);
}

TEST(Audio_Mp3AudioDecoderTests, OddChunksGiveTheSameSamples)
{
    const std::vector<uint8_t> bytes = readCodecTestFile("reference.mp3");
    Decoded whole = decodeBytes(bytes);
    ASSERT_TRUE(whole.ok);

    RetVal<IAudioDecoderPtr> decoder = makeDecoder(bytes.data(), bytes.size());
    ASSERT_TRUE(decoder.ret);

    const size_t sizes[] = { 7, 1000, 333 };
    std::vector<float> chunked;
    std::vector<float> buffer(1000 * 2);
    for (size_t i = 0;; ++i) {
        const samples_t n = sizes[i % 3];
        const samples_t got = decoder.val->readFrames(buffer.data(), n);
        ASSERT_LE(got, n);
        chunked.insert(chunked.end(), buffer.begin(), buffer.begin() + got * 2);
        if (got < n) {
            break;
        }
    }

    EXPECT_TRUE(decoder.val->error());
    EXPECT_EQ(chunked, whole.audio.samples);
}

TEST(Audio_Mp3AudioDecoderTests, SeekThenReadMatchesSequentialDecode)
{
    const std::vector<uint8_t> bytes = readCodecTestFile("reference.mp3");
    Decoded whole = decodeBytes(bytes);
    ASSERT_TRUE(whole.ok);

    RetVal<IAudioDecoderPtr> decoder = makeDecoder(bytes.data(), bytes.size());
    ASSERT_TRUE(decoder.ret);
    IAudioDecoder& d = *decoder.val;

    ASSERT_TRUE(d.seek(12000));
    std::vector<float> out(1000 * 2);
    ASSERT_EQ(d.readFrames(out.data(), 1000), 1000);
    const std::vector<float> expected(whole.audio.samples.begin() + 12000 * 2, whole.audio.samples.begin() + 13000 * 2);
    EXPECT_EQ(out, expected);

    // The end is a valid position and reads nothing
    const samples_t frames = d.info().frames;
    ASSERT_TRUE(d.seek(frames));
    EXPECT_EQ(d.readFrames(out.data(), 1000), 0);
    EXPECT_TRUE(d.error());

    // Past the end fails and keeps the position
    ASSERT_TRUE(d.seek(5000));
    EXPECT_FALSE(d.seek(frames + 1));
    ASSERT_EQ(d.readFrames(out.data(), 10), 10);
    const std::vector<float> at5000(whole.audio.samples.begin() + 5000 * 2, whole.audio.samples.begin() + 5010 * 2);
    EXPECT_TRUE(std::equal(at5000.begin(), at5000.end(), out.begin()));
}

TEST(Audio_Mp3AudioDecoderTests, TruncatedFilesDoNotCrash)
{
    const std::vector<uint8_t> bytes = readCodecTestFile("reference.mp3");
    Decoded intact = decodeBytes(bytes);
    ASSERT_TRUE(intact.ok);
    const size_t maxFrames = intact.audio.frames() + MP3_FRAME_SAMPLES;

    std::vector<size_t> sizes;
    for (size_t n = 0; n <= 80; ++n) {
        sizes.push_back(n);
    }
    for (size_t percent : { 10, 25, 50, 75, 99 }) {
        sizes.push_back(bytes.size() * percent / 100);
    }

    for (size_t n : sizes) {
        const std::vector<uint8_t> cut = truncated(bytes, n);
        Decoded d = decodeBytes(cut);
        if (d.ok) {
            EXPECT_LE(d.audio.frames(), maxFrames) << "size " << n;
        }
    }
}

TEST(Audio_Mp3AudioDecoderTests, GarbageIsRefusedOrBounded)
{
    // random bytes behind an ID3 header (fixed seed)
    std::vector<uint8_t> garbage = { 'I', 'D', '3', 4, 0, 0, 0, 0, 0, 0x10 };
    uint32_t state = 12345;
    for (size_t i = 0; i < 20000; ++i) {
        state = state * 1664525u + 1013904223u;
        garbage.push_back(static_cast<uint8_t>(state >> 24));
    }
    Decoded a = decodeBytes(garbage);
    if (a.ok) {
        EXPECT_LE(a.audio.frames(), garbage.size() * 100);
    }

    const std::vector<uint8_t> ff(64 * 1024, 0xFF);
    Decoded b = decodeBytes(ff);
    if (b.ok) {
        EXPECT_LE(b.audio.frames(), ff.size() * 100);
    }

    // a frame header alone
    const std::vector<uint8_t> header = { 0xFF, 0xFB, 0x90, 0x00 };
    EXPECT_FALSE(decodeBytes(header).ok);
}

TEST(Audio_Mp3AudioDecoderTests, TooLongIsRefused)
{
    const std::vector<uint8_t> bytes = readCodecTestFile("reference.mp3");
    RetVal<IAudioDecoderPtr> decoder = makeDecoder(bytes.data(), bytes.size());
    ASSERT_TRUE(decoder.ret);
    EXPECT_FALSE(decodeAll(*decoder.val, 1000).ret);
}

TEST(Audio_Mp3AudioDecoderTests, DecodesOnSeveralThreads)
{
    const std::vector<uint8_t> bytes = readCodecTestFile("reference.mp3");
    std::vector<Decoded> results(4);
    std::vector<std::thread> threads;
    for (size_t i = 0; i < results.size(); ++i) {
        threads.emplace_back([&bytes, &results, i]() {
            results[i] = decodeBytes(bytes);
        });
    }
    for (std::thread& t : threads) {
        t.join();
    }

    ASSERT_TRUE(results[0].ok);
    for (const Decoded& r : results) {
        ASSERT_TRUE(r.ok);
        EXPECT_EQ(r.audio.samples, results[0].audio.samples);
    }
}
