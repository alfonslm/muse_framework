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

#include "vstprocesscontext.h"

#include <chrono>

#include "audio/common/audioclock.h"

using namespace muse::vst;

void muse::vst::fillProcessContext(VstProcessContext& context, const VstTransportInfo& info)
{
    using Context = VstProcessContext;

    static constexpr uint32_t TRANSPORT_FLAGS = Context::kTempoValid | Context::kProjectTimeMusicValid
                                                | Context::kBarPositionValid | Context::kTimeSigValid
                                                | Context::kCycleValid | Context::kCycleActive
                                                | Context::kContTimeValid | Context::kSystemTimeValid;

    context.state &= ~TRANSPORT_FLAGS;

    context.projectTimeSamples = info.positionSamples;

    //! NOTE A plugin that keeps its own clock through loop jumps and seeks (a looper, a delay that should not
    //! reset) reads these two. In an offline export neither means anything.
    if (!info.offline && info.hasContinuousTime) {
        context.continousTimeSamples = info.continuousSamples;
        context.state |= Context::kContTimeValid;
    }

    if (!info.offline && info.hasSystemTime) {
        context.systemTime = info.systemTimeNs;
        context.state |= Context::kSystemTimeValid;
    }

    //! NOTE Without these a plugin that follows the host transport (a sequencer, an arpeggiator, a tempo-synced
    //! delay...) does not know the tempo, nor which bar and beat it is at
    if (!info.musical.isValid()) {
        return;
    }

    context.tempo = info.musical.bpm;
    context.projectTimeMusic = info.musical.quarters;
    context.barPositionMusic = info.musical.barStartQuarters;
    context.state |= Context::kTempoValid | Context::kProjectTimeMusicValid;

    //! Only from the map's bars: without them the bar start would be a made-up 0
    if (info.musical.hasBar) {
        context.state |= Context::kBarPositionValid;
    }

    if (info.musical.hasTimeSig()) {
        context.timeSigNumerator = info.musical.timeSigNumerator;
        context.timeSigDenominator = info.musical.timeSigDenominator;
        context.state |= Context::kTimeSigValid;
    }

    if (info.hasCycle && info.cycleStartQuarters < info.cycleEndQuarters) {
        context.cycleStartMusic = info.cycleStartQuarters;
        context.cycleEndMusic = info.cycleEndQuarters;
        context.state |= Context::kCycleValid | Context::kCycleActive;
    }
}

void muse::vst::transportClockAt(const audio::engine::IPlayheadPosition& playhead, int64_t offsetInBlock, double sampleRate,
                                 VstTransportInfo& info)
{
    const int64_t processed = playhead.processedSamples();
    info.hasContinuousTime = processed >= 0;
    info.continuousSamples = info.hasContinuousTime ? processed + offsetInBlock : 0;

    //! When the block is heard; if the driver does not say, now
    const int64_t presentation = playhead.blockPresentationTimeNs();
    const int64_t blockStartNs = presentation > 0 ? presentation : vstSystemTimeNs();
    const int64_t offsetNs = sampleRate > 0 ? static_cast<int64_t>(offsetInBlock * 1.0e9 / sampleRate) : 0;
    info.hasSystemTime = true;
    info.systemTimeNs = blockStartNs + offsetNs;
}

int64_t muse::vst::vstSystemTimeNs()
{
    return audio::steadyClockNowNs();
}

bool muse::vst::loopInQuarters(const audio::engine::IPlayheadPosition& playhead, audio::sample_rate_t sampleRate,
                               double& start, double& end)
{
    const audio::engine::IPlayheadPosition::Loop loop = playhead.loop();
    if (!loop.isValid() || sampleRate == 0) {
        return false;
    }

    const audio::MusicalPosition from = playhead.musicalPosition(audio::TimePosition::fromTime(loop.from, sampleRate));
    const audio::MusicalPosition to = playhead.musicalPosition(audio::TimePosition::fromTime(loop.to, sampleRate));
    if (!from.isValid() || !to.isValid() || from.quarters >= to.quarters) {
        return false;
    }

    start = from.quarters;
    end = to.quarters;
    return true;
}
