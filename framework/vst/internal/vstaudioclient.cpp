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
#include "vstaudioclient.h"

#include <cmath>

#include "pluginterfaces/vst/ivstaudioprocessor.h"

#include "vsttrace.h"

#include "log.h"

using namespace muse;
using namespace muse::vst;
using namespace muse::mpe;
using namespace muse::audio;
using namespace muse::audio::engine;
using namespace muse::audioplugins;
using namespace muse::midiremote;

static size_t noteEventKey(int pitch, int channel)
{
    std::size_t h1 = std::hash<int> {}(pitch);
    std::size_t h2 = std::hash<int> {}(channel);
    return h1 ^ (h2 << 1);
}

static std::optional<TransportEvent> mmcToTransportEvent(const IMMCDecoderPtr& decoder, const MMCMessage& msg)
{
    switch (msg.command) {
    case MMCCommand::Play:
        return TransportEvent::play();
    case MMCCommand::Pause:
        return TransportEvent::pause();
    case MMCCommand::Stop:
        return TransportEvent::stop();
    case MMCCommand::Locate: {
        const std::optional<double> pos = decoder->locateToSeconds(msg);
        if (pos.has_value()) {
            return TransportEvent::seek(pos.value());
        }
    } break;
    default: break;
    }

    return std::nullopt;
}

VstAudioClient::VstAudioClient()
{
    //! All fields zero, the optional ones included, until they are filled in
    m_processContext = VstProcessContext {};
}

VstAudioClient::~VstAudioClient()
{
    // Do not call setActive(false) or terminate() here.
    // The component lifecycle is managed by VstPluginInstance,
    // which defers cleanup to the main thread so that any open
    // editor view is destroyed first (required by ZENOLOGY).
}

void VstAudioClient::init(PluginType type, IVstPluginInstancePtr instance)
{
    IF_ASSERT_FAILED(instance && type != PluginType::Undefined) {
        return;
    }

    m_type = type;
    m_pluginPtr = std::move(instance);
    m_trace.name = m_pluginPtr->name();

    if (mmcDecoderFactory()) {
        m_mmcDecoder = mmcDecoderFactory()->makeDecoder();
    }

    transportEventsDispatcher(); // Force resolution outside audio callback
}

void VstAudioClient::loadSupportedParams()
{
    TRACEFUNC;

    IF_ASSERT_FAILED(m_pluginPtr) {
        return;
    }

    PluginControllerPtr controller = m_pluginPtr->controller();
    IF_ASSERT_FAILED(controller) {
        return;
    }

    const int paramCount = controller->getParameterCount();
    m_pluginParamInfoMap.reserve(static_cast<size_t>(paramCount));

    for (int i = 0; i < paramCount; ++i) {
        PluginParamInfo info;
        controller->getParameterInfo(i, info);
        m_pluginParamInfoMap.emplace(info.id, std::move(info));
    }
}

void VstAudioClient::setIsActive(const bool isActive)
{
    flushSound();

    if (isActive) {
        ensureActivity();
    } else {
        disableActivity();
    }
}

void VstAudioClient::setIsPlaying(const bool newPlaying)
{
    constexpr uint32_t playingFlag = static_cast<uint32_t>(VstProcessContext::kPlaying);
    const bool playing = (m_processContext.state & playingFlag) != 0;
    if (playing == newPlaying) {
        return;
    }

    if (newPlaying) {
        m_processContext.state |= playingFlag;
    } else {
        m_processContext.state &= ~playingFlag;
    }

    m_needUpdateState = m_isActive;
}

//! While offline (export), transport events requested by the plugin are dropped.
void VstAudioClient::setIsOffline(const bool isOffline)
{
    m_isOffline = isOffline;
}

void VstAudioClient::setOutputSpec(const audio::OutputSpec& spec)
{
    if (m_outputSpec == spec) {
        return;
    }

    m_processData.numSamples = static_cast<Steinberg::int32>(spec.samplesPerChannel);
    m_outputSpec = spec;
    m_needUnprepareProcessData = true;
    m_needUpdateState = false;

    updateProcessSetup();
}

void VstAudioClient::setProcessMode(VstProcessMode mode)
{
    if (m_processMode == mode) {
        return;
    }

    m_processMode = mode;
    m_needUnprepareProcessData = true;
    m_needUpdateState = false;

    updateProcessSetup();
}

