/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * FontDaw
 * The notation-first DAW
 *
 * Copyright (C) 2026 FontDaw contributors
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

#include "global/types/secs.h"

namespace muse::audio {
//! NOTE Links a playback position to the moment it is actually heard.
//!
//! The engine renders audio in blocks, some time before the block reaches the
//! listener (output buffers, device latency...). So "the engine has rendered up
//! to position X" is not "the listener hears position X".
//!
//! An anchor says: "the sample at `position` is expected to be heard at
//! `presentationTimeNs`, and the `duration` that follows is played continuously".
//! Times are on the steady clock of the process, see audioclock.h
struct PlaybackPositionAnchor
{
    //! Position of the first sample of the block
    secs_t position = 0.0;

    //! Duration of the block. Zero for an immediate anchor
    secs_t duration = 0.0;

    //! Steady clock time (nanoseconds) at which `position` is expected to be heard.
    //! IMMEDIATE means "now": the position was set explicitly (seek), nothing was rendered
    int64_t presentationTimeNs = IMMEDIATE;

    //! Incremented by the engine on every explicit position change (seek).
    //! Lets the receiver drop the anchors that were made before the change, but delivered after it
    uint32_t epoch = 0;

    static constexpr int64_t IMMEDIATE = 0;

    inline bool isImmediate() const { return presentationTimeNs == IMMEDIATE; }

    inline bool operator==(const PlaybackPositionAnchor& other) const
    {
        return position == other.position
               && duration == other.duration
               && presentationTimeNs == other.presentationTimeNs
               && epoch == other.epoch;
    }

    inline bool operator!=(const PlaybackPositionAnchor& other) const { return !this->operator==(other); }
};
}
