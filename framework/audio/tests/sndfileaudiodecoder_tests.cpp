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

#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "audio/common/audiotypes.h"

#include "audio/engine/internal/codecs/audiodecoder.h"
#include "audio/engine/internal/codecs/sndfileaudiodecoder.h"

#include "codectestutils.h"

using namespace muse;
using namespace muse::audio;
using namespace muse::audio::codec;
using namespace muse::audio::tests;

namespace {
struct Decoded {
    bool ok = false;
    PcmAudio audio;
};

Decoded decodeFile(const std::vector<uint8_t>& bytes, size_t maxSamples = WavDecoder::MAX_SAMPLES)
{
    Decoded d;
    RetVal<IAudioDecoderPtr> decoder = makeDecoder(bytes.data(), bytes.size());
    if (!decoder.ret) {
        return d;
    }
    RetVal<PcmAudio> pcm = decodeAll(*decoder.val, maxSamples);
    if (!pcm.ret) {
        return d;
    }
    d.ok = true;
    d.audio = pcm.val;
    return d;
}

IAudioDecoderPtr openFile(const std::vector<uint8_t>& bytes)
{
    RetVal<IAudioDecoderPtr> decoder = makeDecoder(bytes.data(), bytes.size());
    return decoder.ret ? std::move(decoder.val) : nullptr;
}

//! Reads `frames` frames from `decoder` in one call
std::vector<float> readSome(IAudioDecoder& decoder, size_t frames)
{
    std::vector<float> out(frames * decoder.info().channels);
    const samples_t got = decoder.readFrames(out.data(), frames);
    out.resize(static_cast<size_t>(got) * decoder.info().channels);
    return out;
}

const char* const LOSSY_FILES[] = { "reference.ogg", "reference.opus" };
const char* const ALL_FILES[] = { "reference.flac", "reference.ogg", "reference.opus" };
}

class Audio_SndFileAudioDecoderTests : public ::testing::Test
{
};

TEST_F(Audio_SndFileAudioDecoderTests, DetectFormat)
{
    auto detect = [](const char* name) {
        const std::vector<uint8_t> b = readCodecTestFile(name);
        EXPECT_FALSE(b.empty()) << name;
        return detectFormat(b.data(), b.size());
    };
    EXPECT_EQ(detect("reference.flac"), AudioFileFormat::Flac);
    EXPECT_EQ(detect("reference.ogg"), AudioFileFormat::OggVorbis);
    EXPECT_EQ(detect("reference.opus"), AudioFileFormat::OggOpus);
    EXPECT_EQ(detect("reference.wav"), AudioFileFormat::Wav);
    EXPECT_EQ(detect("reference.mp3"), AudioFileFormat::Mp3);

    const std::vector<uint8_t> garbage(64, 0x5A);
    EXPECT_EQ(detectFormat(garbage.data(), garbage.size()), AudioFileFormat::Unknown);

    // An Ogg page whose first packet is neither Vorbis nor Opus
    std::vector<uint8_t> ogg = readCodecTestFile("reference.ogg");
    ASSERT_GT(ogg.size(), 40u);
    const size_t packetPos = 27 + ogg[26];
    std::memcpy(ogg.data() + packetPos, "\x7f""FLAC??", 8);
    EXPECT_EQ(detectFormat(ogg.data(), ogg.size()), AudioFileFormat::Unknown);

    EXPECT_EQ(detectFormat(garbage.data(), 0), AudioFileFormat::Unknown);
    EXPECT_EQ(detectFormat(nullptr, 100), AudioFileFormat::Unknown);
}

TEST_F(Audio_SndFileAudioDecoderTests, FlacIsBitExact)
{
    const std::pair<const char*, const char*> pairs[] = { { "reference.flac", "reference.wav" }, { "mono24.flac", "mono24.wav" } };
    for (const auto& pair : pairs) {
        const Decoded flac = decodeFile(readCodecTestFile(pair.first));
        const Decoded wav = decodeFile(readCodecTestFile(pair.second));
        ASSERT_TRUE(flac.ok) << pair.first;
        ASSERT_TRUE(wav.ok) << pair.second;
        EXPECT_EQ(flac.audio.sampleRate, wav.audio.sampleRate) << pair.first;
        EXPECT_EQ(flac.audio.channels, wav.audio.channels) << pair.first;
        EXPECT_EQ(flac.audio.samples.size(), wav.audio.samples.size()) << pair.first;
        EXPECT_EQ(maxDifference(wav.audio.samples, flac.audio.samples), 0.f) << pair.first;
    }
}

