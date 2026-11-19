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

#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsthostapplication.h"
#include "pluginterfaces/vst/ivstmidilearn.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "pluginterfaces/vst/ivstpluginterfacesupport.h"
#include "pluginterfaces/vst/ivstunits.h"

#include "vst/internal/vstcomponenthandler.h"
#include "vst/internal/vsthostcontext.h"
#include "vst/internal/vsttrace.h"

using namespace muse::vst;
using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {
void collect(const char* line, void* ctx)
{
    static_cast<std::vector<std::string>*>(ctx)->push_back(line);
}

class Vst_TraceTests : public ::testing::Test
{
protected:
    void SetUp() override
    {
        trace::initQueue();
        trace::setEnabled(true);
        trace::drainTo(nullptr, nullptr);
    }

    void TearDown() override
    {
        trace::drainTo(nullptr, nullptr);
        trace::setEnabled(false);
    }
};
}

TEST_F(Vst_TraceTests, LinesComeOutInOrder)
{
    trace::log("one %d", 1);
    trace::log("two %s", "2");

    std::vector<std::string> lines;
    EXPECT_EQ(trace::drainTo(&collect, &lines), 2u);
    ASSERT_EQ(lines.size(), 2u);
    EXPECT_EQ(lines[0], "one 1");
    EXPECT_EQ(lines[1], "two 2");
    EXPECT_EQ(trace::pendingCount(), 0u);
}

TEST_F(Vst_TraceTests, NothingIsQueuedWhileOff)
{
    trace::setEnabled(false);
    trace::log("not logged");
    EXPECT_EQ(trace::pendingCount(), 0u);
}

TEST_F(Vst_TraceTests, LongLinesAreCut)
{
    const std::string longText(1000, 'x');
    trace::log("%s", longText.c_str());

    std::vector<std::string> lines;
    trace::drainTo(&collect, &lines);
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_LT(lines[0].size(), longText.size());
    EXPECT_EQ(lines[0], std::string(lines[0].size(), 'x'));
}

TEST_F(Vst_TraceTests, FullQueueDropsAndCounts)
{
    const size_t droppedBefore = trace::droppedCount();

    for (int i = 0; i < 1100; ++i) {
        trace::log("line %d", i);
    }

    EXPECT_EQ(trace::pendingCount(), 1024u);
    EXPECT_EQ(trace::droppedCount() - droppedBefore, 1100u - 1024u);

    std::vector<std::string> lines;
    trace::drainTo(&collect, &lines);
    ASSERT_EQ(lines.size(), 1024u);
    EXPECT_EQ(lines.front(), "line 0");
    EXPECT_EQ(lines.back(), "line 1023");
}

TEST_F(Vst_TraceTests, ManyWritersLoseNothingThatFits)
{
    constexpr int WRITERS = 4;
    constexpr int LINES_EACH = 200; // 800 in all: fits in the queue

    std::vector<std::thread> writers;
    for (int w = 0; w < WRITERS; ++w) {
        writers.emplace_back([w]() {
            for (int i = 0; i < LINES_EACH; ++i) {
                trace::log("%d:%d", w, i);
            }
        });
    }
    for (std::thread& t : writers) {
        t.join();
    }

    std::vector<std::string> lines;
    EXPECT_EQ(trace::drainTo(&collect, &lines), static_cast<size_t>(WRITERS * LINES_EACH));

    // Each writer's lines keep their order
    std::vector<int> next(WRITERS, 0);
    for (const std::string& line : lines) {
        const int w = std::stoi(line.substr(0, line.find(':')));
        const int i = std::stoi(line.substr(line.find(':') + 1));
        EXPECT_EQ(i, next[w]);
        next[w] = i + 1;
    }
}

TEST_F(Vst_TraceTests, InterfaceNames)
{
    EXPECT_EQ(trace::iidName(IComponentHandler2::iid), "IComponentHandler2");
    EXPECT_EQ(trace::iidName(IProcessContextRequirements::iid), "IProcessContextRequirements");
    EXPECT_EQ(trace::iidName(IComponentHandlerSystemTime::iid), "IComponentHandlerSystemTime");

    const TUID unknown = { 0x01, 0x23, 0x45, 0x67, static_cast<char>(0x89), static_cast<char>(0xAB), static_cast<char>(0xCD),
                           static_cast<char>(0xEF), 0, 0, 0, 0, 0, 0, 0, 0x0F };
    EXPECT_EQ(trace::iidName(unknown), "unknown 0123456789ABCDEF000000000000000F");
}

