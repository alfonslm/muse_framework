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

#include <atomic>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "global/async/asyncable.h"

#include "audiosourcenode.h"

#include "audio/common/audiocliptypes.h"
#include "../codecs/wavdecoder.h"

namespace muse::audio::engine {
//! NOTE The source of an audio clip track (FontDaw, FRAMEWORK_CHANGES F1 step 2).
//! Plays a list of clips over decoded audio, following the transport the way the event sources do:
//! seek() sets the position, process() moves it forward while the mode is a playing one.
//!
//! Threads
//! - engine thread: construction, setClips() (also from the `clipsChanged` channel), seek(),
//!   firstNoteTime(), output spec changes, destruction;
//! - processing thread (the driver's callback, or an export worker): doSelfProcess() only.
//!
//! The clip list the processing thread reads is a ClipSet: built and owned by the engine thread,
//! immutable once published, freed by the engine thread. Publishing and picking up a list is
//! wait-free for the processing thread (one atomic load and one atomic store per block, no
//! allocation, no lock); see setClips() and doSelfProcess().
//!
//! Sources are decoded into memory when the node is made (as in C2; 01b moves decoding off the
//! RPC thread) and never change. Sample rate conversion is done by position, as in C2: no state
//! between blocks, so seek, loop and offline export are exact.
class ClipAudioNode : public AudioSourceNode, public async::Asyncable
{
public:
    using Sources = std::map<std::string /*key*/, std::shared_ptr<const codec::PcmAudio> >;

    //! Where two clips overlap, the earlier one stops at the later one's start, faded out over
    //! this long (or its whole remaining length if shorter), so the cut does not click.
    //! 5 ms: well below what is heard as a fade, long enough to remove the click of a hard cut.
    static constexpr double OVERLAP_CUT_FADE_SECS = 0.005;

    ClipAudioNode(Sources sources, const AudioClipList& clips);
    ~ClipAudioNode() override;

    //! The app's channel for new lists (AudioClipTrackData::clipsChanged)
    void listenToClipChanges(async::Channel<AudioClipList> clipsChanged);

    //! Builds a ClipSet from `clips` and publishes it. Engine thread only.
    void setClips(const AudioClipList& clips);

    //! Lists still held for the processing thread (the current one included). Engine thread; for tests.
    size_t keptClipSets() const { return m_sets.size(); }

    //! Start of the first clip that sounds; nullopt if none does (the one-pass export skips such tracks)
    std::optional<secs_t> firstNoteTime() const override;

    void seek(const TimePosition& position, const bool flushSound = true) override;
    void flush() override;

    const AudioInputParams& inputParams() const override;
    void applyInputParams(const AudioInputParams& requiredParams) override;
    async::Channel<AudioInputParams> inputParamsChanged() const override;

    void prepareToPlay() override;
    bool readyToPlay() const override;
    async::Notification readyToPlayChanged() const override;

    bool hasPendingChunks() const override;
    void processInput() override;
    InputProcessingProgress inputProcessingProgress() const override;

    void clearCache() override;

private:
    //! A clip as the processing thread plays it: checked, cut to its source and to the next
    //! clip (overlap rule), in seconds so it does not depend on the output sample rate.
    struct PlayableClip {
        const codec::PcmAudio* audio = nullptr; // owned by m_sources, which outlives every ClipSet
        double start = 0.0;          // timeline seconds
        double end = 0.0;            // timeline seconds, after the overlap cut; > start
        double sourceOffset = 0.0;   // source seconds at `start`
        float gain = 1.f;            // linear
        AudioFade fadeIn;
        AudioFade fadeOut;           // ends at the clip's own end (before the cut)
        double ownEnd = 0.0;         // the clip's own end, where its fadeOut ends
        double cutFade = 0.0;        // > 0 when cut by a later clip: fade length ending at `end`
        //! Plays to the end of its source and is not cut: when resampling, the converter's tail may ring on
        //! after `end` (as a whole file always did), up to `tailLimit` (the next clip's start)
        bool ringsOut = false;
        double tailLimit = 0.0;
    };

    //! Immutable once published. Sorted by start; the clips do not overlap.
    struct ClipSet {
        uint64_t seq = 0;
        std::vector<PlayableClip> clips;
        std::optional<secs_t> firstSound;
    };

    //! The rules for one list: checks, cutting to the source, muted clips out, overlap cut
    std::unique_ptr<ClipSet> buildClipSet(const AudioClipList& clips, uint64_t seq) const;

    //! Frees every set the processing thread can no longer be reading. Engine thread.
    void freeRetiredSets();

    void onOutputSpecChanged(const OutputSpec& spec) override;
    void doSelfProcess(float* buffer, samples_t samplesPerChannel) override;
    void render(const ClipSet* set, float* buffer, samples_t samplesPerChannel);

    //! Adds the clip's part of the block [blockStart, blockStart + frames) (output frames) to `buffer`
    void renderClip(const PlayableClip& clip, samples_t blockStart, float* buffer, samples_t frames) const;

    //! Gain of the clip's fades and overlap cut at timeline second `t`, inside [start, end)
    static float envelopeAt(const PlayableClip& clip, double t);

    Sources m_sources;

    //! Engine thread: every set that may still be read, oldest first; the last one is current
    std::deque<std::unique_ptr<const ClipSet> > m_sets;
    uint64_t m_lastSeq = 0;

    //! The newest set; written by the engine thread, read by the processing thread.
    //! These three are sequentially consistent: freeRetiredSets() relies on their single total order.
    std::atomic<const ClipSet*> m_current = nullptr;
    //! seq of the set the processing thread read last; it never reads an older one again
    std::atomic<uint64_t> m_inUseSeq = 0;
    //! True while the processing thread is inside a block. A muted or skipped track is never processed,
    //! so without this its old sets could not be freed until it plays again.
    std::atomic<bool> m_processing = false;

    //! Frames of the output sample rate since time zero.
    //! Written by seek() (engine thread) and by process() (processing thread).
    std::atomic<samples_t> m_position = 0;
    sample_rate_t m_positionSampleRate = 0;

    AudioInputParams m_params;
    async::Channel<AudioInputParams> m_paramsChanges;
    async::Notification m_readyToPlayChanged;
};

using ClipAudioNodePtr = std::shared_ptr<ClipAudioNode>;
}
