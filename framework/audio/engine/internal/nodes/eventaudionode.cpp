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

#include "eventaudionode.h"

#include <algorithm>
#include <cmath>

#include "audio/common/audiosanitizer.h"

#include "log.h"

using namespace muse;
using namespace muse::audio;
using namespace muse::audio::engine;
using namespace muse::audio::synth;
using namespace muse::mpe;

EventAudioNode::EventAudioNode(TrackId trackId, const mpe::PlaybackData& playbackData,
                               OnOffStreamEventsReceived onOffStreamReceived)
    : m_trackId(trackId), m_playbackData(playbackData)
{
    ONLY_AUDIO_ENGINE_THREAD;

    if (onOffStreamReceived) {
        m_playbackData.offStream.onReceive(this, [onOffStreamReceived](const PlaybackEventsMap&,
                                                                       bool) {
            onOffStreamReceived();
        });
    }

    setPlaybackStateTimeline(PlaybackStateTimeline::build(m_playbackData.originEvents));

    m_playbackData.mainStream.onReceive(this, [this](const PlaybackEventsMap& events, const DynamicAutomationLayers&) {
        setPlaybackStateTimeline(PlaybackStateTimeline::build(events));
    });
}

EventAudioNode::~EventAudioNode()
{
    m_playbackData.offStream.disconnect(this);
    m_playbackData.mainStream.disconnect(this);
}

void EventAudioNode::setPlaybackStateTimeline(PlaybackStateTimelinePtr timeline)
{
    ONLY_AUDIO_ENGINE_THREAD;

    if (!timeline && !m_engineTimeline) {
        return;
    }

    m_engineTimeline = timeline;

    std::lock_guard lock(m_timelineMutex);
    m_pendingTimeline = std::move(timeline);
    m_timelineChanged = true;
}

//! Time of the first note in the synth's playback data, or nullopt if there are no notes.
std::optional<secs_t> EventAudioNode::firstNoteTime() const
{
    ONLY_AUDIO_ENGINE_THREAD;

    if (!m_synth) {
        return std::nullopt;
    }

    //! NOTE The synth's playback data is kept up to date with the score (main stream changes)
    const mpe::PlaybackEventsMap& events = m_synth->playbackData().originEvents;
    std::optional<timestamp_t> firstNote;
    for (auto it = events.cbegin(); it != events.cend(); ++it) {
        const bool hasNote = std::any_of(it->second.cbegin(), it->second.cend(), [](const mpe::PlaybackEvent& event) {
            return std::holds_alternative<mpe::NoteEvent>(event);
        });

        if (hasNote) {
            firstNote = it->first;
            break;
        }
    }

    //! NOTE A Live (or On) marking can make the instrument sound before its first note, e.g. a drone
    if (m_engineTimeline) {
        if (const std::optional<timestamp_t> firstSound = m_engineTimeline->firstSoundWithoutNote()) {
            firstNote = firstNote.has_value() ? std::min(*firstNote, *firstSound) : *firstSound;
        }
    }

    if (!firstNote.has_value()) {
        return std::nullopt;
    }

    return muse::usecs_to_secs(muse::usecs_t(std::max<timestamp_t>(0, *firstNote)));
}

void EventAudioNode::onModeChanged(const ProcessMode mode)
{
    ONLY_AUDIO_ENGINE_THREAD;

    if (!m_synth) {
        return;
    }

    //! NOTE A disabled (muted) source isn't processed, so its synth stays idle and gets the actual
    //! mode once the source is enabled again (see onEnabledChanged). Activating a plugin synth can be
    //! expensive (e.g. Kontakt), and exporting parts one by one switches every track to
    //! PlayingOffline and back once per part, with all tracks but the part's own muted
    if (!m_enabled) {
        return;
    }

    m_synth->setMode(mode);
    m_synth->flushSound();
}

void EventAudioNode::onEnabledChanged(bool enabled)
{
    ONLY_AUDIO_ENGINE_THREAD;

    if (!m_synth) {
        return;
    }

    const ProcessMode synthMode = (enabled && m_mode != ProcessMode::Undefined) ? m_mode : ProcessMode::Idle;
    if (m_synth->mode() == synthMode) {
        return;
    }

    m_synth->setMode(synthMode);
    m_synth->flushSound();
}

