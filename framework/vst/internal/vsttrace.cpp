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

#include "vsttrace.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>

#include "pluginterfaces/base/iplugincompatibility.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivstattributes.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstautomationstate.h"
#include "pluginterfaces/vst/ivstchannelcontextinfo.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivstcontextmenu.h"
#include "pluginterfaces/vst/ivstdataexchange.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivsthostapplication.h"
#include "pluginterfaces/vst/ivstinterappaudio.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "pluginterfaces/vst/ivstmidilearn.h"
#include "pluginterfaces/vst/ivstnoteexpression.h"
#include "pluginterfaces/vst/ivstparameterfunctionname.h"
#include "pluginterfaces/vst/ivstphysicalui.h"
#include "pluginterfaces/vst/ivstpluginterfacesupport.h"
#include "pluginterfaces/vst/ivstplugview.h"
#include "pluginterfaces/vst/ivstprefetchablesupport.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "pluginterfaces/vst/ivstremapparamid.h"
#include "pluginterfaces/vst/ivstrepresentation.h"
#include "pluginterfaces/vst/ivstunits.h"

#include "log.h"

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {
constexpr size_t QUEUE_SIZE = 1024; // a power of two
constexpr size_t QUEUE_MASK = QUEUE_SIZE - 1;
constexpr size_t LINE_SIZE = 256;

//! Bounded multi-producer multi-consumer queue of fixed-size lines (D. Vyukov's algorithm):
//! no allocation, no lock; push() fails instead of waiting when the queue is full
class LineQueue
{
public:
    LineQueue()
    {
        for (size_t i = 0; i < QUEUE_SIZE; ++i) {
            m_cells[i].sequence.store(i, std::memory_order_relaxed);
        }
    }

    bool push(const char* text)
    {
        Cell* cell = nullptr;
        size_t pos = m_enqueuePos.load(std::memory_order_relaxed);
        for (;;) {
            cell = &m_cells[pos & QUEUE_MASK];
            const size_t seq = cell->sequence.load(std::memory_order_acquire);
            const intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos);
            if (diff == 0) {
                if (m_enqueuePos.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                    break;
                }
            } else if (diff < 0) {
                return false; // full
            } else {
                pos = m_enqueuePos.load(std::memory_order_relaxed);
            }
        }

        std::strncpy(cell->text, text, LINE_SIZE - 1);
        cell->text[LINE_SIZE - 1] = '\0';
        cell->sequence.store(pos + 1, std::memory_order_release);
        return true;
    }

    bool pop(char* out)
    {
        Cell* cell = nullptr;
        size_t pos = m_dequeuePos.load(std::memory_order_relaxed);
        for (;;) {
            cell = &m_cells[pos & QUEUE_MASK];
            const size_t seq = cell->sequence.load(std::memory_order_acquire);
            const intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos + 1);
            if (diff == 0) {
                if (m_dequeuePos.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                    break;
                }
            } else if (diff < 0) {
                return false; // empty
            } else {
                pos = m_dequeuePos.load(std::memory_order_relaxed);
            }
        }

        std::memcpy(out, cell->text, LINE_SIZE);
        cell->sequence.store(pos + QUEUE_MASK + 1, std::memory_order_release);
        return true;
    }

    size_t size() const
    {
        const size_t enqueued = m_enqueuePos.load(std::memory_order_relaxed);
        const size_t dequeued = m_dequeuePos.load(std::memory_order_relaxed);
        return enqueued > dequeued ? enqueued - dequeued : 0;
    }

private:
    struct Cell {
        std::atomic<size_t> sequence { 0 };
        char text[LINE_SIZE] = {};
    };

    std::array<Cell, QUEUE_SIZE> m_cells;
    alignas(64) std::atomic<size_t> m_enqueuePos { 0 };
    alignas(64) std::atomic<size_t> m_dequeuePos { 0 };
};

std::atomic<LineQueue*> s_queue { nullptr };

std::atomic<size_t> s_dropped { 0 };

