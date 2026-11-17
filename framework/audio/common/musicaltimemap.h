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

#include <algorithm>
#include <cmath>
#include <vector>

namespace muse::audio {
//! Where the playhead is in the music, as opposed to in time
struct MusicalPosition {
    double quarters = 0.0;          // quarter notes since the start of the music
    double barStartQuarters = 0.0;  // where the bar that contains `quarters` starts
    double bpm = 0.0;               // quarter notes per minute
    int timeSigNumerator = 0;       // 0 if not known
    int timeSigDenominator = 0;

    bool isValid() const { return bpm > 0.0; }
    bool hasTimeSig() const { return timeSigNumerator > 0 && timeSigDenominator > 0; }
};

//! The relation between playback time and musical position for one piece of music, as it is played back
//! (repeats unrolled, playback speed applied). The audio engine itself only knows about time;
//! this is what lets it tell plugins the tempo, the time signature and the beat they are at.
struct MusicalTimeMap {
    //! The music is at `quarters` when the playback time is `secs`.
    //! Between two points the position moves evenly from one to the other, so two points with the same
    //! `quarters` are a pause in the music (a caesura, a breath).
    //! After the last point it moves on at `bps`. `bps` is also the tempo reported from this point on, it is never 0.
    struct TempoPoint {
        double secs = 0.0;
        double quarters = 0.0;
        double bps = 0.0;   // quarter notes per second

        bool operator==(const TempoPoint& o) const { return secs == o.secs && quarters == o.quarters && bps == o.bps; }
    };

    //! A bar starts at `quarters` and has this time signature.
    //! Bars of the same length follow until the next point, so one point per change is enough;
    //! a bar of irregular length (a pickup, a bar lengthened by a pause) needs a point for itself and one for the bar after it.
    struct BarPoint {
        double quarters = 0.0;
        int numerator = 4;
        int denominator = 4;
        double lengthQuarters = 0.0;    // 0 = as the time signature says

        bool operator==(const BarPoint& o) const
        {
            return quarters == o.quarters && numerator == o.numerator && denominator == o.denominator
                   && lengthQuarters == o.lengthQuarters;
        }
    };

    std::vector<TempoPoint> tempo;  // sorted by secs
    std::vector<BarPoint> bars;     // sorted by quarters

    bool isValid() const { return !tempo.empty(); }

    bool operator==(const MusicalTimeMap& o) const { return tempo == o.tempo && bars == o.bars; }
    bool operator!=(const MusicalTimeMap& o) const { return !operator==(o); }

    MusicalPosition positionAt(double secs) const
    {
        MusicalPosition result;
        if (tempo.empty()) {
            return result;
        }

        // the last point at or before secs
        auto next = std::upper_bound(tempo.begin(), tempo.end(), secs, [](double s, const TempoPoint& p) {
            return s < p.secs;
        });
        const TempoPoint& from = next == tempo.begin() ? tempo.front() : *std::prev(next);

        result.bpm = from.bps * 60.0;

        if (secs <= from.secs) {
            result.quarters = from.quarters;
        } else if (next == tempo.end()) {
            result.quarters = from.quarters + (secs - from.secs) * from.bps;
        } else {
            const double span = next->secs - from.secs;
            const double ratio = span > 0.0 ? (secs - from.secs) / span : 0.0;
            result.quarters = from.quarters + (next->quarters - from.quarters) * ratio;
        }

        if (bars.empty()) {
            return result;
        }

        // the last bar point at or before the position
        auto nextBar = std::upper_bound(bars.begin(), bars.end(), result.quarters, [](double q, const BarPoint& b) {
            return q < b.quarters;
        });
        const BarPoint& bar = nextBar == bars.begin() ? bars.front() : *std::prev(nextBar);

        result.timeSigNumerator = bar.numerator;
        result.timeSigDenominator = bar.denominator;

        const double barLength = bar.lengthQuarters > 0.0
                                 ? bar.lengthQuarters
                                 : (bar.denominator > 0 ? 4.0 * bar.numerator / bar.denominator : 0.0);

        if (barLength > 0.0 && result.quarters > bar.quarters) {
            result.barStartQuarters = bar.quarters + std::floor((result.quarters - bar.quarters) / barLength) * barLength;
        } else {
            result.barStartQuarters = bar.quarters;
        }

        return result;
    }
};
}