void EventAudioNode::onOutputSpecChanged(const OutputSpec& spec)
{
    ONLY_AUDIO_ENGINE_THREAD;
    if (!m_synth) {
        return;
    }

    m_synth->setOutputSpec(spec);
}

void EventAudioNode::doSelfProcess(float* buffer, samples_t samplesPerChannel)
{
    ONLY_AUDIO_PROC_THREAD;
    if (!m_synth) {
        return;
    }

    if (m_timelineChanged.load()) {
        //! NOTE Not waiting for the lock: the new markings are picked up on the next block instead
        std::unique_lock lock(m_timelineMutex, std::try_to_lock);
        if (lock.owns_lock()) {
            m_timeline = m_pendingTimeline;
            m_timelineChanged = false;
        }
    }

    if (m_timeline || m_sleeping) {
        processWithPlaybackStates(buffer, samplesPerChannel);
        return;
    }

    m_synth->process(buffer, samplesPerChannel);
}

void EventAudioNode::processWithPlaybackStates(float* buffer, samples_t samplesPerChannel)
{
    const sample_rate_t sampleRate = m_outputSpec.sampleRate;
    const size_t channelCount = m_outputSpec.audioChannelCount;
    IF_ASSERT_FAILED(sampleRate > 0) {
        return;
    }

    const TimePosition position = m_sleeping ? m_sleepPosition : m_synth->playbackPosition();
    auto toUsecs = [sampleRate](samples_t samples) {
        return static_cast<timestamp_t>(static_cast<double>(samples) * 1000000.0 / sampleRate);
    };

    const timestamp_t from = toUsecs(position.samples());
    const timestamp_t to = toUsecs(position.samples() + samplesPerChannel);

    //! NOTE "needed": the markings or notes require processing; otherwise an idle instrument is
    //! only processed while it may still be ringing out after its last note
    const bool needed = !m_timeline || m_timeline->isAwake(from, to, false);
    const bool awake = needed || m_timeline->isAwake(from, to, m_ringingOut);

    if (!awake) {
        if (!m_sleeping) {
            m_sleeping = true;
            m_sleepPosition = position;
        }

        m_sleepPosition.forward(samplesPerChannel);
        std::fill(buffer, buffer + samplesPerChannel * channelCount, 0.f);
        return;
    }

    if (m_sleeping) {
        //! NOTE The synth stood still while asleep: move it to where it would be now
        m_sleeping = false;
        m_synth->setPlaybackPosition(m_sleepPosition);
        m_synth->flushSound();
    }

    m_synth->process(buffer, samplesPerChannel);

    if (!m_timeline) {
        return;
    }

    if (m_timeline->affectsGain(from, to)) {
        for (samples_t i = 0; i < samplesPerChannel; ++i) {
            const float gain = m_timeline->gainAt(toUsecs(position.samples() + i));
            float* frame = buffer + i * channelCount;
            for (size_t c = 0; c < channelCount; ++c) {
                frame[c] *= gain;
            }
        }
    }

    if (needed) {
        m_ringingOut = true;
        m_silentSamples = 0;
        return;
    }

    //! NOTE Ringing out after the last note: sleep once the output has stayed below about -100 dBFS for half a second
    constexpr float SILENCE_THRESHOLD = 0.00001f;
    const bool silent = std::all_of(buffer, buffer + samplesPerChannel * channelCount, [](float s) {
        return std::fabs(s) < SILENCE_THRESHOLD;
    });

    m_silentSamples = silent ? m_silentSamples + samplesPerChannel : 0;
    if (m_silentSamples >= static_cast<samples_t>(sampleRate / 2)) {
        m_ringingOut = false;
    }
}

void EventAudioNode::seek(const TimePosition& position, const bool flushSound)
{
    ONLY_AUDIO_ENGINE_THREAD;

    IF_ASSERT_FAILED(m_synth) {
        return;
    }

    //! NOTE A sleeping synth wakes up at the new position (see processWithPlaybackStates)
    if (m_sleeping) {
        m_sleepPosition = position;
    }

    m_ringingOut = true;
    m_silentSamples = 0;

    if (m_synth->playbackPosition() == position) {
        return;
    }

    m_synth->setPlaybackPosition(position);

    if (flushSound) {
        m_synth->flushSound();
    }
}

