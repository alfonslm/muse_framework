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

#include "clipaudionode.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

#include "audio/common/audiosanitizer.h"

#include "log.h"

using namespace muse;
using namespace muse::audio;
using namespace muse::audio::engine;

namespace {
//! Windowed sinc interpolation.
//! SINC_HALF_WIDTH zero crossings on each side, tabulated with linear interpolation between entries.
constexpr int SINC_HALF_WIDTH = 16;
constexpr int SINC_TABLE_RESOLUTION = 512; // entries per zero crossing
constexpr int SINC_TABLE_SIZE = SINC_HALF_WIDTH * SINC_TABLE_RESOLUTION;

using SincTable = std::array<float, SINC_TABLE_SIZE + 2>;

SincTable makeSincTable()
{
    static constexpr double PI = 3.14159265358979323846;

    SincTable table {};
    for (int i = 0; i <= SINC_TABLE_SIZE; ++i) {
        const double x = static_cast<double>(i) / SINC_TABLE_RESOLUTION;
        const double sinc = i == 0 ? 1.0 : std::sin(PI * x) / (PI * x);
        // Blackman-Harris window, from its centre (x = 0) to its edge (x = SINC_HALF_WIDTH)
        const double w = PI * (1.0 + x / SINC_HALF_WIDTH);
        const double window = 0.35875 - 0.48829 * std::cos(w) + 0.14128 * std::cos(2.0 * w) - 0.01168 * std::cos(3.0 * w);
        table[i] = static_cast<float>(sinc * window);
    }
    table[SINC_TABLE_SIZE] = 0.f;
    table[SINC_TABLE_SIZE + 1] = 0.f;
    return table;
}

const SincTable& sincTable()
{
    static const SincTable table = makeSincTable();
    return table;
}

inline float windowedSinc(double x)
{
    const double pos = std::abs(x) * SINC_TABLE_RESOLUTION;
    if (pos >= SINC_TABLE_SIZE) {
        return 0.f;
    }
    const int idx = static_cast<int>(pos);
    const float frac = static_cast<float>(pos - idx);
    const SincTable& table = sincTable();
    return table[idx] + (table[idx + 1] - table[idx]) * frac;
}

constexpr double HALF_PI = 1.57079632679489661923;

//! Gain of a fade at x in [0, 1] (0 = silent end, 1 = full level)
inline float fadeGain(AudioFadeCurve curve, double x)
{
    switch (curve) {
    case AudioFadeCurve::EqualPower:
        return static_cast<float>(std::sin(x * HALF_PI));
    case AudioFadeCurve::Linear:
        break;
    }
    return static_cast<float>(x);
}

//! Clip gain is limited to this, so a crafted project cannot make a track blow up the mix
constexpr float MAX_GAIN_DB = 48.f;

//! Clip times beyond this are refused: they come from project files, and every time becomes a frame
//! index (a 64-bit integer) at up to 384 kHz. About 115 days.
constexpr double MAX_CLIP_SECS = 1e7;

//! Clips that overlap by less than this only touch (rounding of start + length); no cut fade.
//! Well under a sample at any rate the engine supports.
constexpr double TOUCH_EPSILON_SECS = 1e-7;

//! A clip whose length is within this of the rest of its source plays to the source's end
//! (the app stores seconds with 6 decimals)
constexpr double RINGS_OUT_EPSILON_SECS = 2e-6;

//! Output channels rendered through the resampler's scratch frame
constexpr audioch_t MAX_OUT_CHANNELS = 32;

inline bool isFiniteSecs(double v)
{
    return std::isfinite(v);
}

inline float sourceSample(const codec::PcmAudio& audio, samples_t frame, audioch_t outChannel, audioch_t outChannelCount)
{
    const audioch_t channels = audio.channels;
    const float* data = audio.samples.data() + frame * channels;

    if (channels == 1) {
        return data[0];
    }

    if (outChannelCount == 1) {
        return 0.5f * (data[0] + data[1]);
    }

    return outChannel < channels ? data[outChannel] : 0.f;
}

//! Adds one output frame, taken at `sourcePos` (in frames of the source), to `out`
void renderResampledFrame(const codec::PcmAudio& audio, samples_t frames, sample_rate_t outSampleRate, double sourcePos, float* out,
                          audioch_t outChannelCount)
{
    //! NOTE When the output rate is lower than the source's, the kernel is stretched
    //! so it also removes what the output rate cannot represent
    const double ratio = static_cast<double>(outSampleRate) / audio.sampleRate;
    const double cutoff = std::min(1.0, ratio);
    const double halfWidth = SINC_HALF_WIDTH / cutoff;

    const double firstPos = std::ceil(sourcePos - halfWidth);
    const double lastPos = std::floor(sourcePos + halfWidth);
    if (lastPos < 0.0 || firstPos > static_cast<double>(frames - 1)) {
        return;
    }

    const samples_t first = static_cast<samples_t>(std::max(0.0, firstPos));
    const samples_t last = std::min(frames - 1, static_cast<samples_t>(lastPos));
    const float gain = static_cast<float>(cutoff);

    for (samples_t frame = first; frame <= last; ++frame) {
        const float weight = gain * windowedSinc((static_cast<double>(frame) - sourcePos) * cutoff);
        for (audioch_t ch = 0; ch < outChannelCount; ++ch) {
            out[ch] += weight * sourceSample(audio, frame, ch, outChannelCount);
        }
    }
}
}

