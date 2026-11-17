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

#pragma once

#include <cstdint>

#include "audio/common/musicaltimemap.h"
#include "audio/engine/iplayhead.h"

#include "../vsttypes.h"

namespace muse::vst {
//! What the host tells a plugin about the transport for one process() call, apart from
//! "playing or not" and the sample rate, which VstAudioClient keeps in the context itself
struct VstTransportInfo {
    int64_t positionSamples = 0;            // project time
    audio::MusicalPosition musical;         // tempo, quarter notes, bar and time signature; may be invalid

    bool hasCycle = false;                  // a playback loop is set and its musical position is known
    double cycleStartQuarters = 0.0;
    double cycleEndQuarters = 0.0;

    int64_t continuousSamples = 0;          // time that keeps running through loop jumps and seeks
    int64_t systemTimeNs = 0;               // vstSystemTimeNs() when the call is made
};

//! Fills the transport fields of `context` and their valid flags from `info`. Fields without valid
//! information get their flag cleared. kPlaying, kRecording and the sample rate are left as they are.
void fillProcessContext(VstProcessContext& context, const VstTransportInfo& info);

//! The playback loop in quarter notes, if one is set and the music's tempo is known
bool loopInQuarters(const audio::engine::IPlayheadPosition& playhead, audio::sample_rate_t sampleRate, double& start,
                    double& end);

//! The host's clock for ProcessContext::systemTime and IComponentHandlerSystemTime, in nanoseconds
int64_t vstSystemTimeNs();
}
