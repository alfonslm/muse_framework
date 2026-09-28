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

#include <atomic>
#include <memory>
#include <vector>

#include "global/async/asyncable.h"

#include "global/modularity/ioc.h"
#include "audio/common/rpc/irpcchannel.h"

#include "audio/common/audiotypes.h"
#include "../nodes/trackchain.h"

#include "abstractaudioencoder.h"

namespace muse::io {
class IODevice;
}

namespace muse::audio::soundtrack {
//! NOTE Renders several audio files (typically one per part) at the same time, on a pool of worker
//! threads. Each file is a job: the worker that takes it renders only that job's tracks, mixes them
//! the same way Mixer::process() does (track outputs plus the aux sends run through the aux
//! channels) and encodes the result. The result is the same as exporting that file on its own with
//! every other track muted, which is what the per-file export does.
//!
//! Tracks are borrowed from the live mixer: a job's tracks must not be part of any other job, so
//! each track is only ever processed by one thread. The aux channels are shared by all tracks, so
//! each worker gets its own copy of them (AuxChannels), created and fully loaded by the caller.
class ParallelSoundTrackWriter : public async::Asyncable
{
    muse::GlobalInject<rpc::IRpcChannel> rpcChannel;

public:
    struct Job {
        std::vector<engine::TrackChainPtr> tracks;
        std::vector<AuxSendsParams> auxSends; // per track, same order as tracks
        io::IODevice* dstDevice = nullptr;
    };

    //! NOTE One worker's copy of the aux channels, indexed like AuxSendsParams.
    //! A null entry means that aux channel has no fx chain, which Mixer skips as well
    using AuxChannels = std::vector<engine::TrackChainPtr>;

    //! NOTE The number of workers is auxChannelsPerWorker.size()
    ParallelSoundTrackWriter(std::vector<Job> jobs, std::vector<AuxChannels> auxChannelsPerWorker, const SoundTrackFormat& format,
                             const secs_t totalDuration);

    Ret write();
    void abort();

    Progress progress();

private:
    struct EncodedJob {
        Job job;
        encode::AbstractAudioEncoderPtr encoder;
        std::atomic<samples_t> framesWritten = 0;
    };

    void workerLoop(size_t workerIdx);
    bool renderJob(EncodedJob& encodedJob, const AuxChannels& auxChannels);

    std::vector<std::unique_ptr<EncodedJob> > m_jobs;
    std::vector<AuxChannels> m_auxChannelsPerWorker;

    OutputSpec m_outputSpec;
    samples_t m_leadingSilenceSamples = 0;
    samples_t m_dataSamples = 0;
    samples_t m_totalSamples = 0;

    std::atomic<size_t> m_nextJobIdx = 0;
    std::atomic<bool> m_hasEncodeError = false;
    std::atomic<bool> m_isAborted = false;

    Progress m_progress;
};
}