bool enabledFromEnvironment()
{
    const char* value = std::getenv("MUSE_VST_TRACE");
    return value && std::strcmp(value, "1") == 0;
}

std::atomic<bool> s_writerRunning { false };
std::atomic<bool> s_exiting { false };

//! Holds the writer thread. If the program exits without stop() (an exit path that skips the module's
//! deinit), the destructor stops the thread instead of letting std::thread's destructor terminate.
struct Writer {
    std::thread thread;

    ~Writer()
    {
        if (thread.joinable()) {
            s_exiting.store(true, std::memory_order_release);
            s_writerRunning.store(false, std::memory_order_release);
            thread.join();
        }
    }
};

Writer s_writer;

std::mutex s_loadingPluginMutex;
std::string s_loadingPlugin;

void writeToLog(const char* line, void*)
{
    LOGI() << "[vst-trace] " << line;
}

void writerLoop()
{
    using namespace std::chrono_literals;
    while (s_writerRunning.load(std::memory_order_acquire)) {
        muse::vst::trace::drainTo(&writeToLog, nullptr);
        std::this_thread::sleep_for(100ms);
    }

    //! At a normal stop the rest is written; at program exit the logger may be gone already
    if (!s_exiting.load(std::memory_order_acquire)) {
        muse::vst::trace::drainTo(&writeToLog, nullptr);
    }
}

std::string joinFlags(uint32_t value, std::initializer_list<std::pair<uint32_t, const char*> > names)
{
    std::string result;
    uint32_t known = 0;
    for (const auto& [bit, name] : names) {
        known |= bit;
        if (value & bit) {
            if (!result.empty()) {
                result += '|';
            }
            result += name;
        }
    }

    const uint32_t unknown = value & ~known;
    if (unknown) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "0x%x", unknown);
        if (!result.empty()) {
            result += '|';
        }
        result += buf;
    }

    return result.empty() ? "-" : result;
}
}

