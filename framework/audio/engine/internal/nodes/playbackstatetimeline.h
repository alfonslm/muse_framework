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

#include <memory>
#include <optional>
#include <vector>

#include "mpe/events.h"

namespace muse::audio::engine {
//! NOTE The On/Off and Live/Idle markings of one track (see mpe::PlaybackStateEvent), prepared for
//! the audio thread: the gain they apply at a given time, and whether the instrument has to be
//! processed at all. Immutable once built, so it can be shared with the audio thread.
//!
//! - On/Off (hard): Off silences the instrument until the next On, whatever is written.
//! - Live/Idle (soft): Live keeps it running; Idle lets it sleep while it has no notes, waking up
//!   `time` before each note. Without markings the instrument always runs, as before.
class PlaybackStateTimeline
{
public:
    using Type = mpe::PlaybackStateEvent::Type;

    //! NOTE Returns nullptr if the events contain no markings
    static std::shared_ptr<const PlaybackStateTimeline> build(const mpe::PlaybackEventsMap& events);

    //! NOTE Gain applied by the markings at time t (microseconds), [0; 1]
    float gainAt(mpe::timestamp_t t) const;

    //! NOTE Whether the block [from; to) has to be processed. False when the instrument is off, or
    //! idle with no note (or note lead-in) in the block and not still ringing out after a note
    bool isAwake(mpe::timestamp_t from, mpe::timestamp_t to, bool ringingOut) const;

    //! NOTE Whether an idle instrument may still be ringing out at time t, i.e. its last note ended
    //! (after the Idle marking) less than the ring-out limit before t
    bool mayRingOut(mpe::timestamp_t t) const;

    //! NOTE Whether the gain can differ from 1 anywhere in [from; to)
    bool affectsGain(mpe::timestamp_t from, mpe::timestamp_t to) const;

    //! NOTE Earliest time the markings make the instrument sound without a note (Live, or an On
    //! after an Off), if any; used to start processing early in exports
    std::optional<mpe::timestamp_t> firstSoundWithoutNote() const;

private:
    struct Marking {
        mpe::timestamp_t time = 0;   // where the marking is written
        mpe::timestamp_t anchor = 0; // where it starts to act: time - lead for On/Live, time for Off/Idle
        mpe::duration_t length = 0;  // the marking's Time
        Type type = Type::On;
        bool fade = true;
        float startGain = 1.f;       // gain just before the anchor
        bool inNoteAtTime = false;   // Idle only: a note is sounding where it's written
    };

    struct NoteSpan {
        mpe::timestamp_t start = 0;
        mpe::timestamp_t end = 0;
    };

    static float rampedGain(const Marking& m, float startGain, mpe::timestamp_t t);

    const Marking* lastHard(mpe::timestamp_t t) const;
    const Marking* lastSoft(mpe::timestamp_t t) const;

    float hardGainAt(mpe::timestamp_t t) const;
    float softGainAt(mpe::timestamp_t t) const;

    bool hasNoteNear(mpe::timestamp_t from, mpe::timestamp_t to, mpe::duration_t lead) const;
    std::optional<mpe::timestamp_t> lastNoteEndBefore(mpe::timestamp_t t) const;

    std::vector<Marking> m_hard; // On/Off, by anchor
    std::vector<Marking> m_soft; // Live/Idle, by anchor
    std::vector<NoteSpan> m_notes; // merged, sorted
};

using PlaybackStateTimelinePtr = std::shared_ptr<const PlaybackStateTimeline>;
}
