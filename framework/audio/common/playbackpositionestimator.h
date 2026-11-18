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

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>

#include "playbackpositionanchor.h"

namespace muse::audio {
//! NOTE Tells which playback position is heard at a given moment.
//! Fed with the anchors made by the engine (see PlaybackPositionAnchor), not thread-safe.
//!
//! An anchor takes effect only when its audio is heard, so the estimate follows
//! the sound over output latency, loops and seeks.
//!
//! How to use, e.g. for a playback cursor:
//!     estimator.addAnchor(anchor);                    // every time the player sends one
//!     pos = estimator.positionAt(steadyClockNowNs()); // every frame
class PlaybackPositionEstimator
{
public:

    //! Sets the position explicitly and forgets the anchors
    void reset(secs_t position)
    {
        m_anchors.clear();
        m_heldPosition = position;
        m_hasOffset = false;
        m_hasLastAnchorEnd = false;
        m_lastRun = NO_RUN;
    }

    void addAnchor(const PlaybackPositionAnchor& anchor)
    {
        // Anchors made before an explicit position change can be delivered after it
        const bool isNewerEpoch = !m_hasEpoch || static_cast<int32_t>(anchor.epoch - m_epoch) > 0;
        if (!isNewerEpoch && anchor.epoch != m_epoch) {
            return;
        }

        if (isNewerEpoch) {
            m_hasEpoch = true;
            m_epoch = anchor.epoch;

            // everything we have is from before the change
            reset(anchor.isImmediate() ? anchor.position : m_heldPosition);
        }

        if (anchor.isImmediate()) {
            return;
        }

        const double position = anchor.position.raw();
        const double duration = std::max(anchor.duration.raw(), 0.0);
        const double rawOffsetNs = static_cast<double>(anchor.presentationTimeNs) - position * 1e9;

        // A run is a sequence of blocks that are played one right after another.
        // Loop and seek start a new run
        const bool isContinuous = m_hasLastAnchorEnd
                                  && std::abs(position - m_lastAnchorEnd) < CONTINUITY_TOLERANCE_SECS;

        const bool isOffsetJump = m_hasOffset && std::abs(rawOffsetNs - m_offsetNs) > OFFSET_JUMP_NS;

        if (isContinuous && m_hasOffset && !isOffsetJump) {
            // Within a run the positions are sample accurate, but the presentation times
            // carry the jitter of the callback and of the latency measurement.
            // So we smooth the "position -> time" offset
            m_offsetNs += OFFSET_SMOOTHING * (rawOffsetNs - m_offsetNs);
        } else {
            m_offsetNs = rawOffsetNs;
            m_hasOffset = true;
            if (!isContinuous) {
                ++m_run;
            }
        }

        Segment segment;
        segment.position = position;
        segment.duration = duration;
        segment.presentationTimeNs = static_cast<int64_t>(m_offsetNs + position * 1e9);
        segment.run = m_run;

        // keep the presentation times ordered
        if (!m_anchors.empty()) {
            segment.presentationTimeNs = std::max(segment.presentationTimeNs, m_anchors.back().presentationTimeNs);
        }

        m_anchors.push_back(segment);

        m_lastAnchorEnd = position + duration;
        m_hasLastAnchorEnd = true;

        while (m_anchors.size() > MAX_ANCHORS) {
            m_anchors.pop_front();
        }
    }

    //! The position that is heard at the given steady clock time.
    //! To compensate for a latency that is not known to the audio driver (e.g. Bluetooth),
    //! ask for an earlier time. To compensate for the latency of the display, ask for a later time
    secs_t positionAt(int64_t timeNs)
    {
        // Drop the anchors whose audio is over
        while (m_anchors.size() > 1 && m_anchors[1].presentationTimeNs <= timeNs) {
            m_anchors.pop_front();
        }

        if (m_anchors.empty()) {
            return m_heldPosition;
        }

        const Segment& current = m_anchors.front();
        if (current.presentationTimeNs > timeNs) {
            // not heard yet
            return m_heldPosition;
        }

        double elapsed = static_cast<double>(timeNs - current.presentationTimeNs) / 1e9;

        if (m_anchors.size() > 1) {
            // Before a jump (loop) we must not run over the end of the block
            if (m_anchors[1].run != current.run) {
                elapsed = std::min(elapsed, current.duration);
            }
        } else {
            // The next anchor is late (or the playback is over). Keep going for a while to stay smooth
            elapsed = std::min(elapsed, current.duration + MAX_EXTRAPOLATION_SECS);
        }

        double position = current.position + elapsed;

        // Never go back within a run
        if (current.run == m_lastRun) {
            position = std::max(position, m_heldPosition.raw());
        }

        m_lastRun = current.run;
        m_heldPosition = position;

        return m_heldPosition;
    }

    //! The last position returned by positionAt (or set by reset)
    secs_t lastPosition() const
    {
        return m_heldPosition;
    }

    bool hasAnchors() const
    {
        return !m_anchors.empty();
    }

private:

    //! Positions closer than this to the end of the previous block continue it
    static constexpr double CONTINUITY_TOLERANCE_SECS = 0.0005;

    //! Per block weight of a new measurement of the "position -> time" offset
    static constexpr double OFFSET_SMOOTHING = 0.1;

    //! An offset change bigger than this is not jitter (the device or its latency has changed)
    static constexpr double OFFSET_JUMP_NS = 50.0 * 1e6;

    //! How long we go on after the end of the last block when no new anchor comes
    static constexpr double MAX_EXTRAPOLATION_SECS = 0.1;

    static constexpr size_t MAX_ANCHORS = 2048;

    static constexpr uint64_t NO_RUN = 0;

    struct Segment {
        double position = 0.0;
        double duration = 0.0;
        int64_t presentationTimeNs = 0;
        uint64_t run = NO_RUN;
    };

    std::deque<Segment> m_anchors;

    secs_t m_heldPosition = 0.0;

    bool m_hasEpoch = false;
    uint32_t m_epoch = 0;

    bool m_hasOffset = false;
    double m_offsetNs = 0.0;

    bool m_hasLastAnchorEnd = false;
    double m_lastAnchorEnd = 0.0;

    uint64_t m_run = NO_RUN;
    uint64_t m_lastRun = NO_RUN;
};
}
