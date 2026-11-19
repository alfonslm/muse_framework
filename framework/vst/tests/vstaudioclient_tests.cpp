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

#include <cmath>
#include <vector>

#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"

#include "vst/ivstplugininstance.h"
#include "vst/internal/vstaudioclient.h"

using namespace muse;
using namespace muse::vst;
using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {
//! What one process() call told the plugin
struct RecordedCall {
    int32 numSamples = 0;
    bool playing = false;
    int64 projectTimeSamples = 0;
    double projectTimeMusic = 0.0;
    int32 eventCount = 0;
};

//! A plugin without sound: it records the process() calls it gets
class FakePlugin : public IComponent, public IAudioProcessor
{
public:
    std::vector<RecordedCall> calls;
    std::vector<std::string> lifecycle;

    tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override
    {
        if (FUnknownPrivate::iidEqual(iid, IAudioProcessor::iid)) {
            *obj = static_cast<IAudioProcessor*>(this);
        } else if (FUnknownPrivate::iidEqual(iid, IComponent::iid) || FUnknownPrivate::iidEqual(iid, IPluginBase::iid)
                   || FUnknownPrivate::iidEqual(iid, FUnknown::iid)) {
            *obj = static_cast<IComponent*>(this);
        } else {
            *obj = nullptr;
            return kNoInterface;
        }
        addRef();
        return kResultOk;
    }

    uint32 PLUGIN_API addRef() override { return ++m_refs; }
    uint32 PLUGIN_API release() override { return --m_refs; } // owned by the test

    // IPluginBase
    tresult PLUGIN_API initialize(FUnknown*) override { return kResultOk; }
    tresult PLUGIN_API terminate() override { return kResultOk; }

    // IComponent
    tresult PLUGIN_API getControllerClassId(TUID) override { return kNotImplemented; }
    tresult PLUGIN_API setIoMode(IoMode) override { return kResultOk; }
    int32 PLUGIN_API getBusCount(MediaType type, BusDirection dir) override
    {
        return type == kAudio && dir == kOutput ? 1 : 0;
    }

    tresult PLUGIN_API getBusInfo(MediaType type, BusDirection dir, int32 index, BusInfo& bus) override
    {
        if (type != kAudio || dir != kOutput || index != 0) {
            return kInvalidArgument;
        }
        bus.mediaType = kAudio;
        bus.direction = kOutput;
        bus.channelCount = 2;
        bus.busType = kMain;
        bus.flags = BusInfo::kDefaultActive;
        return kResultOk;
    }

    tresult PLUGIN_API getRoutingInfo(RoutingInfo&, RoutingInfo&) override { return kNotImplemented; }
    tresult PLUGIN_API activateBus(MediaType, BusDirection, int32, TBool) override { return kResultOk; }
    tresult PLUGIN_API setActive(TBool state) override
    {
        lifecycle.push_back(state ? "setActive(true)" : "setActive(false)");
        return kResultOk;
    }

    tresult PLUGIN_API setState(IBStream*) override { return kResultOk; }
    tresult PLUGIN_API getState(IBStream*) override { return kResultOk; }

    // IAudioProcessor
    tresult PLUGIN_API setBusArrangements(SpeakerArrangement*, int32, SpeakerArrangement*, int32) override { return kResultOk; }
    tresult PLUGIN_API getBusArrangement(BusDirection, int32, SpeakerArrangement& arr) override
    {
        arr = SpeakerArr::kStereo;
        return kResultOk;
    }

    tresult PLUGIN_API canProcessSampleSize(int32 size) override { return size == kSample32 ? kResultTrue : kResultFalse; }
    uint32 PLUGIN_API getLatencySamples() override { return 0; }
    tresult PLUGIN_API setupProcessing(ProcessSetup&) override { return kResultOk; }
    tresult PLUGIN_API setProcessing(TBool state) override
    {
        lifecycle.push_back(state ? "setProcessing(true)" : "setProcessing(false)");
        return kResultOk;
    }

    tresult PLUGIN_API process(ProcessData& data) override
    {
        RecordedCall call;
        call.numSamples = data.numSamples;
        if (data.processContext) {
            call.playing = (data.processContext->state & ProcessContext::kPlaying) != 0;
            call.projectTimeSamples = data.processContext->projectTimeSamples;
            call.projectTimeMusic = data.processContext->projectTimeMusic;
        }
        call.eventCount = data.inputEvents ? data.inputEvents->getEventCount() : 0;
        calls.push_back(call);
        lifecycle.push_back("process");
        return kResultOk;
    }

    uint32 PLUGIN_API getTailSamples() override { return 0; }

private:
    uint32 m_refs = 1;
};

class FakeInstance : public IVstPluginInstance
{
public:
    explicit FakeInstance(FakePlugin* plugin)
        : m_plugin(plugin) {}

    const audio::AudioResourceId& resourceId() const override { return m_id; }
    const std::string& name() const override { return m_name; }
    VstPluginInstanceId id() const override { return 1; }
    bool isLoaded() const override { return true; }
    async::Notification loadingCompleted() const override { return {}; }
    PluginViewPtr createView() const override { return nullptr; }
    PluginControllerPtr controller() const override { return nullptr; }
    PluginComponentPtr component() const override { return PluginComponentPtr(static_cast<IComponent*>(m_plugin)); }
    PluginMidiMappingPtr midiMapping() const override { return nullptr; }
    std::optional<uint32_t> processContextRequirements() const override { return std::nullopt; }
    void updatePluginConfig(const audio::AudioUnitConfig&) override {}
    void refreshConfig() override {}
    async::Channel<audio::AudioUnitConfig> pluginSettingsChanged() const override { return {}; }

private:
    FakePlugin* m_plugin = nullptr;
    audio::AudioResourceId m_id = "fake";
    std::string m_name = "Fake";
};

constexpr audio::samples_t BLOCK = 512;

//! 120 bpm at 48 kHz: one quarter note is 24000 samples
audio::MusicalPosition musicalAt(audio::samples_t samples)
{
    audio::MusicalPosition m;
    m.bpm = 120.0;
    m.quarters = static_cast<double>(samples) / 24000.0;
    m.timeSigNumerator = 4;
    m.timeSigDenominator = 4;
    m.hasBar = true;
    m.barStartQuarters = 4.0 * std::floor(m.quarters / 4.0);
    return m;
}

VstEvent noteOn(int16 pitch)
{
    VstEvent e {};
    e.type = VstEvent::kNoteOnEvent;
    e.noteOn.pitch = pitch;
    e.noteOn.velocity = 0.8f;
    e.noteOn.noteId = -1;
    return e;
}
}

