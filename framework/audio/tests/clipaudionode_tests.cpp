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
#include <cmath>
#include <limits>
#include <memory>
#include <vector>

// pragma-pack matches rpcpacker_tests.cpp to avoid an ODR segfault at static init.
#pragma pack(push, 1)
#include "audio/common/audiotypes.h"
#pragma pack(pop)

#include "audio/engine/internal/nodes/clipaudionode.h"

//! NOTE The unit under test is compiled into this file: this test binary does not link the audio engine
#include "audio/engine/internal/nodes/clipaudionode.cpp"

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

//! One clip: the whole audio from time zero (what the IODevice overload of addTrack makes)
ClipAudioNodePtr makeNode(std::shared_ptr<const PcmAudio> audio, sample_rate_t outSampleRate, audioch_t outChannels = 2)
{
    AudioClip clip;
    clip.sourceKey = "a";
    clip.lengthSecs = audio->sampleRate ? static_cast<double>(audio->frames()) / audio->sampleRate : 0.0;
    ClipAudioNodePtr node = std::make_shared<ClipAudioNode>(ClipAudioNode::Sources { { "a", audio } }, AudioClipList { clip });
    node->setOutputSpec(OutputSpec { outSampleRate, 512, outChannels });
    node->setMode(ProcessMode::Idle);
    node->seek(TimePosition::zero(outSampleRate));
    return node;
}

std::vector<float> render(ClipAudioNodePtr node, samples_t frames, samples_t blockSize)
{
    const audioch_t channels = node->outputSpec().audioChannelCount;
    std::vector<float> out(frames * channels, 99.f);
    for (samples_t done = 0; done < frames; done += blockSize) {
        node->process(out.data() + done * channels, std::min(blockSize, frames - done));
    }
    return out;
}
}

