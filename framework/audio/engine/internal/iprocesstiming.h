/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
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

namespace muse::audio::engine {
//! NOTE Timing of the block that is being processed right now.
//! Valid only in the processing thread, during the process call
class IProcessTiming
{
public:
    virtual ~IProcessTiming() = default;

    //! Steady clock time (nanoseconds, see audioclock.h) at which the first sample
    //! of the block being processed is expected to be heard.
    //! Zero if unknown (e.g. offline processing)
    virtual int64_t blockPresentationTimeNs() const = 0;
};
}