class Vst_AudioClientTests : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_client.init(PluginType::Instrument, std::make_shared<FakeInstance>(&m_plugin));

        audio::OutputSpec spec;
        spec.sampleRate = 48000;
        spec.samplesPerChannel = BLOCK;
        spec.audioChannelCount = 2;
        m_client.setOutputSpec(spec); // activates the plugin

        m_buffer.assign(BLOCK * 2, 0.f);
        m_plugin.calls.clear();
        m_plugin.lifecycle.clear();
    }

    void play(audio::samples_t position)
    {
        m_client.process(m_buffer.data(), BLOCK, position, musicalAt(position));
    }

    FakePlugin m_plugin;
    VstAudioClient m_client;
    std::vector<float> m_buffer;
};

//! Before the first playing block, the plugin is told the start position in a stopped call of its own,
//! and the notes queued for that block wait for it
TEST_F(Vst_AudioClientTests, StartTellsPositionBeforePlaying)
{
    m_client.setIsPlaying(true);
    m_client.handleEvent(noteOn(60));

    play(96000);
    play(96000 + BLOCK);

    ASSERT_EQ(m_plugin.calls.size(), 3u);

    EXPECT_EQ(m_plugin.calls[0].numSamples, 0);
    EXPECT_FALSE(m_plugin.calls[0].playing);
    EXPECT_EQ(m_plugin.calls[0].projectTimeSamples, 96000);
    EXPECT_DOUBLE_EQ(m_plugin.calls[0].projectTimeMusic, 4.0);
    EXPECT_EQ(m_plugin.calls[0].eventCount, 0);

    EXPECT_EQ(m_plugin.calls[1].numSamples, BLOCK);
    EXPECT_TRUE(m_plugin.calls[1].playing);
    EXPECT_EQ(m_plugin.calls[1].projectTimeSamples, 96000);
    EXPECT_EQ(m_plugin.calls[1].eventCount, 1);

    //! Running on: no extra call
    EXPECT_EQ(m_plugin.calls[2].numSamples, BLOCK);
    EXPECT_TRUE(m_plugin.calls[2].playing);
    EXPECT_EQ(m_plugin.calls[2].projectTimeSamples, 96000 + BLOCK);
}

