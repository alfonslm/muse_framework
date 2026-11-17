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

#include <cstdint>
#include <string>
#include <vector>

#include "global/async/channel.h"
#include "global/types/secs.h"

namespace muse::audio {
//! Audio clip tracks (FontDaw, FRAMEWORK_CHANGES F1 step 2)
//! ======================================================
//! A clip track plays any number of windows ("clips") onto decoded audio files ("sources").
//! The app describes the whole track in one AudioClipTrackData and replaces the clip list
//! while playing through `clipsChanged`, the way mpe::PlaybackData carries notation events and
//! their updates (an RPC stream: see IPlayback::addTrack and EngineRpcController).
//!
//! Every value can come from a project file, so the engine checks them (see ClipAudioNode):
//! clips with non-finite or negative values are dropped, windows are cut to the audio.

enum class AudioFadeCurve : uint8_t {
    Linear = 0,     //! gain = x
    EqualPower,     //! gain = sin(x * pi / 2) (fade in); a fade out mirrors it in time
};

struct AudioFade {
    secs_t length = 0.0;
    AudioFadeCurve curve = AudioFadeCurve::Linear;

    //! Exact: secs_t's own == is relative (number_t), which would hide a small move late on the timeline
    bool operator==(const AudioFade& o) const { return length.raw() == o.length.raw() && curve == o.curve; }
};

//! One audio file of a clip track. Sources are fixed when the track is added: a clip list sent
//! later can only use these keys. A track whose set of files changes is added again.
struct AudioClipSource {
    std::string key;      //! names the source for the clips, e.g. the app's asset id

    //! An io::IODevice*, sent as a number: the engine is in the app's process in every work mode
    //! that can add such a track (FD-015 question 3; the web build refuses the call). The engine
    //! reads it while it adds the track and never afterwards.
    uint64_t device = 0;

    bool operator==(const AudioClipSource& o) const { return key == o.key && device == o.device; }
};

//! A window onto a source, placed on the timeline. All times are seconds.
struct AudioClip {
    std::string sourceKey;
    secs_t startSecs = 0.0;          //! where the clip starts on the timeline
    secs_t sourceOffsetSecs = 0.0;   //! where in the source it starts playing
    secs_t lengthSecs = 0.0;         //! source seconds; cut to what the source has
    float gainDb = 0.f;
    AudioFade fadeIn;                //! from the clip's start
    AudioFade fadeOut;               //! up to the clip's end
    bool muted = false;              //! muted clips neither sound nor cut other clips

    //! Reserved for beat matching (F7): timeline seconds per source second. Must be 1 until then;
    //! other values are played at 1 with a warning.
    double stretchRatio = 1.0;

    bool operator==(const AudioClip& o) const
    {
        //! Exact: secs_t's own == is relative (number_t), which would hide a small move late on the timeline
        return sourceKey == o.sourceKey && startSecs.raw() == o.startSecs.raw()
               && sourceOffsetSecs.raw() == o.sourceOffsetSecs.raw()
               && lengthSecs.raw() == o.lengthSecs.raw() && gainDb == o.gainDb && fadeIn == o.fadeIn && fadeOut == o.fadeOut
               && muted == o.muted && stretchRatio == o.stretchRatio;
    }
};

//! In any order. Overlaps are allowed (Musical clips can overlap in seconds after a tempo
//! change): where two clips overlap, the one that starts later plays and the earlier one stops
//! at its start with a short fade (ClipAudioNode::OVERLAP_CUT_FADE_SECS). Of two clips that
//! start together the later in the list plays.
using AudioClipList = std::vector<AudioClip>;

//! What IPlayback::addTrack takes for a clip track. Mirrors mpe::PlaybackData: the data, plus a
//! channel the app sends a whole new clip list on. Lists sent before the engine has answered
//! are applied in order once the track exists.
struct AudioClipTrackData {
    std::vector<AudioClipSource> sources;
    AudioClipList clips;

    async::Channel<AudioClipList> clipsChanged;
};
}
