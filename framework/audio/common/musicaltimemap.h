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
    bool hasBar = false;            // barStartQuarters and the time signature come from the map's bars

    bool isValid() const { return bpm > 0.0; }
    bool hasTimeSig() const { return timeSigNumerator > 0 && timeSigDenominator > 0; }
};

//! The relation between playback time and musical position for one piece of music, as it is played back
//! (repeats unrolled, playback speed applied). The audio engine itself only knows about time;
//! this is what lets it tell plugins the tempo, the time signature and the beat they are at.
//!
//! What the application that builds it must keep to (the contract):
//! - Quarter 0 is playback time 0, the start of the music. A pickup (anacrusis) is the first bar, with
//!   its real, shorter length (a BarPoint with lengthQuarters), so the first full bar starts at the
//!   pickup's length. This keeps plugins on the same grid as a MIDI export of the same music, whose
//!   tick 0 is also the start of the music; neither negative positions nor a padded first bar can be
//!   written to a MIDI file without moving the music in time.
//!   What follows for plugins: downbeats are found through barPositionMusic and the time signature,
//!   as after any bar of odd length; a plugin that takes its grid from whole numbers of
//!   projectTimeMusic is off by the pickup's fraction when the pickup is not a whole number of
//!   quarters; a plugin that numbers bars counts the pickup as bar 1.
//! - Time is played time: repeats unrolled, the playback speed applied, pauses included.
//! - A pause (caesura, breath, section-break pause) can be given two ways. FontDaw gives it as extra
//!   beats: its bar is lengthened by whole beats (a BarPoint with lengthQuarters, and one for the bar
//!   after) at a tempo close to the surrounding one, so the beats keep moving and last exactly the
//!   pause; the same function builds this map and the MIDI export's tempo map, so a tempo track
//!   imported from that file agrees with this map bar for bar. A caller may instead give it as two
//!   tempo points with the same quarters (the music stands still while time runs on; the tempo
//!   reported during it is that of the point that starts it); positionAt() handles both.
//! - Gradual tempo changes are given as steps fine enough for the music (about one per beat).
//! - Bars: a point at the start, at every time signature change, at every bar that is not as long as
//!   its time signature says, and at the bar after each of those.
//! - It is sent again whenever any of this changes (tempo, speed, repeats, the score's content).
//! sanitize() is applied on arrival in the engine, so a careless map cannot break the lookups.
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

    //! Keeps only usable points, in order: tempo points with finite secs and quarters and a positive,
    //! finite bps, sorted by secs; bar points with finite quarters and a positive time signature,
    //! sorted by quarters. Returns false if anything was dropped or reordered.
    bool sanitize()
    {
        bool unchanged = true;

        const auto badTempo = [](const TempoPoint& p) {
            return !std::isfinite(p.secs) || !std::isfinite(p.quarters) || !std::isfinite(p.bps) || p.bps <= 0.0;
        };
        const size_t tempoBefore = tempo.size();
        tempo.erase(std::remove_if(tempo.begin(), tempo.end(), badTempo), tempo.end());
        unchanged = unchanged && tempo.size() == tempoBefore;

        const auto bySecs = [](const TempoPoint& a, const TempoPoint& b) { return a.secs < b.secs; };
        if (!std::is_sorted(tempo.begin(), tempo.end(), bySecs)) {
            std::stable_sort(tempo.begin(), tempo.end(), bySecs);
            unchanged = false;
        }

        const auto badBar = [](const BarPoint& b) {
            return !std::isfinite(b.quarters) || !std::isfinite(b.lengthQuarters) || b.lengthQuarters < 0.0
                   || b.numerator <= 0 || b.denominator <= 0;
        };
        const size_t barsBefore = bars.size();
        bars.erase(std::remove_if(bars.begin(), bars.end(), badBar), bars.end());
        unchanged = unchanged && bars.size() == barsBefore;

        const auto byQuarters = [](const BarPoint& a, const BarPoint& b) { return a.quarters < b.quarters; };
        if (!std::is_sorted(bars.begin(), bars.end(), byQuarters)) {
            std::stable_sort(bars.begin(), bars.end(), byQuarters);
            unchanged = false;
        }

        return unchanged;
    }

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

        result.hasBar = true;
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
