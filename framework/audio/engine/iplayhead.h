/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * MuseScore Studio
 * Music Composition & Notation
 *
 * Copyright (C) 2021 MuseScore Limited and others
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
#include <memory>

#include "audio/common/timeposition.h"
#include "audio/common/musicaltimemap.h"

namespace muse::audio::engine {
class IPlayheadPosition
{
public:
    virtual ~IPlayheadPosition() = default;
    virtual const TimePosition& currentPosition() const = 0;

    //! Where the music is at the given time. Not valid if the tempo of the music is not known.
    virtual MusicalPosition musicalPosition(const TimePosition& position) const = 0;

    //! The playback loop, from `from` up to `to`, in seconds; not valid if no loop is set
    struct Loop {
        double from = 0.0;
        double to = 0.0;

        bool isValid() const { return from < to; }
    };

    virtual Loop loop() const { return Loop(); }

    //! Samples processed by the engine before the block being processed, playing or not; one clock
    //! for all tracks, not reset by seeks or loop jumps. -1 if not known (e.g. offline processing).
    virtual int64_t processedSamples() const { return -1; }

    //! When the first sample of the block being processed is expected to be heard, on the steady
    //! clock of audio/common/audioclock.h, in nanoseconds; 0 if not known
    virtual int64_t blockPresentationTimeNs() const { return 0; }
};

class IPlayhead : public IPlayheadPosition
{
public:

    virtual void forward(const TimePosition& delta) = 0;
};

using PlayheadPositionPtr = std::shared_ptr<IPlayheadPosition>;
using PlayheadPtr = std::shared_ptr<IPlayhead>;
}