TEST_F(Audio_SndFileAudioDecoderTests, VorbisAndOpusMatchReference)
{
    const Decoded ref = decodeFile(readCodecTestFile("reference.wav"));
    ASSERT_TRUE(ref.ok);

    for (const char* name : LOSSY_FILES) {
        const Decoded d = decodeFile(readCodecTestFile(name));
        ASSERT_TRUE(d.ok) << name;
        EXPECT_EQ(d.audio.sampleRate, 48000u) << name;
        EXPECT_EQ(d.audio.channels, 2) << name;
        const size_t frames = d.audio.samples.size() / 2;
        // MEASURED: exactly 24000 frames for both; libsndfile removes the Opus pre-skip and end padding
        EXPECT_EQ(frames, 24000u) << name;
        EXPECT_EQ(bestLag(ref.audio.samples, d.audio.samples, 2, 0, 2000), 0) << name;
        // MEASURED worst difference (click edges included): Vorbis 0.065, Opus 0.018; lag 0 on both
        // channels. Tolerances have more than twice that as margin.
        const float tolerance = std::strcmp(name, "reference.ogg") == 0 ? 0.15f : 0.05f;
        EXPECT_LT(maxDifference(ref.audio.samples, d.audio.samples), tolerance) << name;
    }
}

TEST_F(Audio_SndFileAudioDecoderTests, OddChunksEqualDecodeAll)
{
    for (const char* name : ALL_FILES) {
        const std::vector<uint8_t> bytes = readCodecTestFile(name);
        const Decoded all = decodeFile(bytes);
        ASSERT_TRUE(all.ok) << name;

        IAudioDecoderPtr decoder = openFile(bytes);
        ASSERT_TRUE(decoder) << name;
        const size_t channels = decoder->info().channels;
        std::vector<float> chunked;
        const size_t sizes[] = { 7, 1000, 333 };
        for (size_t i = 0;; ++i) {
            const size_t want = sizes[i % 3];
            const std::vector<float> part = readSome(*decoder, want);
            chunked.insert(chunked.end(), part.begin(), part.end());
            if (part.size() < want * channels) {
                break;
            }
        }
        EXPECT_TRUE(decoder->error()) << name;
        ASSERT_EQ(chunked.size(), all.audio.samples.size()) << name;
        EXPECT_EQ(maxDifference(chunked, all.audio.samples), 0.f) << name;
    }
}

TEST_F(Audio_SndFileAudioDecoderTests, Seek)
{
    for (const char* name : ALL_FILES) {
        const std::vector<uint8_t> bytes = readCodecTestFile(name);
        const Decoded all = decodeFile(bytes);
        ASSERT_TRUE(all.ok) << name;

        IAudioDecoderPtr decoder = openFile(bytes);
        ASSERT_TRUE(decoder) << name;
        const size_t channels = decoder->info().channels;
        const samples_t frames = decoder->info().frames;
        ASSERT_EQ(frames, all.audio.samples.size() / channels) << name;

        ASSERT_TRUE(decoder->seek(12000)) << name;
        const std::vector<float> part = readSome(*decoder, 1000);
        ASSERT_EQ(part.size(), 1000 * channels) << name;
        const std::vector<float> expected(all.audio.samples.begin() + 12000 * channels,
                                          all.audio.samples.begin() + 13000 * channels);
        // MEASURED: seeking is sample-exact for all three formats
        EXPECT_EQ(maxDifference(expected, part), 0.f) << name;

        // Seeking to the end is valid and reads nothing
        EXPECT_TRUE(decoder->seek(frames)) << name;
        EXPECT_EQ(readSome(*decoder, 10).size(), 0u) << name;
        EXPECT_TRUE(decoder->error()) << name;

        // Past the end fails and the position stays
        ASSERT_TRUE(decoder->seek(100)) << name;
        EXPECT_FALSE(decoder->seek(frames + 1)) << name;
        const std::vector<float> after = readSome(*decoder, 50);
        const std::vector<float> expected100(all.audio.samples.begin() + 100 * channels,
                                             all.audio.samples.begin() + 150 * channels);
        EXPECT_EQ(maxDifference(expected100, after), 0.f) << name;
    }
}