TEST(Audio_ClipAudioNodeTests, PlaysTheFileFromTimeZero)
{
    ClipAudioNodePtr node = makeNode(makeRamp(48000, 2, 1000), 48000);
    // The one-pass export never processes a source without a first note
    ASSERT_TRUE(node->firstNoteTime().has_value());
    EXPECT_DOUBLE_EQ(node->firstNoteTime()->raw(), 0.0);

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

TEST(Audio_ClipAudioNodeTests, SilentAndStandingStillWhenNotPlaying)
{
    ClipAudioNodePtr node = makeNode(makeRamp(48000, 2, 1000), 48000);

    // Idle: silence, and the position does not move
    std::vector<float> out = render(node, 300, 100);
    for (float v : out) {
        ASSERT_FLOAT_EQ(v, 0.f);
    }

    node->setMode(ProcessMode::Playing);
    out = render(node, 10, 10);
    EXPECT_FLOAT_EQ(out[0], 1.f); // still at frame 0
}

TEST(Audio_ClipAudioNodeTests, SeekIsExact)
{
    ClipAudioNodePtr node = makeNode(makeRamp(48000, 2, 48000), 48000);
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

TEST(Audio_ClipAudioNodeTests, ChannelLayouts)
{
    // Mono file on a stereo output: the same signal on both sides
    ClipAudioNodePtr mono = makeNode(makeRamp(48000, 1, 100), 48000, 2);
    mono->setMode(ProcessMode::Playing);
    std::vector<float> out = render(mono, 10, 10);
    EXPECT_FLOAT_EQ(out[4 * 2], 5.f);
    EXPECT_FLOAT_EQ(out[4 * 2 + 1], 5.f);

    // Stereo file on a mono output: the average (left = n, right = -n)
    ClipAudioNodePtr stereo = makeNode(makeRamp(48000, 2, 100), 48000, 1);
    stereo->setMode(ProcessMode::Playing);
    out = render(stereo, 10, 10);
    EXPECT_FLOAT_EQ(out[4], 0.f);
}

TEST(Audio_ClipAudioNodeTests, ConvertsTheSampleRate)
{
    const double hz = 1000.0;

    for (const auto& [fileRate, outRate] : std::vector<std::pair<sample_rate_t, sample_rate_t> > {
        { 44100, 48000 }, { 48000, 44100 }, { 96000, 44100 }, { 22050, 48000 } }) {
        ClipAudioNodePtr node = makeNode(makeSine(fileRate, hz, fileRate), outRate, 1); // one second
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

TEST(Audio_ClipAudioNodeTests, KeepsTheTimeWhenTheOutputRateChanges)
{
    // 1 kHz, played to 0.25 s at 48 kHz; then the output switches to 44.1 kHz (as an export does)
    ClipAudioNodePtr node = makeNode(makeSine(48000, 1000.0, 48000), 48000, 1);
    node->setMode(ProcessMode::Playing);
    render(node, 12000, 480);

    node->setOutputSpec(OutputSpec { 44100, 512, 1 });
    const std::vector<float> out = render(node, 441, 441);

    for (samples_t f = 0; f < 441; ++f) {
        const double expected = std::sin(2.0 * PI * 1000.0 * (0.25 + static_cast<double>(f) / 44100.0));
        ASSERT_NEAR(out[f], expected, 2e-3) << "frame " << f;
    }
}

TEST(Audio_ClipAudioNodeTests, EmptyAudio)
{
    ClipAudioNodePtr node = makeNode(std::make_shared<PcmAudio>(), 48000);
    node->setMode(ProcessMode::Playing);

    const std::vector<float> out = render(node, 64, 64);
    for (float v : out) {
        ASSERT_FLOAT_EQ(v, 0.f);
    }
    EXPECT_TRUE(node->readyToPlay());
    EXPECT_FALSE(node->hasPendingChunks());
    EXPECT_FALSE(node->firstNoteTime().has_value());
}

// --- Clip lists ---------------------------------------------------------------------------------

namespace {
//! Mono audio: frame f has the value base + slope * f
std::shared_ptr<const PcmAudio> makeValues(sample_rate_t sampleRate, samples_t frames, float base, float slope)
{
    auto audio = std::make_shared<PcmAudio>();
    audio->sampleRate = sampleRate;
    audio->channels = 1;
    audio->samples.resize(frames);
    for (samples_t f = 0; f < frames; ++f) {
        audio->samples[f] = base + slope * static_cast<float>(f);
    }
    return audio;
}

AudioClip makeClip(const std::string& key, double start, double length, double offset = 0.0)
{
    AudioClip c;
    c.sourceKey = key;
    c.startSecs = start;
    c.sourceOffsetSecs = offset;
    c.lengthSecs = length;
    return c;
}

AudioFade makeFade(double length, AudioFadeCurve curve)
{
    AudioFade f;
    f.length = length;
    f.curve = curve;
    return f;
}

//! Mono output, playing, at time zero
ClipAudioNodePtr makeClipNode(ClipAudioNode::Sources sources, const AudioClipList& clips, sample_rate_t outRate)
{
    ClipAudioNodePtr node = std::make_shared<ClipAudioNode>(std::move(sources), clips);
    node->setOutputSpec(OutputSpec { outRate, 512, 1 });
    node->setMode(ProcessMode::Playing);
    node->seek(TimePosition::zero(outRate));
    return node;
}

samples_t frameAt(double secs, sample_rate_t rate)
{
    return static_cast<samples_t>(std::llround(secs * rate));
}

float fadeValue(AudioFadeCurve curve, double x)
{
    return static_cast<float>(curve == AudioFadeCurve::EqualPower ? std::sin(x * PI / 2.0) : x);
}

void checkThreeClips(sample_rate_t outRate, sample_rate_t srcRate, bool exact)
{
    ClipAudioNode::Sources sources {
        { "a", makeValues(srcRate, 2000, 100.f, 1.f) },
        { "b", makeValues(srcRate, 2000, 10000.f, 1.f) },
    };
    AudioClipList clips { makeClip("a", 0.010, 0.005), makeClip("b", 0.020, 0.004, 0.002), makeClip("a", 0.030, 0.005) };
    clips[2].muted = true;

    ClipAudioNodePtr node = makeClipNode(sources, clips, outRate);
    ASSERT_TRUE(node->firstNoteTime().has_value());
    EXPECT_DOUBLE_EQ(node->firstNoteTime()->raw(), 0.010);

    const samples_t total = frameAt(0.040, outRate);
    const std::vector<float> out = render(node, total, 100);

    struct Region {
        samples_t start, end;
        float base;
        samples_t offsetFrames;
    };
    const Region regions[] = {
        { frameAt(0.010, outRate), frameAt(0.010 + 0.005, outRate), 100.f, 0 },
        { frameAt(0.020, outRate), frameAt(0.020 + 0.004, outRate), 10000.f, frameAt(0.002, srcRate) },
    };

    for (samples_t f = 0; f < total; ++f) {
        const Region* inside = nullptr;
        for (const Region& r : regions) {
            if (f >= r.start && f < r.end) {
                inside = &r;
            }
        }
        if (!inside) {
            ASSERT_EQ(out[f], 0.f) << "frame " << f << " at " << outRate;
        } else if (exact) {
            const float expected = inside->base + static_cast<float>(f - inside->start + inside->offsetFrames);
            ASSERT_EQ(out[f], expected) << "frame " << f << " at " << outRate;
        } else {
            ASSERT_GT(std::abs(out[f]), 1.f) << "frame " << f << " at " << outRate;
        }
    }
}

void checkFadeCurves(AudioFadeCurve in, AudioFadeCurve outCurve)
{
    ClipAudioNode::Sources sources { { "a", makeValues(48000, 48000, 1.f, 0.f) } };
    AudioClip clip = makeClip("a", 0.0, 0.1);
    clip.fadeIn = makeFade(0.01, in);
    clip.fadeOut = makeFade(0.01, outCurve);

    ClipAudioNodePtr node = makeClipNode(sources, { clip }, 48000);
    const std::vector<float> out = render(node, 5000, 128);

    const double end = 0.1;
    for (samples_t f = 0; f < 4800; ++f) {
        const double t = static_cast<double>(f) / 48000.0;
        double expected = 1.0;
        if (t < 0.01) {
            expected *= fadeValue(in, t / 0.01);
        }
        if (end - t < 0.01) {
            expected *= fadeValue(outCurve, (end - t) / 0.01);
        }
        ASSERT_NEAR(out[f], expected, 1e-6) << "frame " << f;
    }
    EXPECT_NEAR(out[0], 0.f, 1e-6);                                              // first frame of the fade in
    EXPECT_NEAR(out[4799], fadeValue(outCurve, (1.0 / 48000.0) / 0.01), 1e-6);   // last frame of the clip
    EXPECT_NEAR(out[2400], 1.f, 1e-6);                                           // middle
    for (samples_t f = 4800; f < 5000; ++f) {
        ASSERT_EQ(out[f], 0.f) << "frame " << f;
    }
}

void checkOverlapOrder(bool reversed)
{
    ClipAudioNode::Sources sources { { "a", makeValues(48000, 48000, 1.f, 0.f) }, { "b", makeValues(48000, 48000, 0.5f, 0.f) } };
    AudioClipList clips { makeClip("a", 0.0, 0.1), makeClip("b", 0.05, 0.1) };
    if (reversed) {
        std::swap(clips[0], clips[1]);
    }

    ClipAudioNodePtr node = makeClipNode(sources, clips, 48000);
    const std::vector<float> out = render(node, 8000, 100);

    const double bStart = 0.05;
    const samples_t bFrame = frameAt(bStart, 48000);
    const samples_t fadeStart = bFrame - frameAt(ClipAudioNode::OVERLAP_CUT_FADE_SECS, 48000);
    ASSERT_EQ(bFrame, 2400);
    ASSERT_EQ(fadeStart, 2160);

    for (samples_t f = 0; f < fadeStart; ++f) {
        ASSERT_EQ(out[f], 1.f) << "frame " << f;
    }
    for (samples_t f = fadeStart; f < bFrame; ++f) {
        const double t = static_cast<double>(f) / 48000.0;
        ASSERT_NEAR(out[f], (bStart - t) / ClipAudioNode::OVERLAP_CUT_FADE_SECS, 1e-6) << "frame " << f;
    }
    for (samples_t f = bFrame; f < frameAt(0.15, 48000); ++f) {
        ASSERT_EQ(out[f], 0.5f) << "frame " << f;
    }
    for (samples_t f = frameAt(0.15, 48000); f < 8000; ++f) {
        ASSERT_EQ(out[f], 0.f) << "frame " << f;
    }
}

//! A clip list with a ramp source plus fades and gain, rendered from zero and from a seek into it
void checkSeekIntoClip(sample_rate_t sourceRate, sample_rate_t outRate)
{
    ClipAudioNode::Sources sources { { "a", makeValues(sourceRate, sourceRate, 1.f, 1.f) } };
    AudioClip a = makeClip("a", 0.1, 0.2, 0.05);
    a.gainDb = -3.f;
    a.fadeIn = makeFade(0.02, AudioFadeCurve::EqualPower);
    a.fadeOut = makeFade(0.02, AudioFadeCurve::Linear);
    AudioClip b = makeClip("a", 0.5, 0.1);
    const AudioClipList clips { a, b };

    const samples_t total = 12000;
    const std::vector<float> full = render(makeClipNode(sources, clips, outRate), total, 100);

    // The reference is not silent where the seeks land
    ASSERT_NE(full[10000], 0.f);

    for (samples_t k : { samples_t(5280), samples_t(10000), samples_t(100) }) {
        ClipAudioNodePtr node = makeClipNode(sources, clips, outRate);
        node->seek(TimePosition::fromSamples(k, outRate));
        const std::vector<float> part = render(node, 1000, 37);
        const std::vector<float> expected(full.begin() + k, full.begin() + k + 1000);
        EXPECT_EQ(part, expected) << "seek to " << k << ", " << sourceRate << " -> " << outRate;
    }
}
}

TEST(Audio_ClipAudioNodeTests, ThreeClips)
{
    checkThreeClips(48000, 48000, true);
    checkThreeClips(44100, 44100, true);
    checkThreeClips(44100, 48000, false);
}

TEST(Audio_ClipAudioNodeTests, FirstNoteTimeSkipsMutedClips)
{
    ClipAudioNode::Sources sources { { "a", makeValues(48000, 2000, 1.f, 0.f) } };
    AudioClipList clips { makeClip("a", 0.001, 0.005), makeClip("a", 0.010, 0.005) };
    clips[0].muted = true;

    ClipAudioNodePtr node = makeClipNode(sources, clips, 48000);
    ASSERT_TRUE(node->firstNoteTime().has_value());
    EXPECT_DOUBLE_EQ(node->firstNoteTime()->raw(), 0.010);

    clips[1].muted = true;
    node->setClips(clips);
    EXPECT_FALSE(node->firstNoteTime().has_value());

    node->setClips({});
    EXPECT_FALSE(node->firstNoteTime().has_value());
}

TEST(Audio_ClipAudioNodeTests, GainAndFades)
{
    // Gain only
    {
        ClipAudioNode::Sources sources { { "a", makeValues(48000, 48000, 1.f, 0.f) } };
        AudioClip clip = makeClip("a", 0.0, 0.1);
        clip.gainDb = -6.f;
        ClipAudioNodePtr node = makeClipNode(sources, { clip }, 48000);
        const std::vector<float> out = render(node, 5000, 256);
        const float expected = static_cast<float>(std::pow(10.0, -6.0 / 20.0));
        for (samples_t f = 0; f < 4800; ++f) {
            ASSERT_NEAR(out[f], expected, 1e-6) << "frame " << f;
        }
        for (samples_t f = 4800; f < 5000; ++f) {
            ASSERT_EQ(out[f], 0.f) << "frame " << f;
        }
    }

    checkFadeCurves(AudioFadeCurve::Linear, AudioFadeCurve::EqualPower);
    checkFadeCurves(AudioFadeCurve::EqualPower, AudioFadeCurve::Linear);
    checkFadeCurves(AudioFadeCurve::Linear, AudioFadeCurve::Linear);
    checkFadeCurves(AudioFadeCurve::EqualPower, AudioFadeCurve::EqualPower);
}

TEST(Audio_ClipAudioNodeTests, FadeLongerThanTheClipIsClamped)
{
    ClipAudioNode::Sources sources { { "a", makeValues(48000, 48000, 1.f, 0.f) } };
    AudioClip clip = makeClip("a", 0.0, 0.01);
    clip.fadeIn = makeFade(1.0, AudioFadeCurve::Linear);
    clip.fadeOut = makeFade(1.0, AudioFadeCurve::EqualPower);

    ClipAudioNodePtr node = makeClipNode(sources, { clip }, 48000);
    const std::vector<float> out = render(node, 600, 64);

    for (samples_t f = 0; f < 480; ++f) {
        ASSERT_TRUE(std::isfinite(out[f])) << "frame " << f;
        ASSERT_GE(out[f], 0.f) << "frame " << f;
        ASSERT_LE(out[f], 1.f) << "frame " << f;

        // Both fades are as long as the clip
        const double t = static_cast<double>(f) / 48000.0;
        const double expected = (t / 0.01) * std::sin(((0.01 - t) / 0.01) * PI / 2.0);
        ASSERT_NEAR(out[f], expected, 1e-6) << "frame " << f;
    }
    for (samples_t f = 480; f < 600; ++f) {
        ASSERT_EQ(out[f], 0.f) << "frame " << f;
    }
}

TEST(Audio_ClipAudioNodeTests, OverlapLaterStartWins)
{
    checkOverlapOrder(false);
    checkOverlapOrder(true);
}

TEST(Audio_ClipAudioNodeTests, OverlapSameStartLaterInListWins)
{
    ClipAudioNode::Sources sources { { "a", makeValues(48000, 48000, 1.f, 0.f) }, { "b", makeValues(48000, 48000, 0.5f, 0.f) } };

    for (const bool aFirst : { true, false }) {
        AudioClipList clips { makeClip("a", 0.0, 0.1), makeClip("b", 0.0, 0.1) };
        if (!aFirst) {
            std::swap(clips[0], clips[1]);
        }
        const float winner = aFirst ? 0.5f : 1.f;

        ClipAudioNodePtr node = makeClipNode(sources, clips, 48000);
        const std::vector<float> out = render(node, 5000, 100);
        for (samples_t f = 0; f < 4800; ++f) {
            ASSERT_EQ(out[f], winner) << "frame " << f << ", a first: " << aFirst;
        }
        for (samples_t f = 4800; f < 5000; ++f) {
            ASSERT_EQ(out[f], 0.f) << "frame " << f;
        }
    }
}

TEST(Audio_ClipAudioNodeTests, MutedLaterClipDoesNotCutTheEarlierOne)
{
    ClipAudioNode::Sources sources { { "a", makeValues(48000, 48000, 1.f, 0.f) }, { "b", makeValues(48000, 48000, 0.5f, 0.f) } };

    for (const double bStart : { 0.05, 0.0 }) {
        AudioClipList clips { makeClip("a", 0.0, 0.1), makeClip("b", bStart, 0.1) };
        clips[1].muted = true;

        ClipAudioNodePtr node = makeClipNode(sources, clips, 48000);
        const std::vector<float> out = render(node, 5000, 100);
        for (samples_t f = 0; f < 4800; ++f) {
            ASSERT_EQ(out[f], 1.f) << "frame " << f << ", muted clip at " << bStart;
        }
        for (samples_t f = 4800; f < 5000; ++f) {
            ASSERT_EQ(out[f], 0.f) << "frame " << f;
        }
    }
}

TEST(Audio_ClipAudioNodeTests, SwapEveryBlock)
{
    ClipAudioNode::Sources sources { { "a", makeValues(48000, 6 * 48000, 1.f, 0.f) } };

    AudioClip long1 = makeClip("a", 0.0, 3.0);
    long1.gainDb = -6.f;
    AudioClip c2 = makeClip("a", 1.0, 1.0);
    c2.fadeIn = makeFade(0.01, AudioFadeCurve::Linear);
    AudioClip c3 = makeClip("a", 3.0, 2.5);
    c3.fadeOut = makeFade(0.1, AudioFadeCurve::EqualPower);

    const std::vector<AudioClipList> lists {
        { long1 },
        { c2, c3 },
        {},
    };

    ClipAudioNodePtr node = makeClipNode(sources, lists[0], 48000);
    const samples_t block = 64;
    std::vector<float> buf(block, 0.f);
    for (size_t i = 0; i < 5 * 48000 / block; ++i) {
        node->setClips(lists[i % lists.size()]);
        node->process(buf.data(), block);
        for (float v : buf) {
            ASSERT_TRUE(std::isfinite(v));
        }
    }

    // The position is at 5 s now. A clip over it is heard...
    AudioClip last = makeClip("a", 3.0, 3.0);
    node->setClips({ last });
    node->process(buf.data(), block);
    for (float v : buf) {
        ASSERT_EQ(v, 1.f);
    }

    // ... and the empty list is silent
    node->setClips({});
    node->process(buf.data(), block);
    for (float v : buf) {
        ASSERT_EQ(v, 0.f);
    }

    node->setClips({ last });
    node->process(buf.data(), block);
    for (float v : buf) {
        ASSERT_EQ(v, 1.f);
    }
}

TEST(Audio_ClipAudioNodeTests, SeekIntoClip)
{
    checkSeekIntoClip(48000, 48000);
    checkSeekIntoClip(44100, 48000);
}

TEST(Audio_ClipAudioNodeTests, InvalidClipsAreDropped)
{

    ClipAudioNode::Sources sources { { "a", makeValues(48000, 4800, 1.f, 0.f) } }; // 0.1 s

    // NOTE NaN or infinite secs_t values cannot be made in a debug build (secs_t asserts), so only NaN gain is tried
    // Invalid clips start inside the valid one's window (0.03 - 0.04 s): none may cut it
    std::vector<std::pair<std::string, AudioClip> > invalid;
    invalid.push_back({ "negative start", makeClip("a", -0.035, 0.1) });
    invalid.push_back({ "negative offset", makeClip("a", 0.035, 0.01, -0.001) });
    invalid.push_back({ "zero length", makeClip("a", 0.035, 0.0) });
    invalid.push_back({ "negative length", makeClip("a", 0.035, -0.01) });
    invalid.push_back({ "unknown source", makeClip("nope", 0.035, 0.01) });
    invalid.push_back({ "offset past the end", makeClip("a", 0.035, 0.01, 0.2) });
    invalid.push_back({ "offset at the end", makeClip("a", 0.035, 0.01, 0.1) });
    AudioClip nanGain = makeClip("a", 0.035, 0.01);
    nanGain.gainDb = std::numeric_limits<float>::quiet_NaN();
    invalid.push_back({ "NaN gain", nanGain });

    const samples_t total = 4800;

    for (const auto& [name, clip] : invalid) {
        // Alone: silence, and no first note
        ClipAudioNodePtr node = makeClipNode(sources, { clip }, 48000);
        EXPECT_FALSE(node->firstNoteTime().has_value()) << name;
        const std::vector<float> out = render(node, total, 256);
        for (samples_t f = 0; f < total; ++f) {
            ASSERT_EQ(out[f], 0.f) << name << ", frame " << f;
        }
    }

    // Together with a valid clip: it plays, whole and uncut
    AudioClipList all { makeClip("a", 0.03, 0.01) };
    for (const auto& [name, clip] : invalid) {
        all.push_back(clip);
    }
    ClipAudioNodePtr node = makeClipNode(sources, all, 48000);
    ASSERT_TRUE(node->firstNoteTime().has_value());
    EXPECT_DOUBLE_EQ(node->firstNoteTime()->raw(), 0.03);
    const std::vector<float> out = render(node, total, 256);
    for (samples_t f = 0; f < total; ++f) {
        const bool inside = f >= frameAt(0.03, 48000) && f < frameAt(0.03 + 0.01, 48000);
        ASSERT_EQ(out[f], inside ? 1.f : 0.f) << "frame " << f;
    }
}

TEST(Audio_ClipAudioNodeTests, LengthIsCutToTheSource)
{
    ClipAudioNode::Sources sources { { "a", makeValues(48000, 4800, 1.f, 0.f) } }; // 0.1 s

    // Whole source from 0.02 s
    {
        ClipAudioNodePtr node = makeClipNode(sources, { makeClip("a", 0.02, 5.0) }, 48000);
        const std::vector<float> out = render(node, 9600, 200);
        for (samples_t f = 0; f < 9600; ++f) {
            ASSERT_EQ(out[f], (f >= 960 && f < 960 + 4800) ? 1.f : 0.f) << "frame " << f;
        }
    }

    // From the middle of the source: 0.05 s are left
    {
        ClipAudioNodePtr node = makeClipNode(sources, { makeClip("a", 0.02, 5.0, 0.05) }, 48000);
        const std::vector<float> out = render(node, 9600, 200);
        for (samples_t f = 0; f < 9600; ++f) {
            ASSERT_EQ(out[f], (f >= 960 && f < 960 + 2400) ? 1.f : 0.f) << "frame " << f;
        }
    }
}

TEST(Audio_ClipAudioNodeTests, WholeFileRingsOutWhenResampledUnlessAClipFollows)
{
    // A sine that stops abruptly: the converter's tail rings on after the last source frame,
    // as it did when a track was one whole file (the one-clip case must not change)
    auto sine = makeSine(22050, 1000.0, 2205); // 0.1 s
    const sample_rate_t outRate = 48000;
    const samples_t endFrame = static_cast<samples_t>(std::llround(0.1 * outRate));

    ClipAudioNodePtr node = makeNode(sine, outRate, 1);
    node->setMode(ProcessMode::Playing);
    std::vector<float> out = render(node, endFrame + 200, 100);

    float tail = 0.f;
    for (samples_t f = endFrame; f < endFrame + 20; ++f) {
        tail = std::max(tail, std::abs(out[f]));
    }
    EXPECT_GT(tail, 1e-4f);
    for (samples_t f = endFrame + 120; f < endFrame + 200; ++f) {
        ASSERT_FLOAT_EQ(out[f], 0.f) << "frame " << f;
    }

    // A clip that starts where the file ends: the tail stops there
    auto silence = std::make_shared<PcmAudio>();
    silence->sampleRate = 22050;
    silence->channels = 1;
    silence->samples.assign(2205, 0.f);

    AudioClip first;
    first.sourceKey = "a";
    first.lengthSecs = 0.1;
    AudioClip second;
    second.sourceKey = "b";
    second.startSecs = 0.1;
    second.lengthSecs = 0.1;
    ClipAudioNodePtr two = std::make_shared<ClipAudioNode>(ClipAudioNode::Sources { { "a", sine }, { "b", silence } },
                                                           AudioClipList { first, second });
    two->setOutputSpec(OutputSpec { outRate, 512, 1 });
    two->setMode(ProcessMode::Playing);
    two->seek(TimePosition::zero(outRate));
    out = render(two, endFrame + 200, 100);
    for (samples_t f = endFrame; f < endFrame + 200; ++f) {
        ASSERT_FLOAT_EQ(out[f], 0.f) << "frame " << f;
    }
}

TEST(Audio_ClipAudioNodeTests, ResampledWholeFileTakesTheSamePositionsAsBefore)
{
    // What a whole-file track rendered before clip lists: output frame f reads the source at f * step.
    // A one-clip list at zero must give exactly those samples, so old projects export the same.
    auto sine = makeSine(44100, 997.0, 44100);
    const sample_rate_t outRate = 48000;
    const double step = 44100.0 / outRate;
    const samples_t frames = 48000 + 100; // past the end, into the tail

    std::vector<float> expected(frames, 0.f);
    for (samples_t f = 0; f < frames; ++f) {
        renderResampledFrame(*sine, sine->frames(), outRate, static_cast<double>(f) * step, &expected[f], 1);
    }

    ClipAudioNodePtr node = makeNode(sine, outRate, 1);
    node->setMode(ProcessMode::Playing);
    const std::vector<float> out = render(node, frames, 512);

    for (samples_t f = 0; f < frames; ++f) {
        ASSERT_EQ(out[f], expected[f]) << "frame " << f;
    }
}

TEST(Audio_ClipAudioNodeTests, OldListsAreFreedWhenTheTrackIsNotProcessed)
{
    // A muted track's source is not processed at all: lists sent meanwhile must not pile up
    ClipAudioNodePtr node = makeNode(makeRamp(48000, 1, 4800), 48000, 1);
    for (int i = 0; i < 100; ++i) {
        AudioClip clip;
        clip.sourceKey = "a";
        clip.startSecs = 0.001 * i;
        clip.lengthSecs = 0.05;
        node->setClips({ clip });
    }
    EXPECT_EQ(node->keptClipSets(), 1u);

    // And the list that was kept is the newest one
    node->setMode(ProcessMode::Playing);
    const samples_t start = static_cast<samples_t>(std::llround(0.099 * 48000));
    const std::vector<float> out = render(node, start + 10, 512);
    EXPECT_FLOAT_EQ(out[start - 1], 0.f);
    EXPECT_FLOAT_EQ(out[start], 1.f);
}

TEST(Audio_ClipAudioNodeTests, HugeTimesAreRefused)
{
    auto audio = makeRamp(48000, 1, 4800);
    for (double huge : { 1e300, 1e8 }) {
        AudioClip clip;
        clip.sourceKey = "a";
        clip.lengthSecs = 0.05;

        AudioClip lateStart = clip;
        lateStart.startSecs = huge;
        AudioClip bigOffset = clip;
        bigOffset.sourceOffsetSecs = huge;
        AudioClip longClip = clip;
        longClip.lengthSecs = huge;

        for (const AudioClip& c : { lateStart, bigOffset, longClip }) {
            ClipAudioNodePtr node = std::make_shared<ClipAudioNode>(ClipAudioNode::Sources { { "a", audio } }, AudioClipList { c });
            EXPECT_FALSE(node->firstNoteTime().has_value()) << huge;
        }
    }
}