ClipAudioNode::ClipAudioNode(Sources sources, const AudioClipList& clips)
    : m_sources(std::move(sources))
{
    ONLY_AUDIO_ENGINE_THREAD;

    sincTable(); // built here, not in the processing thread

    setName("Source[audio clips]");
    setClips(clips);
}

ClipAudioNode::~ClipAudioNode()
{
    //! NOTE By now the node is out of the mixer: nothing reads the sets any more
    m_current.store(nullptr);
}

void ClipAudioNode::listenToClipChanges(async::Channel<AudioClipList> clipsChanged)
{
    ONLY_AUDIO_ENGINE_THREAD;

    clipsChanged.onReceive(this, [this](const AudioClipList& clips) {
        setClips(clips);
    });
}

// --- Clip lists (engine thread) ----------------------------------------------------------------

std::unique_ptr<ClipAudioNode::ClipSet> ClipAudioNode::buildClipSet(const AudioClipList& clips, uint64_t seq) const
{
    auto set = std::make_unique<ClipSet>();
    set->seq = seq;
    set->clips.reserve(clips.size());

    for (const AudioClip& clip : clips) {
        if (clip.muted) {
            continue; // neither sounds nor cuts other clips
        }

        const double start = clip.startSecs.raw();
        const double offset = clip.sourceOffsetSecs.raw();
        const double length = clip.lengthSecs.raw();
        if (!isFiniteSecs(start) || !isFiniteSecs(offset) || !isFiniteSecs(length) || !std::isfinite(clip.gainDb)
            || start < 0.0 || offset < 0.0 || length <= 0.0
            || start > MAX_CLIP_SECS || offset > MAX_CLIP_SECS || length > MAX_CLIP_SECS) {
            LOGE() << "audio clip with invalid times dropped";
            continue;
        }

        auto source = m_sources.find(clip.sourceKey);
        if (source == m_sources.end() || !source->second || !source->second->isValid() || source->second->frames() == 0) {
            LOGE() << "audio clip names no source of this track, dropped: " << clip.sourceKey;
            continue;
        }

        if (clip.stretchRatio != 1.0) {
            LOGW() << "audio clip stretch is not supported yet, played unstretched";
        }

        const codec::PcmAudio* audio = source->second.get();
        const double sourceDuration = static_cast<double>(audio->frames()) / audio->sampleRate;
        if (offset >= sourceDuration) {
            continue; // the window holds no audio
        }

        PlayableClip p;
        p.audio = audio;
        p.start = start;
        p.sourceOffset = offset;
        p.ownEnd = start + std::min(length, sourceDuration - offset);
        p.ringsOut = length >= sourceDuration - offset - RINGS_OUT_EPSILON_SECS;
        p.end = p.ownEnd;
        p.gain = static_cast<float>(std::pow(10.0, std::min(clip.gainDb, MAX_GAIN_DB) / 20.0));

        const double clipLength = p.ownEnd - p.start;
        auto checkedFade = [clipLength](const AudioFade& fade) {
            AudioFade f = fade;
            const double len = fade.length.raw();
            f.length = (std::isfinite(len) && len > 0.0) ? std::min(len, clipLength) : 0.0;
            return f;
        };
        p.fadeIn = checkedFade(clip.fadeIn);
        p.fadeOut = checkedFade(clip.fadeOut);

        set->clips.push_back(p);
    }

    //! Overlap rule: of two overlapping clips the later-starting one plays, the earlier one stops at
    //! its start (with a short fade). Same start: the later one in the list plays (stable sort).
    std::stable_sort(set->clips.begin(), set->clips.end(), [](const PlayableClip& a, const PlayableClip& b) {
        return a.start < b.start;
    });

    std::vector<PlayableClip> kept;
    kept.reserve(set->clips.size());
    for (size_t i = 0; i < set->clips.size(); ++i) {
        PlayableClip clip = set->clips[i];
        clip.tailLimit = std::numeric_limits<double>::infinity();
        if (i + 1 < set->clips.size()) {
            const double nextStart = set->clips[i + 1].start;
            clip.tailLimit = nextStart;
            if (nextStart < clip.end) {
                //! NOTE Two halves of a split meet where the left one ends, but `start + length` in
                //! doubles can land a hair past the right one's start: that is touching, not overlapping
                const bool overlaps = clip.end - nextStart > TOUCH_EPSILON_SECS;
                clip.end = nextStart;
                if (overlaps) {
                    clip.cutFade = std::min(OVERLAP_CUT_FADE_SECS, clip.end - clip.start);
                    clip.ringsOut = false;
                }
            }
        }
        if (clip.end > clip.start) {
            kept.push_back(clip);
        }
    }
    set->clips = std::move(kept);

    if (!set->clips.empty()) {
        set->firstSound = secs_t(set->clips.front().start);
    }

    return set;
}