TEST_F(Audio_SndFileAudioDecoderTests, DamagedInput)
{
    const Decoded ref = decodeFile(readCodecTestFile("reference.wav"));
    ASSERT_TRUE(ref.ok);

    for (const char* name : ALL_FILES) {
        const std::vector<uint8_t> bytes = readCodecTestFile(name);
        ASSERT_FALSE(bytes.empty()) << name;

        std::vector<size_t> sizes;
        for (size_t n = 0; n <= 80; ++n) {
            sizes.push_back(n);
        }
        for (size_t percent : { 10, 25, 50, 75, 99 }) {
            sizes.push_back(bytes.size() * percent / 100);
        }

        for (size_t n : sizes) {
            const std::vector<uint8_t> cut = truncated(bytes, n);
            RetVal<IAudioDecoderPtr> decoder = makeDecoder(cut.data(), cut.size());
            if (!decoder.ret) {
                continue;
            }
            RetVal<PcmAudio> pcm = decodeAll(*decoder.val);
            if (!pcm.ret) {
                continue;
            }
            EXPECT_LE(pcm.val.samples.size(), ref.audio.samples.size()) << name << " cut at " << n;
        }

        // Garbage behind the magic: no crash, whatever the outcome
        std::vector<uint8_t> garbled = bytes;
        uint32_t x = 12345;
        for (size_t i = 4; i < garbled.size(); ++i) {
            x = x * 1664525u + 1013904223u;
            garbled[i] = static_cast<uint8_t>(x >> 24);
        }
        (void)decodeFile(garbled);
    }

    // FLAC cut inside the audio: the STREAMINFO total says it is short
    const std::vector<uint8_t> flac = readCodecTestFile("reference.flac");
    const std::vector<uint8_t> half = truncated(flac, flac.size() / 2);
    RetVal<IAudioDecoderPtr> decoder = makeDecoder(half.data(), half.size());
    ASSERT_TRUE(decoder.ret) << "the header is whole, so the file opens";
    EXPECT_FALSE(decodeAll(*decoder.val).ret) << "and the shortfall is found while reading";
}

TEST_F(Audio_SndFileAudioDecoderTests, FlacHugeTotalIsRefused)
{
    std::vector<uint8_t> flac = readCodecTestFile("reference.flac");
    ASSERT_GT(flac.size(), 8u + 18u);
    // STREAMINFO starts at byte 8; the 36-bit total is the low 4 bits of byte 13 and bytes 14..17
    flac[8 + 13] |= 0x0F;
    flac[8 + 14] = flac[8 + 15] = flac[8 + 16] = flac[8 + 17] = 0xFF;

    RetVal<IAudioDecoderPtr> decoder = makeDecoder(flac.data(), flac.size());
    ASSERT_TRUE(decoder.ret) << "the header is valid, it only lies about the length";
    EXPECT_FALSE(decodeAll(*decoder.val).ret);

    // Refused before a single block is read
    RetVal<IAudioDecoderPtr> again = makeDecoder(flac.data(), flac.size());
    ASSERT_TRUE(again.ret);
    int blocks = 0;
    EXPECT_FALSE(readAll(*again.val, [&blocks](const float*, samples_t) { ++blocks; return true; }).ret);
    EXPECT_EQ(blocks, 0);

    // The intact file with a small limit
    const std::vector<uint8_t> intact = readCodecTestFile("reference.flac");
    RetVal<IAudioDecoderPtr> ok = makeDecoder(intact.data(), intact.size());
    ASSERT_TRUE(ok.ret);
    EXPECT_FALSE(decodeAll(*ok.val, 1000).ret);
}

TEST_F(Audio_SndFileAudioDecoderTests, ThreadsDecodeIndependently)
{
    const std::vector<uint8_t> flac = readCodecTestFile("reference.flac");
    const std::vector<uint8_t> opus = readCodecTestFile("reference.opus");
    const Decoded flacRef = decodeFile(flac);
    const Decoded opusRef = decodeFile(opus);
    ASSERT_TRUE(flacRef.ok);
    ASSERT_TRUE(opusRef.ok);

    std::vector<Decoded> results(4);
    std::vector<std::thread> threads;
    for (size_t i = 0; i < results.size(); ++i) {
        threads.emplace_back([&, i]() {
            for (int round = 0; round < 5; ++round) {
                results[i] = decodeFile(i % 2 == 0 ? flac : opus);
            }
        });
    }
    for (std::thread& t : threads) {
        t.join();
    }

    for (size_t i = 0; i < results.size(); ++i) {
        const Decoded& ref = i % 2 == 0 ? flacRef : opusRef;
        ASSERT_TRUE(results[i].ok) << i;
        EXPECT_EQ(results[i].audio.samples.size(), ref.audio.samples.size()) << i;
        EXPECT_EQ(maxDifference(ref.audio.samples, results[i].audio.samples), 0.f) << i;
    }
}

