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
#include "vstsynthesiser.h"

#include <algorithm>

#include "log.h"

using namespace muse;
using namespace muse::vst;
using namespace muse::audio::synth;
using namespace muse::audio;
using namespace muse::audioplugins;

static const std::set<Steinberg::Vst::CtrlNumber> SUPPORTED_CONTROLLERS = {
    Steinberg::Vst::kCtrlVolume,
    Steinberg::Vst::kCtrlExpression,
    Steinberg::Vst::kCtrlSustainOnOff,
    Steinberg::Vst::kCtrlSustenutoOnOff,
    Steinberg::Vst::kPitchBend,
};

VstSynthesiser::VstSynthesiser(const TrackId trackId, const muse::audio::AudioInputParams& params)
    : AbstractSynthesizer(params),
    m_vstAudioClient(std::make_unique<VstAudioClient>()),
    m_trackId(trackId)
{
}

VstSynthesiser::~VstSynthesiser()
{
    instancesRegister()->unregisterInstrPlugin(m_params.resourceMeta.id, m_trackId);
}

void VstSynthesiser::init(const OutputSpec& spec)
{
    IF_ASSERT_FAILED(spec.isValid()) {
        return;
    }

    m_outputSpec = spec;

    m_pluginPtr = instancesRegister()->makeAndRegisterInstrPlugin(m_params.resourceMeta.id, m_trackId);

    m_vstAudioClient->init(PluginType::Instrument, m_pluginPtr);

    auto onPluginLoaded = [this]() {
        m_pluginPtr->updatePluginConfig(m_params.configuration);
        m_vstAudioClient->setOutputSpec(m_outputSpec);
        m_vstAudioClient->loadSupportedParams();
        m_sequencer.init(m_vstAudioClient->paramsMapping(SUPPORTED_CONTROLLERS), m_useDynamicEvents);
        m_inited = true;
    };

    if (m_pluginPtr->isLoaded()) {
        onPluginLoaded();
    } else {
        m_pluginPtr->loadingCompleted().onNotify(this, onPluginLoaded);
    }

    m_pluginPtr->pluginSettingsChanged().onReceive(this, [this](const muse::audio::AudioUnitConfig& newConfig) {
        if (m_params.configuration == newConfig) {
            return;
        }

        m_params.configuration = newConfig;
        m_paramsChanges.send(m_params);
    });

    m_sequencer.setOnOffStreamFlushed([this]() {
        m_vstAudioClient->flushSound();
    });
}

void VstSynthesiser::toggleVolumeGain(const bool isActive)
{
    static constexpr muse::audio::gain_t NON_ACTIVE_GAIN = 0.5f;

    if (isActive) {
        m_vstAudioClient->setVolumeGain(m_sequencer.currentGain());
    } else {
        m_vstAudioClient->setVolumeGain(NON_ACTIVE_GAIN);
    }
}

bool VstSynthesiser::isValid() const
{
    if (!m_pluginPtr) {
        return false;
    }

    return m_pluginPtr->isLoaded();
}

muse::audio::AudioSourceType VstSynthesiser::type() const
{
    return m_params.type();
}

std::string VstSynthesiser::name() const
{
    if (!m_pluginPtr) {
        return std::string();
    }

    return m_pluginPtr->name();
}

void VstSynthesiser::flushSound()
{
    m_sequencer.flushOffstream();
    m_vstAudioClient->flushSound();
}

void VstSynthesiser::setupSound(const mpe::PlaybackSetupData& setupData)
{
    m_useDynamicEvents = setupData.supportsSingleNoteDynamics;
}

void VstSynthesiser::setupEvents(const mpe::PlaybackData& playbackData)
{
    m_sequencer.load(playbackData);
}

const mpe::PlaybackData& VstSynthesiser::playbackData() const
{
    return m_sequencer.playbackData();
}

void VstSynthesiser::setMode(const muse::audio::ProcessMode mode)
{
    if (m_mode == mode) {
        return;
    }

    AbstractSynthesizer::setMode(mode);

    bool isActive = isModePlaying(mode);
    m_sequencer.setActive(isActive);
    toggleVolumeGain(isActive);
    m_vstAudioClient->setIsPlaying(isActive);
    m_vstAudioClient->setIsActive(isActive);

    m_vstAudioClient->setIsOffline(mode == ProcessMode::PlayingOffline);

    if (mode == ProcessMode::PlayingOffline) {
        m_vstAudioClient->setProcessMode(VstProcessMode::kOffline);
    } else {
        m_vstAudioClient->setProcessMode(VstProcessMode::kRealtime);
    }
}