void ClipAudioNode::setClips(const AudioClipList& clips)
{
    ONLY_AUDIO_ENGINE_THREAD;

    std::unique_ptr<ClipSet> set = buildClipSet(clips, ++m_lastSeq);
    const ClipSet* published = set.get();

    //! NOTE Kept until the processing thread has moved past it (freeRetiredSets)
    m_sets.push_back(std::move(set));
    m_current.store(published);

    freeRetiredSets();
}

void ClipAudioNode::freeRetiredSets()
{
    ONLY_AUDIO_ENGINE_THREAD;

    //! NOTE The newest set is never freed here. All atomics are seq_cst, so their operations have one
    //! total order S, and m_current was stored before this point.
    //! - Not processing: the last block (if any) ended before this load in S, so its reads happened
    //!   before; a block that starts later loads m_current after our store, so it reads the newest set.
    //!   Everything but the newest can go.
    //! - Processing: the processing thread stores the seq of each set it reads and only ever reads the
    //!   newest set, so it never again reads a set older than the seq it stored last.
    if (!m_processing.load()) {
        while (m_sets.size() > 1) {
            m_sets.pop_front();
        }
        return;
    }

    const uint64_t inUse = m_inUseSeq.load();
    while (m_sets.size() > 1 && m_sets.front()->seq < inUse) {
        m_sets.pop_front();
    }
}

std::optional<secs_t> ClipAudioNode::firstNoteTime() const
{
    ONLY_AUDIO_ENGINE_THREAD;

    if (m_sets.empty()) {
        return std::nullopt;
    }
    return m_sets.back()->firstSound;
}

// --- Transport (engine thread) -----------------------------------------------------------------

void ClipAudioNode::onOutputSpecChanged(const OutputSpec& spec)
{
    ONLY_AUDIO_ENGINE_THREAD;

    //! NOTE The position is counted in frames of the output sample rate: keep the time
    if (spec.sampleRate > 0 && m_positionSampleRate > 0 && spec.sampleRate != m_positionSampleRate) {
        const double secs = static_cast<double>(m_position.load()) / m_positionSampleRate;
        m_position.store(static_cast<samples_t>(std::llround(secs * spec.sampleRate)));
    }
    m_positionSampleRate = spec.sampleRate;
}

