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

#include <atomic>
#include <cstdint>

namespace muse::audio {
//! NOTE The output latency measured by the active audio driver:
//! the time between the driver asking for a block of audio (the driver callback)
//! and the first sample of that block being heard.
//!
//! There is one output device per process, so this is a process wide value.
//! It is lock-free and can be used from any thread:
//! - the driver reports the value from its callback thread, right before it asks for audio
//! - the audio callback reads it to timestamp the block it is about to render
//! - the UI reads it to show the detected latency
//!
//! A driver that can't measure its latency reports nothing, the latency is then "unknown"
class OutputLatency
{
public:
    static constexpr int64_t UNKNOWN = -1;

    //! Called by the audio driver, any thread
    static void report(double latencySecs)
    {
        if (!(latencySecs >= 0.0) || latencySecs > MAX_SECS) { // also filters out NaN
            return;
        }

        value().store(static_cast<int64_t>(latencySecs * 1e9), std::memory_order_relaxed);
    }

    //! Called when the driver is opened or closed
    static void reset()
    {
        value().store(UNKNOWN, std::memory_order_relaxed);
    }

    static bool isKnown()
    {
        return ns() != UNKNOWN;
    }

    //! Latency in nanoseconds, UNKNOWN if the driver did not report it
    static int64_t ns()
    {
        return value().load(std::memory_order_relaxed);
    }

    //! Latency in nanoseconds, the given value if the driver did not report it
    static int64_t nsOr(int64_t fallbackNs)
    {
        const int64_t v = ns();
        return v != UNKNOWN ? v : fallbackNs;
    }

private:
    //! Anything above is a broken report, not a latency
    static constexpr double MAX_SECS = 5.0;

    static std::atomic<int64_t>& value()
    {
        static std::atomic<int64_t> v { UNKNOWN };
        return v;
    }
};
}
