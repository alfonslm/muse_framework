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

#include "playbackstatetimeline.h"

#include <algorithm>

using namespace muse;
using namespace muse::mpe;
using namespace muse::audio::engine;

//! NOTE How long an idle instrument may keep ringing out after its last note before it's put to sleep
//! even if it isn't silent yet (see EventAudioNode, which also puts it to sleep once it's silent)
static constexpr duration_t RING_OUT_LIMIT = 10'000'000;

static bool isStart(PlaybackStateTimeline::Type type)
{
    return type == PlaybackStateTimeline::Type::On || type == PlaybackStateTimeline::Type::Live;
}

static bool isHard(PlaybackStateTimeline::Type type)
{
    return type == PlaybackStateTimeline::Type::On || type == PlaybackStateTimeline::Type::Off;
}

std::shared_ptr<const PlaybackStateTimeline> PlaybackStateTimeline::build(const PlaybackEventsMap& events)
{
    auto timeline = std::make_shared<PlaybackStateTimeline>();

    for (auto it = events.cbegin(); it != events.cend(); ++it) {
        const timestamp_t timestamp = it->first;

        for (const PlaybackEvent& event : it->second) {
            if (const NoteEvent* note = std::get_if<NoteEvent>(&event)) {
                const duration_t duration = std::max<duration_t>(0, note->arrangementCtx().actualDuration);
                timeline->m_notes.push_back({ timestamp, timestamp + duration });
                continue;
            }

            if (const PlaybackStateEvent* state = std::get_if<PlaybackStateEvent>(&event)) {
                Marking marking;
                marking.time = timestamp;
                marking.length = std::max<duration_t>(0, state->time);
                marking.type = state->type;
                marking.fade = state->fade;
                marking.anchor = isStart(state->type) ? timestamp - marking.length : timestamp;

                if (isHard(state->type)) {
                    timeline->m_hard.push_back(marking);
                } else {
                    timeline->m_soft.push_back(marking);
                }
            }
        }
    }

    if (timeline->m_hard.empty() && timeline->m_soft.empty()) {
        return nullptr;
    }

    auto byAnchor = [](const Marking& a, const Marking& b) { return a.anchor < b.anchor; };
    std::stable_sort(timeline->m_hard.begin(), timeline->m_hard.end(), byAnchor);
    std::stable_sort(timeline->m_soft.begin(), timeline->m_soft.end(), byAnchor);

    // Merge overlapping notes (chords, several voices), so the spans are sorted by both start and end
    std::vector<NoteSpan>& notes = timeline->m_notes;
    std::sort(notes.begin(), notes.end(), [](const NoteSpan& a, const NoteSpan& b) { return a.start < b.start; });
    std::vector<NoteSpan> merged;
    for (const NoteSpan& span : notes) {
        if (!merged.empty() && span.start <= merged.back().end) {
            merged.back().end = std::max(merged.back().end, span.end);
        } else {
            merged.push_back(span);
        }
    }
    notes = std::move(merged);

    //! NOTE Each marking starts from the gain the previous one left, so e.g. an On during an Off's
    //! fade-out fades back in from there instead of jumping
    for (size_t i = 0; i < timeline->m_hard.size(); ++i) {
        Marking& m = timeline->m_hard[i];
        m.startGain = i == 0 ? 1.f : rampedGain(timeline->m_hard[i - 1], timeline->m_hard[i - 1].startGain, m.anchor);
    }

    for (size_t i = 0; i < timeline->m_soft.size(); ++i) {
        Marking& m = timeline->m_soft[i];
        m.inNoteAtTime = timeline->hasNoteNear(m.time, m.time + 1, 0);

        if (i == 0) {
            m.startGain = 1.f;
            continue;
        }

        const Marking& prev = timeline->m_soft[i - 1];
        if (prev.type == Type::Idle) {
            //! NOTE A Live after an Idle fades in from silence, unless a note keeps the instrument awake there
            const bool asleep = m.anchor >= prev.anchor + prev.length && !timeline->hasNoteNear(m.anchor, m.anchor + 1, prev.length);
            m.startGain = asleep ? 0.f : 1.f;
        } else {
            m.startGain = rampedGain(prev, prev.startGain, m.anchor);
        }
    }

    return timeline;
}

float PlaybackStateTimeline::rampedGain(const Marking& m, float startGain, timestamp_t t)
{
    const float progress = m.length > 0
                           ? static_cast<float>(std::clamp<double>(static_cast<double>(t - m.anchor) / m.length, 0.0, 1.0))
                           : (t >= m.anchor ? 1.f : 0.f);

    if (isStart(m.type)) {
        return m.fade ? startGain + (1.f - startGain) * progress : 1.f;
    }

    if (m.fade) {
        return startGain * (1.f - progress);
    }

    return t < m.anchor + m.length ? startGain : 0.f;
}