TEST_F(Vst_TraceTests, FlagNames)
{
    EXPECT_EQ(trace::processContextStateToString(0), "-");
    EXPECT_EQ(trace::processContextStateToString(ProcessContext::kPlaying | ProcessContext::kTempoValid), "playing|tempo");
    EXPECT_EQ(trace::processContextStateToString(1u << 30), "0x40000000");

    EXPECT_EQ(trace::processContextRequirementsToString(IProcessContextRequirements::kNeedProjectTimeMusic
                                                        | IProcessContextRequirements::kNeedTransportState),
              "projectTimeMusic|transportState");

    EXPECT_EQ(trace::restartFlagsToString(kLatencyChanged | kIoChanged), "ioChanged|latencyChanged");
}

TEST_F(Vst_TraceTests, HostContextAnswersAndLogs)
{
    VstHostContext context;
    trace::setLoadingPlugin("TestPlugin");

    //! The host application interface itself
    void* obj = nullptr;
    ASSERT_EQ(context.queryInterface(IHostApplication::iid, &obj), kResultOk);
    ASSERT_NE(obj, nullptr);
    static_cast<FUnknown*>(obj)->release();

    //! IPlugInterfaceSupport comes back wrapped and gives the SDK's answers
    obj = nullptr;
    ASSERT_EQ(context.queryInterface(IPlugInterfaceSupport::iid, &obj), kResultOk);
    ASSERT_NE(obj, nullptr);
    auto* support = static_cast<IPlugInterfaceSupport*>(obj);
    EXPECT_EQ(support->isPlugInterfaceSupported(IComponent::iid), kResultTrue);
    EXPECT_EQ(support->isPlugInterfaceSupported(IProcessContextRequirements::iid), kResultTrue);
    EXPECT_NE(support->isPlugInterfaceSupported(IMidiLearn::iid), kResultTrue);
    support->release();

    //! Something the host does not have
    obj = nullptr;
    EXPECT_NE(context.queryInterface(IComponentHandlerSystemTime::iid, &obj), kResultOk);
    EXPECT_EQ(obj, nullptr);

    trace::setLoadingPlugin(std::string());

    std::vector<std::string> lines;
    trace::drainTo(&collect, &lines);
    ASSERT_EQ(lines.size(), 6u);
    EXPECT_EQ(lines[0], "TestPlugin asks the host context for IHostApplication: yes");
    EXPECT_EQ(lines[1], "TestPlugin asks the host context for IPlugInterfaceSupport: yes");
    EXPECT_EQ(lines[2], "TestPlugin asks whether the host supports IComponent: yes");
    EXPECT_EQ(lines[3], "TestPlugin asks whether the host supports IProcessContextRequirements: yes");
    EXPECT_EQ(lines[4], "TestPlugin asks whether the host supports IMidiLearn: no");
    EXPECT_EQ(lines[5], "TestPlugin asks the host context for IComponentHandlerSystemTime: no");
}

TEST_F(Vst_TraceTests, HostContextGivesInterfaceSupportWithTraceOff)
{
    //! VST3 SDK 3.7.12's HostApplication never handed it out; VstHostContext does, traced or not
    trace::setEnabled(false);
    VstHostContext context;

    void* obj = nullptr;
    ASSERT_EQ(context.queryInterface(IPlugInterfaceSupport::iid, &obj), kResultOk);
    ASSERT_NE(obj, nullptr);
    auto* support = static_cast<IPlugInterfaceSupport*>(obj);
    EXPECT_EQ(support->isPlugInterfaceSupported(IComponent::iid), kResultTrue);
    support->release();

    EXPECT_EQ(trace::pendingCount(), 0u);
}

TEST_F(Vst_TraceTests, ComponentHandlerGivesSystemTime)
{
    trace::setEnabled(false);
    //! Held as VstPluginInstance holds it
    FUnknownPtr<VstComponentHandler> handler(new VstComponentHandler());

    void* obj = nullptr;
    ASSERT_EQ(handler->queryInterface(IComponentHandlerSystemTime::iid, &obj), kResultOk);
    ASSERT_NE(obj, nullptr);
    auto* systemTime = static_cast<IComponentHandlerSystemTime*>(obj);

    int64 now = 0;
    EXPECT_EQ(systemTime->getSystemTime(now), kResultOk);
    EXPECT_GT(now, 0);
    systemTime->release();
}