muse::audio::TimePosition VstSynthesiser::playbackPosition() const
{
    return m_currentPosition;
}

void VstSynthesiser::setPlaybackPosition(const muse::audio::TimePosition& position)
{
    IF_ASSERT_FAILED(position.isValid()) {
        return;
    }

    //! NOTE Don't trust that msecs_t is used everywhere here,
    // in fact, usecs_t (microseconds) is stored there.
    const usecs_t usecs = muse::secs_to_usecs(position.time());
    m_sequencer.setPlaybackPosition(msecs_t(usecs.raw()));

    m_currentPosition = position;

    if (m_sequencer.isActive()) {
        m_vstAudioClient->setVolumeGain(m_sequencer.currentGain());
    }
}

void VstSynthesiser::setOutputSpec(const audio::OutputSpec& spec)
{
    m_outputSpec = spec;
    if (m_inited) {
        m_vstAudioClient->setOutputSpec(spec);
    }
}

samples_t VstSynthesiser::process(float* buffer, samples_t samplesPerChannel)
{
    if (!buffer) {
        return 0;
    }

    const msecs_t nextMsecs = samplesToMsecs(samplesPerChannel, m_outputSpec.sampleRate);
    const VstSequencer::EventSequenceMap sequences = m_sequencer.movePlaybackForward(nextMsecs);
    const bool active = m_sequencer.isActive();

    //! NOTE Events that go into the plugin (notes, parameter changes) take effect at the start of a plugin
    //! process() call, so the block is split at them. Volume gain changes don't: we apply the gain to the
    //! plugin's output ourselves, so they're passed along with the call at their sample offset instead.
    //! Dynamics curves (e.g. hairpins) produce a gain change every few tens of milliseconds, and splitting
    //! at each of them multiplies the number of plugin calls, which is expensive for heavy instruments.
    samples_t segmentStart = 0;
    samples_t sampleOffset = 0;
    samples_t processedSamples = 0;
    VstAudioClient::VolumeGainChanges volumeGainChanges;

    auto processSegment = [&](samples_t segmentEnd) {
        if (segmentEnd > segmentStart) {
            const samples_t segmentSamples = segmentEnd - segmentStart;
            processedSamples += m_vstAudioClient->process(buffer + segmentStart * m_outputSpec.audioChannelCount, segmentSamples,
                                                          m_currentPosition.samples(), volumeGainChanges);

            if (active) {
                m_currentPosition.forward(segmentSamples);
            }
        } else if (!volumeGainChanges.empty()) {
            m_vstAudioClient->setVolumeGain(volumeGainChanges.back().second);
        }

        volumeGainChanges.clear();
        segmentStart = segmentEnd;
    };

    for (auto it = sequences.cbegin(); it != sequences.cend(); ++it) {
        samples_t durationInSamples = samplesPerChannel - sampleOffset;

        auto nextIt = std::next(it);
        if (nextIt != sequences.cend()) {
            msecs_t duration = nextIt->first - it->first;
            durationInSamples = microSecsToSamples(duration, m_outputSpec.sampleRate);
        }

        IF_ASSERT_FAILED(sampleOffset + durationInSamples <= samplesPerChannel) {
            break;
        }

        const bool hasPluginEvents = std::any_of(it->second.cbegin(), it->second.cend(), [](const VstSequencer::EventType& event) {
            return !std::holds_alternative<muse::audio::gain_t>(event);
        });

        if (hasPluginEvents) {
            processSegment(sampleOffset);
        }

        for (const VstSequencer::EventType& event : it->second) {
            if (std::holds_alternative<VstEvent>(event)) {
                m_vstAudioClient->handleEvent(std::get<VstEvent>(event));
            } else if (std::holds_alternative<ParamChangeEvent>(event)) {
                m_vstAudioClient->handleParamChange(std::get<ParamChangeEvent>(event));
            } else {
                volumeGainChanges.emplace_back(sampleOffset - segmentStart, std::get<muse::audio::gain_t>(event));
            }
        }

        sampleOffset += durationInSamples;
    }

    processSegment(sampleOffset);

    return processedSamples;
}
