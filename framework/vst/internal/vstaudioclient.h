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

#include <utility>
#include <vector>

#include "../ivstplugininstance.h"
#include "../vsttypes.h"
#include "vstprocesscontext.h"

#include "modularity/ioc.h"
#include "audio/common/musicaltimemap.h"
#include "audio/engine/itransporteventsdispatcher.h"
#include "midiremote/immcdecoderfactory.h"

namespace muse::vst {
class VstAudioClient
{
    muse::GlobalInject<muse::audio::engine::ITransportEventsDispatcher> transportEventsDispatcher;
    muse::GlobalInject<muse::midiremote::IMMCDecoderFactory> mmcDecoderFactory;

public:
    VstAudioClient();
    ~VstAudioClient();

    void init(PluginType type, IVstPluginInstancePtr instance);
    void loadSupportedParams();

    void setIsActive(const bool isActive);
    void setIsPlaying(const bool isPlaying);

    //! NOTE While offline (export), transport events requested by the plugin are dropped: they only
    //! make sense for live playback, and forwarding them from an export worker thread would need
    //! that thread to register with the engine's message queue while the engine is blocked on it.
    void setIsOffline(const bool isOffline);
    void setOutputSpec(const audio::OutputSpec& spec);
    void setProcessMode(VstProcessMode mode);
    void setVolumeGain(const muse::audio::gain_t newVolumeGain);

    bool handleEvent(const VstEvent& event);
    bool handleParamChange(const ParamChangeEvent& param);

    void flushSound();

    //! NOTE Volume gain changes within one process() call: {sample offset in the call, new gain}, sorted by offset.
    //! The volume gain is applied to the plugin's output by us, not by the plugin, so a gain change doesn't
    //! need its own plugin process() call. After the call, the volume gain is the last gain of the list
    using VolumeGainChanges = std::vector<std::pair<audio::samples_t, audio::gain_t> >;
    audio::samples_t process(float* output, audio::samples_t samplesPerChannel, audio::samples_t playbackPositionSamples = 0,
                             const audio::MusicalPosition& musicalPosition = audio::MusicalPosition(),
                             const VolumeGainChanges& volumeGainChanges = VolumeGainChanges());

    //! The playback loop in quarter notes, told to the plugin as its cycle; cleared when there is none
    void setCycle(double startQuarters, double endQuarters);
    void clearCycle();

    //! The engine's clocks for the next process() call (see transportClockAt() in vstprocesscontext.h)
    void setTransportClock(const VstTransportInfo& clock);

    //! Moves the transport to a position without processing audio, e.g. to where the last block ended
    //! before the transport stops: the next call to the plugin, a state change or a block, carries it.
    //! The clock fields are cleared until the next block sets them.
    void setTransportPosition(audio::samples_t positionSamples, const audio::MusicalPosition& musicalPosition);

    ParamsMapping paramsMapping(const std::set<Steinberg::Vst::CtrlNumber>& controllers) const;

private:
    IAudioProcessorPtr pluginProcessor() const;
    PluginComponentPtr pluginComponent() const;

    void setUpProcessData();
    void updateProcessContext(audio::samples_t playbackPositionSamples, const audio::MusicalPosition& musicalPosition);
    void updateProcessSetup();
    void extractInputSamples(muse::audio::samples_t sampleCount, const float* sourceBuffer);

    void fillOutputBufferInstrument(muse::audio::samples_t sampleCount, float* output, const VolumeGainChanges& volumeGainChanges);
    void fillOutputBufferFx(muse::audio::samples_t sampleCount, float* output);

    void processOutputEvents();

    void ensureActivity();
    void disableActivity();

    //! A process() call with no samples (VST3 allows it for state changes): tells the plugin the transport
    //! state and position in m_processContext without rendering anything. With withEvents false, the
    //! queued events and parameter changes are kept for the next block instead of being sent with it.
    void processStateChange(bool withEvents);

    //! True when the transport plays from a position the plugin hasn't been told about yet: the first
    //! playing block after activation, or a jump (not a loop jump back into the cycle). Instruments only
    bool isLocateWhilePlaying(audio::samples_t position) const;
    void rememberCall(audio::samples_t position, audio::samples_t samples);

    void flushBuffers();

    void addParamChange(const ParamChangeEvent& param);

    //! VST trace (vsttrace.h); they do nothing while it is off
    void traceProcessSetup(const VstProcessSetup& setup);
    void traceProcessContext(const char* reason);
    void traceBeforeProcess(audio::samples_t samplesPerChannel);
    void traceAfterProcess(audio::samples_t samplesPerChannel);

    bool m_isActive = false;
    bool m_isOffline = false;
    muse::audio::gain_t m_volumeGain = 1.f; // 0.0 - 1.0

    IVstPluginInstancePtr m_pluginPtr = nullptr;
    mutable PluginComponentPtr m_pluginComponent = nullptr;

    std::vector<int> m_activeOutputBusses;
    std::vector<int> m_activeInputBusses;

    VstEventList m_inputEvents;
    VstParameterChanges m_inputParamChanges;
    VstEventList m_noInputEvents; // always empty, for state changes that must not take the queued events
    VstParameterChanges m_noInputParamChanges;
    VstEventList m_outputEvents;
    VstProcessData m_processData;
    VstProcessContext m_processContext;
    VstProcessMode m_processMode = VstProcessMode::kRealtime;

    std::unordered_map<size_t, VstEvent> m_playingNotes;
    std::vector<PluginParamId> m_playingParams;

    std::unordered_map<PluginParamId, PluginParamInfo> m_pluginParamInfoMap;

    bool m_needUnprepareProcessData = false;
    bool m_needUpdateState = false;

    PluginType m_type = PluginType::Undefined;
    audio::OutputSpec m_outputSpec;

    midiremote::IMMCDecoderPtr m_mmcDecoder;

    bool m_hasCycle = false;
    double m_cycleStartQuarters = 0.0;
    double m_cycleEndQuarters = 0.0;
    VstTransportInfo m_clock; // only the clock fields are used

    //! The last call to the plugin since activation, to tell a jump from a block that runs on
    struct LastCall {
        bool valid = false;
        bool playing = false;
        audio::samples_t position = 0;
        audio::samples_t nextPosition = 0;
        bool hasEndQuarters = false;
        double endQuarters = 0.0;
    };
    LastCall m_lastCall;

    struct TraceState {
        std::string name;
        bool firstAfterActivation = true;
        audio::samples_t expectedPosition = 0;
        audio::samples_t lastPosition = 0;
        bool wasPlaying = false;
        audio::samples_t samplesSinceReport = 0;
        int callsSinceReport = 0;
        int outputEventsSinceReport = 0;
        bool awaitingSound = false;
        audio::samples_t soundSearchStart = 0;
    };
    TraceState m_trace;
};
}
