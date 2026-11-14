/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * MuseScore Studio
 * Music Composition & Notation
 *
 * Copyright (C) 2025 MuseScore Limited and others
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
#include <mutex>

#include "audiosourcenode.h"
#include "playbackstatetimeline.h"

#include "global/async/asyncable.h"
#include "global/modularity/ioc.h"
#include "mpe/events.h"

#include "audio/common/audiotypes.h"
#include "../iaudiofactory.h"

namespace muse::audio::engine {
class EventAudioNode : public AudioSourceNode, public async::Asyncable
{
    GlobalInject<IAudioFactory> audioFactory;

public:
    using OnOffStreamEventsReceived = std::function<void ()>;

    explicit EventAudioNode(TrackId trackId, const mpe::PlaybackData& playbackData, OnOffStreamEventsReceived onOffStreamReceived);

    ~EventAudioNode() override;

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
    struct SynthCtx
    {
        ProcessMode mode = ProcessMode::Undefined;
        TimePosition playbackPosition;
        bool isValid() const { return mode != ProcessMode::Undefined; }
    };

    //! NOTE From the synth's playback data, which follows score changes
    std::optional<secs_t> firstNoteTime() const override;

    void onModeChanged(const ProcessMode mode) override;
    void onEnabledChanged(bool enabled) override;
    void onOutputSpecChanged(const OutputSpec& spec) override;

    void doSelfProcess(float* buffer, samples_t samplesPerChannel) override;

    //! NOTE Processes a block under On/Off and Live/Standby markings: applies their gain, and doesn't
    //! run the synth at all while the instrument is off or in standby (see PlaybackStateTimeline)
    void processWithPlaybackStates(float* buffer, samples_t samplesPerChannel);

    //! NOTE Engine thread: new markings, e.g. after a score change. Picked up by the audio thread
    void setPlaybackStateTimeline(PlaybackStateTimelinePtr timeline);

    void setupSource();
    SynthCtx currentSynthCtx() const;
    void restoreSynthCtx(const SynthCtx& ctx);

    TrackId m_trackId = -1;
    mpe::PlaybackData m_playbackData;
    synth::ISynthesizerPtr m_synth = nullptr;
    AudioInputParams m_params;
    async::Channel<AudioInputParams> m_paramsChanges;

    // Markings
    PlaybackStateTimelinePtr m_engineTimeline;  // engine thread
    std::mutex m_timelineMutex;
    PlaybackStateTimelinePtr m_pendingTimeline; // guarded by m_timelineMutex
    std::atomic<bool> m_timelineChanged = false;
    PlaybackStateTimelinePtr m_timeline;        // audio thread
    bool m_sleeping = false;                    // the synth isn't processed, see processWithPlaybackStates()
    TimePosition m_sleepPosition;               // where the synth would be now, while sleeping
    bool m_ringingOut = true;                   // an instrument in standby after its note hasn't gone silent yet
    samples_t m_silentSamples = 0;
};

using EventAudioNodePtr = std::shared_ptr<EventAudioNode>;
}