void EventAudioNode::flush()
{
    ONLY_AUDIO_ENGINE_THREAD;

    IF_ASSERT_FAILED(m_synth) {
        return;
    }

    m_synth->flushSound();
}

const AudioInputParams& EventAudioNode::inputParams() const
{
    return m_params;
}

void EventAudioNode::applyInputParams(const AudioInputParams& requiredParams)
{
    IF_ASSERT_FAILED(m_outputSpec.isValid()) {
        return;
    }

    if (m_params.isValid() && m_params == requiredParams) {
        return;
    }

    SynthCtx ctx = currentSynthCtx();

    if (m_synth) {
        m_playbackData = m_synth->playbackData();
    }

    RetVal<synth::ISynthesizerPtr> synth = audioFactory()->makeSynth(m_trackId, requiredParams, m_playbackData.setupData);

    if (!synth.ret) {
        synth = audioFactory()->makeDefaultSynth(m_trackId);
        IF_ASSERT_FAILED(synth.val) {
            LOGE() << "Default synth not found!";
            return;
        }
    }

    m_synth = synth.val;

    m_synth->paramsChanged().onReceive(this, [this](const AudioInputParams& params) {
        m_paramsChanges.send(params);
    });

    setupSource();

    if (ctx.isValid()) {
        restoreSynthCtx(ctx);
    } else {
        m_synth->setMode(ProcessMode::Idle);
    }

    setName(std::string("Source[") + m_synth->name() + "]");

    m_params = m_synth->params();
    m_paramsChanges.send(m_params);
}

async::Channel<AudioInputParams> EventAudioNode::inputParamsChanged() const
{
    return m_paramsChanges;
}

void EventAudioNode::prepareToPlay()
{
    ONLY_AUDIO_ENGINE_THREAD;

    IF_ASSERT_FAILED(m_synth) {
        return;
    }

    m_synth->prepareToPlay();
}

bool EventAudioNode::readyToPlay() const
{
    ONLY_AUDIO_ENGINE_THREAD;

    IF_ASSERT_FAILED(m_synth) {
        return false;
    }

    return m_synth->readyToPlay();
}

async::Notification EventAudioNode::readyToPlayChanged() const
{
    ONLY_AUDIO_ENGINE_THREAD;

    IF_ASSERT_FAILED(m_synth) {
        return {};
    }

    return m_synth->readyToPlayChanged();
}

bool EventAudioNode::hasPendingChunks() const
{
    ONLY_AUDIO_ENGINE_THREAD;

    IF_ASSERT_FAILED(m_synth) {
        return false;
    }

    return m_synth->hasPendingChunks();
}

void EventAudioNode::processInput()
{
    ONLY_AUDIO_ENGINE_THREAD;

    IF_ASSERT_FAILED(m_synth) {
        return;
    }

    m_synth->processInput();
}

InputProcessingProgress EventAudioNode::inputProcessingProgress() const
{
    ONLY_AUDIO_ENGINE_THREAD;

    IF_ASSERT_FAILED(m_synth) {
        return {};
    }

    return m_synth->inputProcessingProgress();
}

void EventAudioNode::clearCache()
{
    ONLY_AUDIO_ENGINE_THREAD;

    IF_ASSERT_FAILED(m_synth) {
        return;
    }

    m_synth->clearCache();
}

EventAudioNode::SynthCtx EventAudioNode::currentSynthCtx() const
{
    if (!m_synth) {
        return SynthCtx();
    }

    return { m_synth->mode(), m_synth->playbackPosition() };
}

void EventAudioNode::restoreSynthCtx(const SynthCtx& ctx)
{
    m_synth->setPlaybackPosition(ctx.playbackPosition);
    m_synth->setMode(ctx.mode);
}

void EventAudioNode::setupSource()
{
    ONLY_AUDIO_ENGINE_THREAD;

    IF_ASSERT_FAILED(m_synth) {
        return;
    }

    m_synth->setOutputSpec(m_outputSpec);
    m_synth->setup(m_playbackData);
}