const PlaybackStateTimeline::Marking* PlaybackStateTimeline::lastHard(timestamp_t t) const
{
    auto it = std::upper_bound(m_hard.cbegin(), m_hard.cend(), t, [](timestamp_t time, const Marking& m) { return time < m.anchor; });
    return it == m_hard.cbegin() ? nullptr : &*std::prev(it);
}

const PlaybackStateTimeline::Marking* PlaybackStateTimeline::lastSoft(timestamp_t t) const
{
    auto it = std::upper_bound(m_soft.cbegin(), m_soft.cend(), t, [](timestamp_t time, const Marking& m) { return time < m.anchor; });
    return it == m_soft.cbegin() ? nullptr : &*std::prev(it);
}

float PlaybackStateTimeline::hardGainAt(timestamp_t t) const
{
    const Marking* m = lastHard(t);
    return m ? rampedGain(*m, m->startGain, t) : 1.f;
}

float PlaybackStateTimeline::softGainAt(timestamp_t t) const
{
    const Marking* m = lastSoft(t);
    if (!m) {
        return 1.f;
    }

    if (m->type == Type::Live) {
        return rampedGain(*m, m->startGain, t);
    }

    //! NOTE Idle: the marking fades out (or cuts) whatever sounds without a note, e.g. a drone. After
    //! that the instrument either sleeps or plays its notes, which have their own envelopes
    if (m->inNoteAtTime || t >= m->anchor + m->length) {
        return 1.f;
    }

    return hasNoteNear(t, t + 1, m->length) ? 1.f : rampedGain(*m, m->startGain, t);
}

float PlaybackStateTimeline::gainAt(timestamp_t t) const
{
    return hardGainAt(t) * softGainAt(t);
}

bool PlaybackStateTimeline::affectsGain(timestamp_t from, timestamp_t to) const
{
    if (gainAt(from) < 1.f || gainAt(to) < 1.f) {
        return true;
    }

    auto rampOverlaps = [from, to](const Marking& m) {
        const timestamp_t rampEnd = isStart(m.type) ? m.time : m.anchor + m.length;
        return m.anchor < to && rampEnd >= from;
    };

    return std::any_of(m_hard.cbegin(), m_hard.cend(), rampOverlaps) || std::any_of(m_soft.cbegin(), m_soft.cend(), rampOverlaps);
}

bool PlaybackStateTimeline::isAwake(timestamp_t from, timestamp_t to, bool ringingOut) const
{
    // Hard: asleep only when fully off for the whole block
    const bool hardAnchorInBlock = std::any_of(m_hard.cbegin(), m_hard.cend(), [from, to](const Marking& m) {
        return m.anchor > from && m.anchor < to;
    });

    if (!hardAnchorInBlock && hardGainAt(from) <= 0.f && hardGainAt(to) <= 0.f) {
        return false;
    }

    // Soft
    const Marking* m = lastSoft(to - 1);
    if (!m || m->type == Type::Live) {
        return true;
    }

    //! NOTE Still fading out (or waiting to cut) after the Idle marking, or the block starts before it
    if (from < m->anchor + m->length) {
        return true;
    }

    if (hasNoteNear(from, to, m->length)) {
        return true;
    }

    return ringingOut && mayRingOut(from);
}

bool PlaybackStateTimeline::mayRingOut(timestamp_t t) const
{
    const Marking* m = lastSoft(t);
    if (!m || m->type != Type::Idle) {
        return false;
    }

    const std::optional<timestamp_t> lastEnd = lastNoteEndBefore(t);
    if (!lastEnd.has_value() || *lastEnd <= m->time) {
        return false;
    }

    return t - *lastEnd < RING_OUT_LIMIT;
}

bool PlaybackStateTimeline::hasNoteNear(timestamp_t from, timestamp_t to, duration_t lead) const
{
    //! NOTE The spans are merged, so their ends are sorted too: the first one ending after `from`
    //! is the only candidate
    auto it = std::upper_bound(m_notes.cbegin(), m_notes.cend(), from, [](timestamp_t time, const NoteSpan& span) {
        return time < span.end;
    });

    return it != m_notes.cend() && it->start - lead < to;
}

std::optional<timestamp_t> PlaybackStateTimeline::lastNoteEndBefore(timestamp_t t) const
{
    auto it = std::upper_bound(m_notes.cbegin(), m_notes.cend(), t, [](timestamp_t time, const NoteSpan& span) {
        return time < span.end;
    });

    if (it == m_notes.cbegin()) {
        return std::nullopt;
    }

    return std::prev(it)->end;
}

std::optional<timestamp_t> PlaybackStateTimeline::firstSoundWithoutNote() const
{
    std::optional<timestamp_t> result;

    auto consider = [&result](const Marking& m) {
        if (isStart(m.type) && (!result.has_value() || m.anchor < *result)) {
            result = m.anchor;
        }
    };

    std::for_each(m_hard.cbegin(), m_hard.cend(), consider);
    std::for_each(m_soft.cbegin(), m_soft.cend(), consider);

    return result;
}