void VstAudioClient::setVolumeGain(const muse::audio::gain_t newVolumeGain)
{
    m_volumeGain = newVolumeGain;
}

bool VstAudioClient::handleEvent(const VstEvent& event)
{
    ensureActivity();

    if (event.type == VstEvent::kNoteOnEvent) {
        size_t key = noteEventKey(event.noteOn.pitch, event.noteOn.channel);
        m_playingNotes.insert_or_assign(key, event);
    } else if (event.type == VstEvent::kNoteOffEvent) {
        size_t key = noteEventKey(event.noteOff.pitch, event.noteOff.channel);
        m_playingNotes.erase(key);
    }

    if (m_inputEvents.addEvent(const_cast<VstEvent&>(event)) == Steinberg::kResultTrue) {
        return true;
    }

    return false;
}

bool VstAudioClient::handleParamChange(const ParamChangeEvent& param)
{
    ensureActivity();
    addParamChange(param);

    m_playingParams.push_back(param.paramId);

    return true;
}

void VstAudioClient::flushSound()
{
    if (m_playingNotes.empty() && m_playingParams.empty()) {
        return;
    }

    flushBuffers();

    m_inputEvents.clear();
    m_inputParamChanges.clearQueue();

    for (const auto& pair : m_playingNotes) {
        const VstEvent& noteOn = pair.second;

        VstEvent noteOff;
        noteOff.type = VstEvent::kNoteOffEvent;
        noteOff.ppqPosition = 0;
        noteOff.sampleOffset = 0;
        noteOff.busIndex = noteOn.busIndex;
        noteOff.flags = noteOn.flags;
        noteOff.noteOff.noteId = noteOn.noteOn.noteId;
        noteOff.noteOff.channel = noteOn.noteOn.channel;
        noteOff.noteOff.pitch = noteOn.noteOn.pitch;
        noteOff.noteOff.tuning = noteOn.noteOn.tuning;
        noteOff.noteOff.velocity = noteOn.noteOn.velocity;

        m_inputEvents.addEvent(noteOff);
    }

    for (PluginParamId id : m_playingParams) {
        auto infoIt = m_pluginParamInfoMap.find(id);
        if (infoIt == m_pluginParamInfoMap.end()) {
            continue;
        }

        ParamChangeEvent paramOff;
        paramOff.paramId = id;
        paramOff.value = infoIt->second.defaultNormalizedValue;

        addParamChange(paramOff);
    }

    m_playingNotes.clear();
    m_playingParams.clear();
}

audio::samples_t VstAudioClient::process(float* output, samples_t samplesPerChannel,
                                         samples_t playbackPositionSamples)
{
    IAudioProcessorPtr processor = pluginProcessor();
    if (!processor || !output) {
        return 0;
    }

    if (!m_isActive) {
        return 0;
    }

    //! NOTE: From the VST3 documentation:
    //!
    //! Note that the ProcessData->numSamples
    //! which indicates how many samples are used in a process call can change from call to call,
    //! but never bigger than the maxSamplesPerBlock
    m_processData.numSamples = samplesPerChannel;

    m_processContext.projectTimeSamples = playbackPositionSamples;

    if (trace::isEnabled()) {
        traceBeforeProcess(samplesPerChannel);
    }

    if (samplesPerChannel > m_outputSpec.samplesPerChannel) {
        OutputSpec newSpec = m_outputSpec;
        newSpec.samplesPerChannel = samplesPerChannel;
        setOutputSpec(newSpec);
    }

    if (m_type == PluginType::Fx) {
        extractInputSamples(samplesPerChannel, output);
    }

    if (processor->process(m_processData) != Steinberg::kResultOk) {
        return 0;
    }

    m_needUpdateState = false;

    if (trace::isEnabled()) {
        traceAfterProcess(samplesPerChannel);
    }

    if (m_type == PluginType::Instrument) {
        m_inputEvents.clear();
        m_inputParamChanges.clearQueue();

        fillOutputBufferInstrument(samplesPerChannel, output);
    } else {
        fillOutputBufferFx(samplesPerChannel, output);
    }

    processOutputEvents();

    return samplesPerChannel;
}

