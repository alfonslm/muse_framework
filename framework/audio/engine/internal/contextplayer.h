/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * MuseScore Studio
 * Music Composition & Notation
 *
 * Copyright (C) 2021 MuseScore Limited and others
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
#include <memory>
#include <vector>

#include "global/types/retval.h"
#include "global/async/channel.h"
#include "global/async/asyncable.h"

#include "../iengineplayer.h"
#include "../iplayhead.h"

#include "iexecoperation.h"
#include "igettracksource.h"
#include "iprocesstiming.h"

namespace muse::audio::engine {
class ContextPlayer : public IEnginePlayer, public IPlayhead, public async::Asyncable
{
public:
    explicit ContextPlayer(IGetTrackSource* getTracks, IExecOperation* execOperation, const IProcessTiming* processTiming = nullptr);

    async::Promise<Ret> prepareToPlay() override;

    void play(const secs_t delay = 0) override;
    void seek(const TimePosition& position, const bool flushSound = true) override;
    void stop() override;
    void pause() override;
    void resume(const secs_t delay = 0) override;

    PlaybackStatus playbackStatus() const override;
    async::Channel<PlaybackStatus> playbackStatusChanged() const override;
    bool isActive() const override;
    async::Channel<bool> isActiveChanged() const override;

    secs_t duration() const override;
    void setDuration(const secs_t duration) override;

    Ret setLoop(const secs_t from, const secs_t to) override;
    void resetLoop() override;
    void setMusicalTimeMap(MusicalTimeMap map) override;

    secs_t playbackPosition() const override;
    async::Channel<secs_t> playbackPositionChanged() const override;

    PlaybackPositionAnchor playbackPositionAnchor() const override;
    async::Channel<PlaybackPositionAnchor> playbackPositionAnchorChanged() const override;

    // IPlayhead interface
    void forward(const TimePosition& delta) override;
    const TimePosition& currentPosition() const override;
    MusicalPosition musicalPosition(const TimePosition& position) const override;
    Loop loop() const override;
    int64_t processedSamples() const override;
    int64_t blockPresentationTimeNs() const override;

private:

    void exec(OperationType type, const Operation& func);

    void onStatusChanged(const PlaybackStatus status);

    // Processing thread functions
    TimePosition proc_onTimeChanged(const TimePosition& delta);
    // ----------------------------

    enum class TimeEventType {
        Undefined,
        PlaybackEnded,
        CountDownEnded,
        LoopEnded,
    };

    struct TimeEvent {
        TimeEventType type = TimeEventType::Undefined;
        TimePosition position;
    };

    void onTimeEvent(const TimeEvent event);

    void seekAllTracks(const TimePosition& position);
    void flushAllTracks();

    using AllTracksReadyCallback = std::function<void ()>;
    void prepareAllTracksToPlay(AllTracksReadyCallback allTracksReadyCallback);

    IGetTrackSource* m_trackSource = nullptr;
    IExecOperation* m_execOperation = nullptr;
    const IProcessTiming* m_processTiming = nullptr;

    ValCh<PlaybackStatus> m_status;
    ValCh<bool> m_isActive;

    TimePosition m_currentPosition;
    async::Channel<secs_t> m_timeChanged;

    //! NOTE Positions linked to the time they are heard.
    //! The epoch is changed by an explicit position change (engine thread)
    //! and read to stamp the rendered blocks (processing thread)
    async::Channel<PlaybackPositionAnchor> m_anchorChanged;
    std::atomic<uint32_t> m_anchorEpoch = 0;

    secs_t m_timeDuration = 0.;
    secs_t m_countDown = 0.;
    secs_t m_timeLoopStart = 0.;
    secs_t m_timeLoopEnd = 0.;

    //! The musical time map. The engine thread owns it (m_musicalTimeMap) and publishes it to the
    //! processing thread through an atomic pointer. A replaced map is kept (m_retiredMaps) until two
    //! more blocks have been processed, so that a block that started before the swap can finish
    //! reading it; it is then freed on the engine thread, never on the processing thread.
    struct RetiredMap {
        std::shared_ptr<const MusicalTimeMap> map;
        uint64_t retiredAtBlock = 0;
    };

    void freeRetiredMaps();

    std::shared_ptr<const MusicalTimeMap> m_musicalTimeMap;
    std::atomic<const MusicalTimeMap*> m_musicalTimeMapForProc = nullptr;
    std::vector<RetiredMap> m_retiredMaps;
    std::atomic<uint64_t> m_processedBlocks = 0;
    std::atomic<int64_t> m_processedSamples = 0;

    async::Channel<TimeEvent> m_timeEvent;

    bool m_flushSoundOnSeek = true;
    std::set<AudioSourceNodePtr> m_notYetReadyToPlayTracks;
};
}
