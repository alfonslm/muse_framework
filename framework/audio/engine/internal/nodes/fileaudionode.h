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

#pragma once

#include <atomic>
#include <memory>

#include "audiosourcenode.h"

#include "../codecs/wavdecoder.h"

namespace muse::audio::engine {
//! NOTE A source that plays decoded audio (one whole file) from time zero,
//! following the transport the same way the event sources do:
//! seek() sets the position, process() moves it forward while the mode is a playing one.
//!
//! The audio is held in memory and never changes after construction.
//! Sample rate conversion is done in process() by position (no state between blocks),
//! so seek, loop and offline export are exact, and nothing is allocated when the output spec changes.
class FileAudioNode : public AudioSourceNode
{
public:
    explicit FileAudioNode(std::shared_ptr<const codec::PcmAudio> audio);

    secs_t duration() const;

    void seek(const TimePosition& position, const bool flushSound = true) override;
    void flush() override;

    const AudioInputParams& inputParams() const override;
    void applyInputParams(const AudioInputParams& requiredParams) override;
    async::Channel<AudioInputParams> inputParamsChanged() const override;

    void prepareToPlay() override;
    bool readyToPlay() const override;
    async::Notification readyToPlayChanged() const override;

    bool hasPendingChunks() const override;
    void processInput() override;
    InputProcessingProgress inputProcessingProgress() const override;

    void clearCache() override;

private:
    void onOutputSpecChanged(const OutputSpec& spec) override;

    void doSelfProcess(float* buffer, samples_t samplesPerChannel) override;

    float sourceSample(samples_t frame, audioch_t outChannel, audioch_t outChannelCount) const;
    //! Adds one output frame, taken at `sourcePos` (in frames of the file), to `out`
    void renderResampledFrame(double sourcePos, float* out, audioch_t outChannelCount) const;

    std::shared_ptr<const codec::PcmAudio> m_audio;
    samples_t m_frames = 0;

    //! Frames of the output sample rate since time zero.
    //! Written by seek() (engine thread) and by process() (processing thread).
    std::atomic<samples_t> m_position = 0;
    sample_rate_t m_positionSampleRate = 0;

    AudioInputParams m_params;
    async::Channel<AudioInputParams> m_paramsChanges;
    async::Notification m_readyToPlayChanged;
};

using FileAudioNodePtr = std::shared_ptr<FileAudioNode>;
}