ParamsMapping VstAudioClient::paramsMapping(const std::set<Steinberg::Vst::CtrlNumber>& controllers) const
{
    ParamsMapping result;

    if (!m_pluginPtr) {
        return result;
    }

    PluginMidiMappingPtr midiMapping = m_pluginPtr->midiMapping();
    if (!midiMapping) {
        return result;
    }

    for (const int busIdx : m_activeInputBusses) {
        for (const auto& ctrlNum : controllers) {
            PluginParamId id = 0;

            if (midiMapping->getMidiControllerAssignment(busIdx, 0, ctrlNum, id) != Steinberg::kResultOk) {
                continue;
            }

            result.emplace(ctrlNum, id);
        }
    }

    return result;
}

IAudioProcessorPtr VstAudioClient::pluginProcessor() const
{
    return static_cast<IAudioProcessorPtr>(pluginComponent());
}

PluginComponentPtr VstAudioClient::pluginComponent() const
{
    if (!m_pluginComponent) {
        if (!m_pluginPtr) {
            return nullptr;
        }

        m_pluginComponent = m_pluginPtr->component();
    }

    return m_pluginComponent;
}

void VstAudioClient::setUpProcessData()
{
    PluginComponentPtr component = pluginComponent();
    if (!component) {
        return;
    }

    m_processContext.sampleRate = m_outputSpec.sampleRate;
    m_processData.inputEvents = &m_inputEvents;
    m_processData.inputParameterChanges = &m_inputParamChanges;
    m_processData.outputEvents = &m_outputEvents;
    m_processData.processContext = &m_processContext;

    if (m_needUnprepareProcessData) {
        m_processData.unprepare();
        m_needUnprepareProcessData = false;
    }

    if (!m_processData.outputs || !m_processData.inputs) {
        m_processData.prepare(*component, m_outputSpec.samplesPerChannel, Steinberg::Vst::kSample32);
    }

    if (!m_activeOutputBusses.empty() && !m_activeInputBusses.empty()) {
        return;
    }

    BusInfo busInfo;

    for (int busIndex = 0; busIndex < m_processData.numInputs; ++busIndex) {
        component->getBusInfo(BusMediaType::kAudio, BusDirection::kInput, busIndex, busInfo);

        if (busInfo.busType == BusType::kMain && (busInfo.flags & BusInfo::kDefaultActive)) {
            component->activateBus(BusMediaType::kAudio, BusDirection::kInput, busIndex, true);
            m_activeInputBusses.emplace_back(busIndex);
        }
    }

    for (int busIndex = 0; busIndex < m_processData.numOutputs; ++busIndex) {
        component->getBusInfo(BusMediaType::kAudio, BusDirection::kOutput, busIndex, busInfo);

        if (busInfo.busType == BusType::kMain && (busInfo.flags & BusInfo::kDefaultActive)) {
            component->activateBus(BusMediaType::kAudio, BusDirection::kOutput, busIndex, true);
            m_activeOutputBusses.emplace_back(busIndex);
        }

        LOGI() << "BusIndex: " << busIndex;

        if (busInfo.busType == BusType::kMain) {
            LOGI() << "BusType: Main";
        } else {
            LOGI() << "BusType: Aux";
        }

        if (busInfo.flags & BusInfo::kDefaultActive) {
            LOGI() << "BusFlag: DefaultActive";
        } else {
            LOGI() << "BusFlag: ControlVoltage";
        }
    }

    if (m_activeInputBusses.empty()) {
        LOGI() << "0 active input buses, activating default bus";
        component->activateBus(BusMediaType::kAudio, BusDirection::kInput, 0, true);
        m_activeInputBusses.emplace_back(0);
    }

    if (m_activeOutputBusses.empty()) {
        LOGI() << "0 active output buses, activating default bus";
        component->activateBus(BusMediaType::kAudio, BusDirection::kOutput, 0, true);
        m_activeOutputBusses.emplace_back(0);
    }
}

void VstAudioClient::updateProcessSetup()
{
    if (!m_outputSpec.isValid()) {
        return;
    }

    IAudioProcessorPtr processor = pluginProcessor();
    if (!processor) {
        return;
    }

    disableActivity();

    VstProcessSetup setup;
    setup.processMode = m_processMode;
    setup.symbolicSampleSize = Steinberg::Vst::kSample32;
    setup.maxSamplesPerBlock = m_outputSpec.samplesPerChannel;
    setup.sampleRate = m_outputSpec.sampleRate;

    if (processor->setupProcessing(setup) != Steinberg::kResultOk) {
        if (trace::isEnabled()) {
            trace::log("%s: setupProcessing failed", m_trace.name.c_str());
        }
        return;
    }

    if (trace::isEnabled()) {
        traceProcessSetup(setup);
    }

    setUpProcessData();
    flushBuffers();

    ensureActivity();
}