void ClipAudioNode::seek(const TimePosition& position, const bool)
{
    ONLY_AUDIO_ENGINE_THREAD;

    IF_ASSERT_FAILED(position.isValid()) {
        return;
    }

    const sample_rate_t sampleRate = m_outputSpec.sampleRate;
    if (sampleRate == 0 || position.sampleRate() == sampleRate) {
        m_position.store(position.samples());
    } else {
        m_position.store(static_cast<samples_t>(std::llround(position.time().raw() * sampleRate)));
    }

    freeRetiredSets();
}

void ClipAudioNode::flush()
{
    // nothing rings on after the transport stops
}

// --- Processing thread -------------------------------------------------------------------------
// No allocation, no lock, no IO below this line.

float ClipAudioNode::envelopeAt(const PlayableClip& clip, double t)
{
    float g = 1.f;

    const double fadeIn = clip.fadeIn.length.raw();
    if (fadeIn > 0.0) {
        const double x = (t - clip.start) / fadeIn;
        if (x < 1.0) {
            g *= fadeGain(clip.fadeIn.curve, std::max(0.0, x));
        }
    }

    const double fadeOut = clip.fadeOut.length.raw();
    if (fadeOut > 0.0) {
        const double x = (clip.ownEnd - t) / fadeOut;
        if (x < 1.0) {
            g *= fadeGain(clip.fadeOut.curve, std::max(0.0, x));
        }
    }

    if (clip.cutFade > 0.0) {
        const double x = (clip.end - t) / clip.cutFade;
        if (x < 1.0) {
            g *= static_cast<float>(std::max(0.0, x));
        }
    }

    return g;
}

void ClipAudioNode::renderClip(const PlayableClip& clip, samples_t blockStart, float* buffer, samples_t frames) const
{
    const audioch_t outChannels = m_outputSpec.audioChannelCount;
    const sample_rate_t outRate = m_outputSpec.sampleRate;
    const codec::PcmAudio& audio = *clip.audio;
    const sample_rate_t sourceRate = audio.sampleRate;
    const samples_t sourceFrames = audio.frames();

    const samples_t startFrame = static_cast<samples_t>(std::llround(clip.start * outRate));
    samples_t endFrame = static_cast<samples_t>(std::llround(clip.end * outRate));
    if (clip.ringsOut && sourceRate != outRate) {
        //! NOTE The converter's kernel reaches this far past the last source frame
        const double cutoff = std::min(1.0, static_cast<double>(outRate) / sourceRate);
        const double tailSecs = (SINC_HALF_WIDTH / cutoff + 1.0) / sourceRate;
        const double tailEnd = std::min(clip.end + tailSecs, clip.tailLimit);
        samples_t tailEndFrame = static_cast<samples_t>(std::ceil(tailEnd * outRate));
        if (std::isfinite(clip.tailLimit)) {
            tailEndFrame = std::min(tailEndFrame, static_cast<samples_t>(std::llround(clip.tailLimit * outRate)));
        }
        endFrame = std::max(endFrame, tailEndFrame);
    }
    const samples_t from = std::max(blockStart, startFrame);
    const samples_t to = std::min(blockStart + frames, endFrame);
    if (from >= to) {
        return;
    }

    const bool hasEnvelope = clip.fadeIn.length.raw() > 0.0 || clip.fadeOut.length.raw() > 0.0 || clip.cutFade > 0.0;

    if (sourceRate == outRate) {
        //! NOTE Whole frames: a clip at zero with gain 1 and no fades gives the source's samples exactly.
        //! The difference start - offset is rounded once (not each of the two), so the halves of a clip
        //! split off the sample grid take the same source frames as the unsplit clip
        const long long shift = std::llround((clip.start - clip.sourceOffset) * sourceRate); // signed (samples_t is not)
        for (samples_t f = from; f < to; ++f) {
            const long long src = static_cast<long long>(f) - shift;
            if (src < 0) {
                continue;
            }
            if (src >= static_cast<long long>(sourceFrames)) {
                break;
            }
            float g = clip.gain;
            if (hasEnvelope) {
                g *= envelopeAt(clip, static_cast<double>(f) / outRate);
            }
            float* out = buffer + (f - blockStart) * outChannels;
            for (audioch_t ch = 0; ch < outChannels; ++ch) {
                out[ch] = g * sourceSample(audio, static_cast<samples_t>(src), ch, outChannels); // clips never share a frame
            }
        }
        return;
    }

    if (outChannels > MAX_OUT_CHANNELS) {
        return;
    }

    const double step = static_cast<double>(sourceRate) / outRate;
    const double startFrames = clip.start * outRate;
    const double sourceOffsetFrames = clip.sourceOffset * sourceRate;

    std::array<float, MAX_OUT_CHANNELS> frame {};
    for (samples_t f = from; f < to; ++f) {
        const double t = static_cast<double>(f) / outRate;
        //! NOTE Counted from the clip's exact (unrounded) start, so the halves of a clip split off the
        //! sample grid take the same positions as the unsplit clip; a clip at zero with no offset takes
        //! exactly the positions a whole file always took (frame * step + 0.0)
        const double sourcePos = (static_cast<double>(f) - startFrames) * step + sourceOffsetFrames;

        std::fill(frame.begin(), frame.begin() + outChannels, 0.f);
        renderResampledFrame(audio, sourceFrames, outRate, sourcePos, frame.data(), outChannels);

        float g = clip.gain;
        if (hasEnvelope) {
            g *= envelopeAt(clip, t);
        }
        float* out = buffer + (f - blockStart) * outChannels;
        for (audioch_t ch = 0; ch < outChannels; ++ch) {
            out[ch] = g * frame[ch]; // clips never share a frame
        }
    }
}

