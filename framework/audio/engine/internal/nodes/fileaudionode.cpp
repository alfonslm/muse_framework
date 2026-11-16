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

#include "fileaudionode.h"

#include <algorithm>
#include <array>
#include <cmath>

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
}

FileAudioNode::FileAudioNode(std::shared_ptr<const codec::PcmAudio> audio)
    : m_audio(std::move(audio))
{
    ONLY_AUDIO_ENGINE_THREAD;

    if (m_audio && m_audio->isValid()) {
        m_frames = m_audio->frames();
    }

    sincTable(); // built here, not in the processing thread

    setName("Source[audio file]");
}

secs_t FileAudioNode::duration() const
{
    if (m_frames == 0) {
        return 0.0;
    }
    return static_cast<double>(m_frames) / m_audio->sampleRate;
}

void FileAudioNode::onOutputSpecChanged(const OutputSpec& spec)
{
    ONLY_AUDIO_ENGINE_THREAD;

    //! NOTE The position is counted in frames of the output sample rate: keep the time
    if (spec.sampleRate > 0 && m_positionSampleRate > 0 && spec.sampleRate != m_positionSampleRate) {
        const double secs = static_cast<double>(m_position.load()) / m_positionSampleRate;
        m_position.store(static_cast<samples_t>(std::llround(secs * spec.sampleRate)));
    }
    m_positionSampleRate = spec.sampleRate;
}

void FileAudioNode::seek(const TimePosition& position, const bool)
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
}

void FileAudioNode::flush()
{
    // nothing rings on after the transport stops
}

inline float FileAudioNode::sourceSample(samples_t frame, audioch_t outChannel, audioch_t outChannelCount) const
{
    const audioch_t channels = m_audio->channels;
    const float* data = m_audio->samples.data() + frame * channels;

    if (channels == 1) {
        return data[0];
    }

    if (outChannelCount == 1) {
        return 0.5f * (data[0] + data[1]);
    }

    return outChannel < channels ? data[outChannel] : 0.f;
}

void FileAudioNode::renderResampledFrame(double sourcePos, float* out, audioch_t outChannelCount) const
{
    //! NOTE When the output rate is lower than the file's, the kernel is stretched
    //! so it also removes what the output rate cannot represent
    const double ratio = static_cast<double>(m_outputSpec.sampleRate) / m_audio->sampleRate;
    const double cutoff = std::min(1.0, ratio);
    const double halfWidth = SINC_HALF_WIDTH / cutoff;

    const double firstPos = std::ceil(sourcePos - halfWidth);
    const double lastPos = std::floor(sourcePos + halfWidth);
    if (lastPos < 0.0 || firstPos > static_cast<double>(m_frames - 1)) {
        return;
    }

    const samples_t first = static_cast<samples_t>(std::max(0.0, firstPos));
    const samples_t last = std::min(m_frames - 1, static_cast<samples_t>(lastPos));
    const float gain = static_cast<float>(cutoff);

    for (samples_t frame = first; frame <= last; ++frame) {
        const float weight = gain * windowedSinc((static_cast<double>(frame) - sourcePos) * cutoff);
        for (audioch_t ch = 0; ch < outChannelCount; ++ch) {
            out[ch] += weight * sourceSample(frame, ch, outChannelCount);
        }
    }
}

void FileAudioNode::doSelfProcess(float* buffer, samples_t samplesPerChannel)
{
    ONLY_AUDIO_PROC_THREAD;

    const audioch_t outChannels = m_outputSpec.audioChannelCount;
    const sample_rate_t outSampleRate = m_outputSpec.sampleRate;

    std::fill(buffer, buffer + samplesPerChannel * outChannels, 0.f);

    if (!isModePlaying(m_mode) || m_frames == 0 || outSampleRate == 0) {
        return;
    }

    samples_t position = m_position.load();

    const sample_rate_t sourceSampleRate = m_audio->sampleRate;

    if (sourceSampleRate == outSampleRate) {
        const samples_t available = position < m_frames ? m_frames - position : 0;
        const samples_t count = std::min(samplesPerChannel, available);
        for (samples_t i = 0; i < count; ++i) {
            for (audioch_t ch = 0; ch < outChannels; ++ch) {
                buffer[i * outChannels + ch] = sourceSample(position + i, ch, outChannels);
            }
        }
    } else {
        const double step = static_cast<double>(sourceSampleRate) / outSampleRate;
        for (samples_t i = 0; i < samplesPerChannel; ++i) {
            renderResampledFrame(static_cast<double>(position + i) * step, buffer + i * outChannels, outChannels);
        }
    }

    //! NOTE If a seek came in meanwhile, its position wins
    m_position.compare_exchange_strong(position, position + samplesPerChannel);
}

const AudioInputParams& FileAudioNode::inputParams() const
{
    return m_params;
}

void FileAudioNode::applyInputParams(const AudioInputParams& requiredParams)
{
    //! NOTE There is nothing to choose for a file; the params are kept as given
    if (m_params == requiredParams) {
        return;
    }

    m_params = requiredParams;
    m_paramsChanges.send(m_params);
}

async::Channel<AudioInputParams> FileAudioNode::inputParamsChanged() const
{
    return m_paramsChanges;
}

void FileAudioNode::prepareToPlay()
{
}

bool FileAudioNode::readyToPlay() const
{
    return true;
}

async::Notification FileAudioNode::readyToPlayChanged() const
{
    return m_readyToPlayChanged;
}

bool FileAudioNode::hasPendingChunks() const
{
    return false;
}

void FileAudioNode::processInput()
{
}

InputProcessingProgress FileAudioNode::inputProcessingProgress() const
{
    return {};
}

void FileAudioNode::clearCache()
{
}