void VstAudioClient::extractInputSamples(samples_t sampleCount, const float* sourceBuffer)
{
    if (!m_processData.inputs || !sourceBuffer) {
        return;
    }

    Steinberg::Vst::AudioBusBuffers& bus = m_processData.inputs[0];

    for (samples_t sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex) {
        size_t offset = sampleIndex * m_outputSpec.audioChannelCount;

        for (audioch_t audioChannelIndex = 0; audioChannelIndex < bus.numChannels; ++audioChannelIndex) {
            bus.channelBuffers32[audioChannelIndex][sampleIndex] = sourceBuffer[offset + audioChannelIndex];
        }
    }
}

void VstAudioClient::fillOutputBufferInstrument(samples_t sampleCount, float* output)
{
    if (!m_processData.outputs) {
        return;
    }

    for (const int busIndex : m_activeOutputBusses) {
        Steinberg::Vst::AudioBusBuffers bus = m_processData.outputs[busIndex];

        for (samples_t sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex) {
            size_t offset = sampleIndex * m_outputSpec.audioChannelCount;

            for (audioch_t audioChannelIndex = 0; audioChannelIndex < bus.numChannels; ++audioChannelIndex) {
                float sample = bus.channelBuffers32[audioChannelIndex][sampleIndex];
                output[offset + audioChannelIndex] += sample * m_volumeGain;
            }
        }
    }
}

void VstAudioClient::fillOutputBufferFx(samples_t sampleCount, float* output)
{
    if (!m_processData.outputs) {
        return;
    }

    for (const int busIndex : m_activeOutputBusses) {
        Steinberg::Vst::AudioBusBuffers bus = m_processData.outputs[busIndex];

        for (samples_t sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex) {
            size_t offset = sampleIndex * m_outputSpec.audioChannelCount;

            for (audioch_t audioChannelIndex = 0; audioChannelIndex < bus.numChannels; ++audioChannelIndex) {
                float sample = bus.channelBuffers32[audioChannelIndex][sampleIndex];
                output[offset + audioChannelIndex] = sample * m_volumeGain;
            }
        }
    }
}

void VstAudioClient::processOutputEvents()
{
    const int32_t count = m_outputEvents.getEventCount();
    if (count == 0) {
        return;
    }

    m_trace.outputEventsSinceReport += count;

    if (m_isOffline || !m_mmcDecoder || !transportEventsDispatcher()) {
        m_outputEvents.clear();
        return;
    }

    TransportEvents events;

    for (int32_t i = 0; i < count; ++i) {
        VstEvent vstEvent;
        if (m_outputEvents.getEvent(i, vstEvent) != Steinberg::kResultOk) {
            continue;
        }

        if (vstEvent.type != Steinberg::Vst::Event::kDataEvent
            || vstEvent.data.type != Steinberg::Vst::DataEvent::kMidiSysEx) {
            continue;
        }

        std::optional<MMCMessage> msg = m_mmcDecoder->decode(vstEvent.data.bytes, vstEvent.data.size);
        if (!msg.has_value()) {
            continue;
        }

        std::optional<TransportEvent> event = mmcToTransportEvent(m_mmcDecoder, msg.value());
        if (event.has_value()) {
            events.push_back(event.value());
        }
    }

    m_outputEvents.clear();

    if (!events.empty()) {
        transportEventsDispatcher()->dispatch(events);
    }
}

void VstAudioClient::ensureActivity()
{
    if (m_isActive) {
        return;
    }

    IAudioProcessorPtr processor = pluginProcessor();
    if (!processor) {
        return;
    }

    PluginComponentPtr component = pluginComponent();
    if (!component) {
        return;
    }

    component->setActive(true);
    processor->setProcessing(true);

    m_isActive = true;

    m_trace.firstAfterActivation = true;
    if (trace::isEnabled()) {
        trace::log("%s: setActive(true), setProcessing(true) at %lld", m_trace.name.c_str(),
                   static_cast<long long>(m_processContext.projectTimeSamples));
    }
}