void ClipAudioNode::doSelfProcess(float* buffer, samples_t samplesPerChannel)
{
    ONLY_AUDIO_PROC_THREAD;

    const audioch_t outChannels = m_outputSpec.audioChannelCount;
    const sample_rate_t outSampleRate = m_outputSpec.sampleRate;

    std::fill(buffer, buffer + samplesPerChannel * outChannels, 0.f);

    //! NOTE Pick up the newest list and say so (see freeRetiredSets for why this order)
    m_processing.store(true);
    const ClipSet* set = m_current.load();
    if (set) {
        m_inUseSeq.store(set->seq);
    }

    if (isModePlaying(m_mode) && set && outSampleRate != 0) {
        render(set, buffer, samplesPerChannel);
    }

    m_processing.store(false);
}

void ClipAudioNode::render(const ClipSet* set, float* buffer, samples_t samplesPerChannel)
{
    const sample_rate_t outSampleRate = m_outputSpec.sampleRate;

    samples_t position = m_position.load();
    const samples_t blockEnd = position + samplesPerChannel;

    for (const PlayableClip& clip : set->clips) {
        if (static_cast<samples_t>(std::llround(clip.start * outSampleRate)) >= blockEnd) {
            break; // sorted by start
        }
        renderClip(clip, position, buffer, samplesPerChannel);
    }

    //! NOTE If a seek came in meanwhile, its position wins
    m_position.compare_exchange_strong(position, blockEnd);
}

// --- AudioSourceNode ---------------------------------------------------------------------------

const AudioInputParams& ClipAudioNode::inputParams() const
{
    return m_params;
}

void ClipAudioNode::applyInputParams(const AudioInputParams& requiredParams)
{
    //! NOTE There is nothing to choose for audio clips; the params are kept as given
    if (m_params == requiredParams) {
        return;
    }

    m_params = requiredParams;
    m_paramsChanges.send(m_params);
}

async::Channel<AudioInputParams> ClipAudioNode::inputParamsChanged() const
{
    return m_paramsChanges;
}

void ClipAudioNode::prepareToPlay()
{
}

bool ClipAudioNode::readyToPlay() const
{
    return true;
}

async::Notification ClipAudioNode::readyToPlayChanged() const
{
    return m_readyToPlayChanged;
}

bool ClipAudioNode::hasPendingChunks() const
{
    return false;
}

void ClipAudioNode::processInput()
{
}

InputProcessingProgress ClipAudioNode::inputProcessingProgress() const
{
    return {};
}

void ClipAudioNode::clearCache()
{
}