//! A jump while playing is told in a stopped call at the new position
TEST_F(Vst_AudioClientTests, JumpTellsPositionBeforePlaying)
{
    m_client.setIsPlaying(true);
    play(0);
    play(BLOCK);
    m_plugin.calls.clear();

    play(48000);

    ASSERT_EQ(m_plugin.calls.size(), 2u);
    EXPECT_EQ(m_plugin.calls[0].numSamples, 0);
    EXPECT_FALSE(m_plugin.calls[0].playing);
    EXPECT_EQ(m_plugin.calls[0].projectTimeSamples, 48000);
    EXPECT_TRUE(m_plugin.calls[1].playing);
    EXPECT_EQ(m_plugin.calls[1].projectTimeSamples, 48000);
}

//! A loop jump back to the cycle start is not a locate: the plugin knows the cycle
TEST_F(Vst_AudioClientTests, LoopJumpIsNotALocate)
{
    m_client.setCycle(0.0, 4.0);
    m_client.setIsPlaying(true);
    play(96000 - BLOCK);
    m_plugin.calls.clear();

    play(0);

    ASSERT_EQ(m_plugin.calls.size(), 1u);
    EXPECT_TRUE(m_plugin.calls[0].playing);
    EXPECT_EQ(m_plugin.calls[0].projectTimeSamples, 0);
}

//! A stop is told without rendering, at the position where the last block ended, with the note-offs
TEST_F(Vst_AudioClientTests, StopIsAZeroSampleCallAtTheEnd)
{
    m_client.setIsPlaying(true);
    m_client.handleEvent(noteOn(60));
    play(0);
    play(BLOCK);
    m_plugin.calls.clear();
    m_plugin.lifecycle.clear();

    //! As VstSynthesiser::setMode does when the transport stops
    m_client.setTransportPosition(2 * BLOCK, musicalAt(2 * BLOCK));
    m_client.setIsPlaying(false);
    m_client.setIsActive(false);

    ASSERT_EQ(m_plugin.calls.size(), 1u);
    EXPECT_EQ(m_plugin.calls[0].numSamples, 0);
    EXPECT_FALSE(m_plugin.calls[0].playing);
    EXPECT_EQ(m_plugin.calls[0].projectTimeSamples, 2 * BLOCK);
    EXPECT_DOUBLE_EQ(m_plugin.calls[0].projectTimeMusic, 2.0 * BLOCK / 24000.0);
    EXPECT_EQ(m_plugin.calls[0].eventCount, 1); // the note-off

    const std::vector<std::string> expected { "process", "setProcessing(false)", "setActive(false)" };
    EXPECT_EQ(m_plugin.lifecycle, expected);
}

//! After a stop and a reactivation, the next start is told again
TEST_F(Vst_AudioClientTests, RestartAfterStopTellsPositionAgain)
{
    m_client.setIsPlaying(true);
    play(0);
    m_client.setTransportPosition(BLOCK, musicalAt(BLOCK));
    m_client.setIsPlaying(false);
    m_client.setIsActive(false);
    m_plugin.calls.clear();

    m_client.setIsPlaying(true);
    m_client.setIsActive(true);
    play(24000);

    ASSERT_EQ(m_plugin.calls.size(), 2u);
    EXPECT_EQ(m_plugin.calls[0].numSamples, 0);
    EXPECT_FALSE(m_plugin.calls[0].playing);
    EXPECT_EQ(m_plugin.calls[0].projectTimeSamples, 24000);
    EXPECT_TRUE(m_plugin.calls[1].playing);
}

//! Stopped blocks get no extra calls
TEST_F(Vst_AudioClientTests, StoppedBlocksAreUnchanged)
{
    play(0);
    play(0);

    ASSERT_EQ(m_plugin.calls.size(), 2u);
    EXPECT_EQ(m_plugin.calls[0].numSamples, BLOCK);
    EXPECT_EQ(m_plugin.calls[1].numSamples, BLOCK);
}