namespace muse::vst::trace {
std::atomic<bool> detail::s_enabled { enabledFromEnvironment() };

void setEnabled(bool enabled)
{
    detail::s_enabled.store(enabled, std::memory_order_relaxed);
}

void initQueue()
{
    if (!s_queue.load(std::memory_order_acquire)) {
        //! Lives until the program ends: audio threads may still hold it
        s_queue.store(new LineQueue(), std::memory_order_release);
    }
}

void start()
{
    if (!isEnabled() || s_writerRunning.load()) {
        return;
    }

    initQueue();

    LOGI() << "[vst-trace] VST host trace is on (MUSE_VST_TRACE=1)";
    s_writerRunning.store(true, std::memory_order_release);
    s_writer.thread = std::thread(&writerLoop);
}

void stop()
{
    if (!s_writerRunning.load()) {
        return;
    }

    s_writerRunning.store(false, std::memory_order_release);
    if (s_writer.thread.joinable()) {
        s_writer.thread.join();
    }

    const size_t dropped = s_dropped.exchange(0);
    if (dropped > 0) {
        LOGI() << "[vst-trace] " << dropped << " lines were dropped (queue full)";
    }
}

void log(const char* format, ...)
{
    if (!isEnabled()) {
        return;
    }

    char line[LINE_SIZE];
    va_list args;
    va_start(args, format);
    std::vsnprintf(line, sizeof(line), format, args);
    va_end(args);

    LineQueue* queue = s_queue.load(std::memory_order_acquire);
    if (!queue || !queue->push(line)) {
        s_dropped.fetch_add(1, std::memory_order_relaxed);
    }
}

size_t pendingCount()
{
    LineQueue* queue = s_queue.load(std::memory_order_acquire);
    return queue ? queue->size() : 0;
}

size_t droppedCount()
{
    return s_dropped.load(std::memory_order_relaxed);
}

size_t drainTo(void (*sink)(const char* line, void* ctx), void* ctx)
{
    LineQueue* queue = s_queue.load(std::memory_order_acquire);
    if (!queue) {
        return 0;
    }

    char line[LINE_SIZE];
    size_t count = 0;
    while (queue->pop(line)) {
        if (sink) {
            sink(line, ctx);
        }
        ++count;
    }
    return count;
}

void setLoadingPlugin(const std::string& name)
{
    std::lock_guard lock(s_loadingPluginMutex);
    s_loadingPlugin = name;
}

std::string loadingPlugin()
{
    std::lock_guard lock(s_loadingPluginMutex);
    return s_loadingPlugin;
}

std::string iidName(const TUID iid)
{
#define MUSE_VST_TRACE_IID(I) \
    if (FUnknownPrivate::iidEqual(iid, I::iid)) { \
        return #I; \
    }

    MUSE_VST_TRACE_IID(FUnknown)
    MUSE_VST_TRACE_IID(IPluginBase)
    MUSE_VST_TRACE_IID(IPluginCompatibility)
    MUSE_VST_TRACE_IID(IHostApplication)
    MUSE_VST_TRACE_IID(IPlugInterfaceSupport)
    MUSE_VST_TRACE_IID(IMessage)
    MUSE_VST_TRACE_IID(IAttributeList)
    MUSE_VST_TRACE_IID(IStreamAttributes)
    MUSE_VST_TRACE_IID(IConnectionPoint)
    MUSE_VST_TRACE_IID(IComponent)
    MUSE_VST_TRACE_IID(IAudioProcessor)
    MUSE_VST_TRACE_IID(IAudioPresentationLatency)
    MUSE_VST_TRACE_IID(IProcessContextRequirements)
    MUSE_VST_TRACE_IID(IEditController)
    MUSE_VST_TRACE_IID(IEditController2)
    MUSE_VST_TRACE_IID(IEditControllerHostEditing)
    MUSE_VST_TRACE_IID(IComponentHandler)
    MUSE_VST_TRACE_IID(IComponentHandler2)
    MUSE_VST_TRACE_IID(IComponentHandler3)
    MUSE_VST_TRACE_IID(IComponentHandlerBusActivation)
    MUSE_VST_TRACE_IID(IComponentHandlerSystemTime)
    MUSE_VST_TRACE_IID(IProgress)
    MUSE_VST_TRACE_IID(IContextMenu)
    MUSE_VST_TRACE_IID(IContextMenuTarget)
    MUSE_VST_TRACE_IID(IUnitHandler)
    MUSE_VST_TRACE_IID(IUnitHandler2)
    MUSE_VST_TRACE_IID(IUnitInfo)
    MUSE_VST_TRACE_IID(IUnitData)
    MUSE_VST_TRACE_IID(IProgramListData)
    MUSE_VST_TRACE_IID(IMidiMapping)
    MUSE_VST_TRACE_IID(IMidiLearn)
    MUSE_VST_TRACE_IID(INoteExpressionController)
    MUSE_VST_TRACE_IID(INoteExpressionPhysicalUIMapping)
    MUSE_VST_TRACE_IID(IKeyswitchController)
    MUSE_VST_TRACE_IID(IParameterFunctionName)
    MUSE_VST_TRACE_IID(IParameterFinder)
    MUSE_VST_TRACE_IID(IPrefetchableSupport)
    MUSE_VST_TRACE_IID(IAutomationState)
    MUSE_VST_TRACE_IID(ChannelContext::IInfoListener)
    MUSE_VST_TRACE_IID(IXmlRepresentationController)
    MUSE_VST_TRACE_IID(IInterAppAudioHost)
    MUSE_VST_TRACE_IID(IDataExchangeHandler)
    MUSE_VST_TRACE_IID(IDataExchangeReceiver)
    MUSE_VST_TRACE_IID(IRemapParamID)
    MUSE_VST_TRACE_IID(IVst3ToAUWrapper)
    MUSE_VST_TRACE_IID(IVst3ToVst2Wrapper)
    MUSE_VST_TRACE_IID(IVst3ToAAXWrapper)
    MUSE_VST_TRACE_IID(IVst3WrapperMPESupport)

#undef MUSE_VST_TRACE_IID

    char hex[2 * sizeof(TUID) + 1];
    for (size_t i = 0; i < sizeof(TUID); ++i) {
        std::snprintf(hex + 2 * i, 3, "%02X", static_cast<unsigned char>(iid[i]));
    }
    return std::string("unknown ") + hex;
}

namespace {
const std::initializer_list<std::pair<uint32_t, const char*> > PROCESS_CONTEXT_STATE_NAMES = {
        { ProcessContext::kPlaying, "playing" },
        { ProcessContext::kCycleActive, "cycleActive" },
        { ProcessContext::kRecording, "recording" },
        { ProcessContext::kSystemTimeValid, "systemTime" },
        { ProcessContext::kContTimeValid, "contTime" },
        { ProcessContext::kProjectTimeMusicValid, "projectTimeMusic" },
        { ProcessContext::kBarPositionValid, "barPosition" },
        { ProcessContext::kCycleValid, "cycle" },
        { ProcessContext::kTempoValid, "tempo" },
        { ProcessContext::kTimeSigValid, "timeSig" },
        { ProcessContext::kChordValid, "chord" },
        { ProcessContext::kSmpteValid, "smpte" },
        { ProcessContext::kClockValid, "clock" },
};
}

std::string processContextStateToString(uint32_t state)
{
    return joinFlags(state, PROCESS_CONTEXT_STATE_NAMES);
}

const char* formatProcessContextState(uint32_t state, char* buffer, size_t size)
{
    if (!buffer || size == 0) {
        return buffer;
    }

    size_t len = 0;
    buffer[0] = '\0';
    uint32_t known = 0;
    auto append = [&](const char* text) {
        const int n = std::snprintf(buffer + len, size - len, "%s%s", len > 0 ? "|" : "", text);
        if (n > 0) {
            len = std::min(size - 1, len + static_cast<size_t>(n));
        }
    };

    for (const auto& [bit, name] : PROCESS_CONTEXT_STATE_NAMES) {
        known |= bit;
        if (state & bit) {
            append(name);
        }
    }

    if (const uint32_t unknown = state & ~known) {
        char hex[16];
        std::snprintf(hex, sizeof(hex), "0x%x", unknown);
        append(hex);
    }

    if (len == 0) {
        std::snprintf(buffer, size, "-");
    }

    return buffer;
}

std::string processContextRequirementsToString(uint32_t flags)
{
    using R = IProcessContextRequirements;
    return joinFlags(flags, {
        { R::kNeedSystemTime, "systemTime" },
        { R::kNeedContinousTimeSamples, "contTime" },
        { R::kNeedProjectTimeMusic, "projectTimeMusic" },
        { R::kNeedBarPositionMusic, "barPosition" },
        { R::kNeedCycleMusic, "cycle" },
        { R::kNeedSamplesToNextClock, "clock" },
        { R::kNeedTempo, "tempo" },
        { R::kNeedTimeSignature, "timeSig" },
        { R::kNeedChord, "chord" },
        { R::kNeedFrameRate, "frameRate" },
        { R::kNeedTransportState, "transportState" },
    });
}

std::string restartFlagsToString(int32_t flags)
{
    return joinFlags(static_cast<uint32_t>(flags), {
        { kReloadComponent, "reloadComponent" },
        { kIoChanged, "ioChanged" },
        { kParamValuesChanged, "paramValuesChanged" },
        { kLatencyChanged, "latencyChanged" },
        { kParamTitlesChanged, "paramTitlesChanged" },
        { kMidiCCAssignmentChanged, "midiCCAssignmentChanged" },
        { kNoteExpressionChanged, "noteExpressionChanged" },
        { kIoTitlesChanged, "ioTitlesChanged" },
        { kPrefetchableSupportChanged, "prefetchableSupportChanged" },
        { kRoutingInfoChanged, "routingInfoChanged" },
        { kKeyswitchChanged, "keyswitchChanged" },
        { kParamIDMappingChanged, "paramIDMappingChanged" },
    });
}
}