void VstAudioClient::disableActivity()
{
    if (!m_isActive) {
        return;
    }

    IAudioProcessorPtr processor = pluginProcessor();
    if (!processor) {
        return;
    }

    PluginComponentPtr component = pluginComponent();
    if (!component) {
        return;
    }

    if (m_needUpdateState) {
        processor->process(m_processData);
        m_needUpdateState = false;
    }

    processor->setProcessing(false);
    component->setActive(false);

    m_isActive = false;

    if (trace::isEnabled()) {
        trace::log("%s: setProcessing(false), setActive(false) at %lld", m_trace.name.c_str(),
                   static_cast<long long>(m_processContext.projectTimeSamples));
    }
}

void VstAudioClient::flushBuffers()
{
    for (int inputsNumber = 0; inputsNumber < m_processData.numInputs; ++inputsNumber) {
        Steinberg::Vst::AudioBusBuffers input = m_processData.inputs[inputsNumber];

        for (int i = 0; i < m_processData.numSamples; ++i) {
            for (int audioChannel = 0; audioChannel < input.numChannels; ++audioChannel) {
                input.channelBuffers32[audioChannel][i] = 0.f;
            }
        }
    }

    for (int outputsNumber = 0; outputsNumber < m_processData.numOutputs; ++outputsNumber) {
        Steinberg::Vst::AudioBusBuffers output = m_processData.outputs[outputsNumber];

        for (int i = 0; i < m_processData.numSamples; ++i) {
            for (int audioChannel = 0; audioChannel < output.numChannels; ++audioChannel) {
                output.channelBuffers32[audioChannel][i] = 0.f;
            }
        }
    }
}

void VstAudioClient::addParamChange(const ParamChangeEvent& param)
{
    Steinberg::int32 dummyIdx = 0;
    Steinberg::Vst::IParamValueQueue* queue = m_inputParamChanges.addParameterData(param.paramId, dummyIdx);
    if (queue) {
        queue->addPoint(0, param.value, dummyIdx);
    }
}

void VstAudioClient::traceProcessSetup(const VstProcessSetup& setup)
{
    IAudioProcessorPtr processor = pluginProcessor();
    if (!processor) {
        return;
    }

    //! NOTE The SDK asks for these on the UI thread once setup is done; hosts commonly ask
    //! from wherever they set up processing, as here. Only done while tracing.
    trace::log("%s: setupProcessing %s, block up to %d samples, %.0f Hz; latency %u samples, tail %u samples",
               m_trace.name.c_str(), setup.processMode == VstProcessMode::kOffline ? "offline" : "realtime",
               static_cast<int>(setup.maxSamplesPerBlock), setup.sampleRate,
               static_cast<unsigned>(processor->getLatencySamples()), static_cast<unsigned>(processor->getTailSamples()));

    Steinberg::FUnknownPtr<Steinberg::Vst::IProcessContextRequirements> requirements(processor);
    if (requirements) {
        trace::log("%s: process context requirements: %s", m_trace.name.c_str(),
                   trace::processContextRequirementsToString(requirements->getProcessContextRequirements()).c_str());
    } else {
        trace::log("%s: does not say which process context fields it needs (no IProcessContextRequirements)",
                   m_trace.name.c_str());
    }
}

void VstAudioClient::traceProcessContext(const char* reason)
{
    using Context = VstProcessContext;
    const Context& c = m_processContext;

    char musical[160] = "";
    int len = 0;
    auto append = [&musical, &len](const char* format, auto... args) {
        if (len < 0 || len >= static_cast<int>(sizeof(musical))) {
            return;
        }
        const int n = std::snprintf(musical + len, sizeof(musical) - len, format, args...);
        len = n < 0 ? -1 : len + n;
    };

    if (c.state & Context::kTempoValid) {
        append(" tempo=%.3f", c.tempo);
    }
    if (c.state & Context::kProjectTimeMusicValid) {
        append(" ppq=%.4f", c.projectTimeMusic);
    }
    if (c.state & Context::kBarPositionValid) {
        append(" bar=%.4f", c.barPositionMusic);
    }
    if (c.state & Context::kTimeSigValid) {
        append(" sig=%d/%d", static_cast<int>(c.timeSigNumerator), static_cast<int>(c.timeSigDenominator));
    }
    if (c.state & Context::kCycleValid) {
        append(" cycle=%.4f-%.4f", c.cycleStartMusic, c.cycleEndMusic);
    }
    if (c.state & Context::kContTimeValid) {
        append(" cont=%lld", static_cast<long long>(c.continousTimeSamples));
    }
    if (c.state & Context::kSystemTimeValid) {
        append(" sys=%lld", static_cast<long long>(c.systemTime));
    }

    char state[128];
    trace::log("%s: %s: pos=%lld (%.3f s) state=%s%s", m_trace.name.c_str(), reason,
               static_cast<long long>(c.projectTimeSamples),
               c.sampleRate > 0 ? static_cast<double>(c.projectTimeSamples) / c.sampleRate : 0.0,
               trace::formatProcessContextState(c.state, state, sizeof(state)), musical);
}