TEST_F(Vst_TraceTests, NothingIsBuiltOrQueuedWhileOff)
{
    trace::setEnabled(false);

    VstHostContext context;
    void* obj = nullptr;
    context.queryInterface(IHostApplication::iid, &obj);
    if (obj) {
        static_cast<FUnknown*>(obj)->release();
    }

    TUID messageIid;
    IMessage::iid.toTUID(messageIid);
    obj = nullptr;
    context.createInstance(messageIid, messageIid, &obj);
    if (obj) {
        static_cast<FUnknown*>(obj)->release();
    }

    FUnknownPtr<VstComponentHandler> handler(new VstComponentHandler());
    IComponentHandler* componentHandler = handler;
    componentHandler->restartComponent(kLatencyChanged);
    obj = nullptr;
    componentHandler->queryInterface(IComponentHandlerBusActivation::iid, &obj);

    EXPECT_EQ(trace::pendingCount(), 0u);
}

TEST_F(Vst_TraceTests, InterfaceSupportListsWhatTheHostUses)
{
    VstHostContext context;
    void* obj = nullptr;
    ASSERT_EQ(context.queryInterface(IPlugInterfaceSupport::iid, &obj), kResultOk);
    auto* support = static_cast<IPlugInterfaceSupport*>(obj);

    EXPECT_EQ(support->isPlugInterfaceSupported(IComponent::iid), kResultTrue);
    EXPECT_EQ(support->isPlugInterfaceSupported(IAudioProcessor::iid), kResultTrue);
    EXPECT_EQ(support->isPlugInterfaceSupported(IEditController::iid), kResultTrue);
    EXPECT_EQ(support->isPlugInterfaceSupported(IConnectionPoint::iid), kResultTrue);
    EXPECT_EQ(support->isPlugInterfaceSupported(IMidiMapping::iid), kResultTrue);
    EXPECT_EQ(support->isPlugInterfaceSupported(IProcessContextRequirements::iid), kResultTrue);

    EXPECT_NE(support->isPlugInterfaceSupported(IUnitInfo::iid), kResultTrue);
    EXPECT_NE(support->isPlugInterfaceSupported(IUnitData::iid), kResultTrue);
    EXPECT_NE(support->isPlugInterfaceSupported(IProgramListData::iid), kResultTrue);
    EXPECT_NE(support->isPlugInterfaceSupported(IEditController2::iid), kResultTrue);

    support->release();
}

TEST_F(Vst_TraceTests, StateIntoFixedBuffer)
{
    char buffer[64];
    EXPECT_STREQ(trace::formatProcessContextState(0, buffer, sizeof(buffer)), "-");
    EXPECT_STREQ(trace::formatProcessContextState(ProcessContext::kPlaying | ProcessContext::kTempoValid, buffer, sizeof(buffer)),
                 "playing|tempo");
    EXPECT_EQ(trace::formatProcessContextState(ProcessContext::kPlaying, buffer, sizeof(buffer)),
              trace::processContextStateToString(ProcessContext::kPlaying));

    //! Too small: cut, still terminated
    char tiny[6];
    trace::formatProcessContextState(ProcessContext::kPlaying | ProcessContext::kTempoValid, tiny, sizeof(tiny));
    EXPECT_EQ(std::string(tiny).size(), 5u);
}

//! Each line keeps the time it was logged, not the time it is written out
TEST_F(Vst_TraceTests, LinesKeepTheirLoggingTime)
{
    trace::log("first");
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    trace::log("second");

    std::vector<int64_t> times;
    trace::drainWithTimesTo([](const char*, int64_t timeNs, void* ctx) {
        static_cast<std::vector<int64_t>*>(ctx)->push_back(timeNs);
    }, &times);

    ASSERT_EQ(times.size(), 2u);
    EXPECT_GT(times[0], 0);
    EXPECT_GE(times[1] - times[0], 15 * 1000 * 1000); // at least 15 ms apart
}
