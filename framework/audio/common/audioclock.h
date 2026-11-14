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

#include <chrono>
#include <cstdint>

namespace muse::audio {
//! NOTE The time base shared by the audio driver thread, the engine and the UI.
//! Monotonic, in nanoseconds, the same in every thread of the process.
//! Never zero (zero is reserved, see PlaybackPositionAnchor::IMMEDIATE)
inline int64_t steadyClockNowNs()
{
    using namespace std::chrono;
    const int64_t ns = duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
    return ns != 0 ? ns : 1;
}

inline int64_t secsToNs(double secs)
{
    return static_cast<int64_t>(secs * 1e9);
}

inline double nsToSecs(int64_t ns)
{
    return static_cast<double>(ns) / 1e9;
}
}