void VstAudioClient::traceBeforeProcess(samples_t samplesPerChannel)
{
    const bool playing = (m_processContext.state & VstProcessContext::kPlaying) != 0;
    const samples_t position = static_cast<samples_t>(m_processContext.projectTimeSamples);

    //! While playing the position should run on from the last block; while stopped it should stay
    const samples_t expectedPosition = m_trace.wasPlaying ? m_trace.expectedPosition : m_trace.lastPosition;

    const char* reason = nullptr;
    if (m_trace.firstAfterActivation) {
        reason = "first block after activation";
    } else if (playing != m_trace.wasPlaying) {
        reason = playing ? "transport starts" : "transport stops";
    } else if (position != expectedPosition) {
        reason = playing ? "position jumps" : "position moves while stopped";
    }

    if (reason) {
        traceProcessContext(reason);

        if (playing) {
            m_trace.awaitingSound = true;
            m_trace.soundSearchStart = position;
        }
    }

    m_trace.firstAfterActivation = false;
    m_trace.wasPlaying = playing;
    m_trace.lastPosition = position;
    m_trace.expectedPosition = position + samplesPerChannel;

    ++m_trace.callsSinceReport;
    m_trace.samplesSinceReport += samplesPerChannel;

    const double sampleRate = m_processContext.sampleRate;
    if (sampleRate > 0 && m_trace.samplesSinceReport >= static_cast<samples_t>(sampleRate)) {
        trace::log("%s: %d process calls for %lld samples, %d output events, since the last report", m_trace.name.c_str(),
                   m_trace.callsSinceReport, static_cast<long long>(m_trace.samplesSinceReport), m_trace.outputEventsSinceReport);
        traceProcessContext("each second");
        m_trace.callsSinceReport = 0;
        m_trace.samplesSinceReport = 0;
        m_trace.outputEventsSinceReport = 0;
    }
}

void VstAudioClient::traceAfterProcess(samples_t samplesPerChannel)
{
    if (!m_trace.awaitingSound || !m_processData.outputs) {
        return;
    }

    //! The first sample above -60 dBFS after a start or a jump: where the plugin's first sound
    //! lands, in host samples. Compare it with where its first note should be.
    static constexpr float THRESHOLD = 0.001f;
    static constexpr double GIVE_UP_SECS = 10.0;

    const samples_t blockStart = static_cast<samples_t>(m_processContext.projectTimeSamples);

    for (const int busIndex : m_activeOutputBusses) {
        if (busIndex >= m_processData.numOutputs) {
            continue;
        }

        const Steinberg::Vst::AudioBusBuffers& bus = m_processData.outputs[busIndex];
        for (samples_t i = 0; i < samplesPerChannel; ++i) {
            for (int ch = 0; ch < bus.numChannels; ++ch) {
                if (std::fabs(bus.channelBuffers32[ch][i]) > THRESHOLD) {
                    const samples_t at = blockStart + i;
                    trace::log("%s: first sound at %lld (%.3f s), %lld samples (%.1f ms) after the start or jump at %lld",
                               m_trace.name.c_str(), static_cast<long long>(at), at / m_processContext.sampleRate,
                               static_cast<long long>(at - m_trace.soundSearchStart),
                               1000.0 * (at - m_trace.soundSearchStart) / m_processContext.sampleRate,
                               static_cast<long long>(m_trace.soundSearchStart));
                    m_trace.awaitingSound = false;
                    return;
                }
            }
        }
    }

    const double searched = static_cast<double>(blockStart + samplesPerChannel - m_trace.soundSearchStart);
    if (m_processContext.sampleRate > 0 && searched / m_processContext.sampleRate > GIVE_UP_SECS) {
        trace::log("%s: no sound within %.0f s after the start or jump at %lld", m_trace.name.c_str(), GIVE_UP_SECS,
                   static_cast<long long>(m_trace.soundSearchStart));
        m_trace.awaitingSound = false;
    }
}