namespace {
//! reference.flac with its STREAMINFO total samples set to `total` (36 bits)
std::vector<uint8_t> flacWithTotal(uint64_t total)
{
    std::vector<uint8_t> flac = readCodecTestFile("reference.flac");
    EXPECT_GT(flac.size(), 8u + 18u);
    // STREAMINFO starts at byte 8; the total is the low 4 bits of byte 13 and bytes 14..17
    flac[8 + 13] = static_cast<uint8_t>((flac[8 + 13] & 0xF0) | ((total >> 32) & 0x0F));
    flac[8 + 14] = static_cast<uint8_t>(total >> 24);
    flac[8 + 15] = static_cast<uint8_t>(total >> 16);
    flac[8 + 16] = static_cast<uint8_t>(total >> 8);
    flac[8 + 17] = static_cast<uint8_t>(total);
    return flac;
}
}

TEST_F(Audio_SndFileAudioDecoderTests, FlacLongerThanTheDurationCapIsRefusedBeforeReading)
{
    // 3 hours at 48 kHz stereo: under the 2^30-float cap, over MAX_DECODED_SECONDS (2 hours)
    const std::vector<uint8_t> flac = flacWithTotal(uint64_t(3) * 3600 * 48000);
    RetVal<IAudioDecoderPtr> decoder = makeDecoder(flac.data(), flac.size());
    ASSERT_TRUE(decoder.ret);
    int blocks = 0;
    EXPECT_FALSE(readAll(*decoder.val, [&blocks](const float*, samples_t) { ++blocks; return true; }).ret);
    EXPECT_EQ(blocks, 0);
}

TEST_F(Audio_SndFileAudioDecoderTests, FlacWithUnknownTotalIsReadToItsEnd)
{
    // A STREAMINFO total of 0 means "unknown" (a stream written live): the decoder reads to the end
    const std::vector<uint8_t> flac = flacWithTotal(0);
    RetVal<IAudioDecoderPtr> decoder = makeDecoder(flac.data(), flac.size());
    ASSERT_TRUE(decoder.ret) << decoder.ret.text();
    EXPECT_EQ(decoder.val->info().frames, 0u);

    RetVal<PcmAudio> pcm = decodeAll(*decoder.val);
    ASSERT_TRUE(pcm.ret) << pcm.ret.text();
    const Decoded ref = decodeFile(readCodecTestFile("reference.wav"));
    ASSERT_TRUE(ref.ok);
    EXPECT_EQ(pcm.val.frames(), ref.audio.frames());
    EXPECT_EQ(maxDifference(ref.audio.samples, pcm.val.samples), 0.f);
}

TEST_F(Audio_SndFileAudioDecoderTests, OggWithALyingLastGranuleDoesNotCrashOrOverread)
{
    // The length of an Ogg stream comes from its last page's granule position: make it lie, both
    // huge and small, for Vorbis and Opus
    for (const char* name : { "reference.ogg", "reference.opus" }) {
        const std::vector<uint8_t> original = readCodecTestFile(name);
        size_t lastPage = std::string::npos;
        for (size_t i = 0; i + 27 <= original.size(); ++i) {
            if (std::memcmp(original.data() + i, "OggS", 4) == 0) {
                lastPage = i;
            }
        }
        ASSERT_NE(lastPage, std::string::npos) << name;

        for (uint64_t granule : { uint64_t(1) << 62, uint64_t(1000), uint64_t(0) }) {
            std::vector<uint8_t> bytes = original;
            for (int b = 0; b < 8; ++b) {
                bytes[lastPage + 6 + b] = static_cast<uint8_t>(granule >> (8 * b));
            }
            RetVal<IAudioDecoderPtr> decoder = makeDecoder(bytes.data(), bytes.size());
            if (!decoder.ret) {
                continue; // refused on open: fine
            }
            RetVal<PcmAudio> pcm = decodeAll(*decoder.val);
            if (pcm.ret) {
                // Whatever the page says, never more audio than the stream holds (plus one packet)
                EXPECT_LE(pcm.val.frames(), 24000u + 5760u) << name << " granule " << granule;
            }
        }
    }
}

TEST_F(Audio_SndFileAudioDecoderTests, MoreThanTwoChannelsAreDecoded)
{
    const std::vector<uint8_t> flac = readCodecTestFile("sixchannels.flac");
    RetVal<IAudioDecoderPtr> decoder = makeDecoder(flac.data(), flac.size());
    ASSERT_TRUE(decoder.ret) << decoder.ret.text();
    EXPECT_EQ(decoder.val->info().channels, 6u);
    EXPECT_EQ(decoder.val->info().sampleRate, 48000u);

    RetVal<PcmAudio> pcm = decodeAll(*decoder.val);
    ASSERT_TRUE(pcm.ret);
    EXPECT_EQ(pcm.val.channels, 6u);
    EXPECT_EQ(pcm.val.frames(), 4800u);
}
