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

#include "parallelsoundtrackwriter.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <thread>

#include "global/types/number.h"
#include "audio/common/audioerrors.h"

#include "encoderfactory.h"

#include "log.h"

using namespace muse;
using namespace muse::audio;
using namespace muse::audio::engine;
using namespace muse::audio::soundtrack;

static bool isChainSilent(const TrackChainPtr& chain)
{
    if (auto signal = chain->signal()) {
        return signal->isSilent();
    }
    return false;
}

static void mixInto(float* dst, const float* src, size_t size, float gain = 1.f)
{
    for (size_t i = 0; i < size; ++i) {
        dst[i] += src[i] * gain;
    }
}

ParallelSoundTrackWriter::ParallelSoundTrackWriter(std::vector<Job> jobs, std::vector<AuxChannels> auxChannelsPerWorker,
                                                   const SoundTrackFormat& format, const secs_t totalDuration)
    : m_auxChannelsPerWorker(std::move(auxChannelsPerWorker))
{
    IF_ASSERT_FAILED(format.isValid()) {
        return;
    }

    m_outputSpec = format.outputSpec;

    auto durationToSamples = [this](const secs_t& duration) {
        const double sec = std::max(0.0, duration.raw());
        return static_cast<samples_t>(std::llround(sec * static_cast<double>(m_outputSpec.sampleRate)));
    };

    m_dataSamples = durationToSamples(totalDuration);
    m_leadingSilenceSamples = durationToSamples(format.leadingSilenceDuration);
    const samples_t trailingSilenceSamples = durationToSamples(format.trailingSilenceDuration);
    m_totalSamples = m_leadingSilenceSamples + m_dataSamples + trailingSilenceSamples;

    m_jobs.reserve(jobs.size());
    for (Job& job : jobs) {
        IF_ASSERT_FAILED(job.dstDevice && job.tracks.size() == job.auxSends.size()) {
            m_hasEncodeError = true;
            continue;
        }

        encode::AbstractAudioEncoderPtr encoder = createEncoder(format, *job.dstDevice);
        if (!encoder || !encoder->begin(m_totalSamples)) {
            LOGE() << "Failed to start encoder";
            m_hasEncodeError = true;
            continue;
        }

        auto encodedJob = std::make_unique<EncodedJob>();
        encodedJob->job = std::move(job);
        encodedJob->encoder = std::move(encoder);
        m_jobs.push_back(std::move(encodedJob));
    }
}

Ret ParallelSoundTrackWriter::write()
{
    TRACEFUNC;

    if (m_jobs.empty() || m_hasEncodeError || m_totalSamples == 0) {
        return make_ret(Err::NoAudioToExport);
    }

    IF_ASSERT_FAILED(!m_auxChannelsPerWorker.empty()) {
        return make_ret(Err::NoAudioToExport);
    }

    //! NOTE The borrowed tracks get their mode/spec from the mixer (see AudioContext),
    //! the aux copies belong to this export only
    for (AuxChannels& auxChannels : m_auxChannelsPerWorker) {
        for (TrackChainPtr& aux : auxChannels) {
            if (aux) {
                aux->setOutputSpec(m_outputSpec);
                aux->setMode(ProcessMode::PlayingOffline);
            }
        }
    }

    const size_t workerCount = std::min(m_auxChannelsPerWorker.size(), m_jobs.size());
    LOGI() << "Exporting " << m_jobs.size() << " files on " << workerCount << " threads";

    std::vector<std::thread> workers;
    workers.reserve(workerCount);
    for (size_t i = 0; i < workerCount; ++i) {
        workers.emplace_back(&ParallelSoundTrackWriter::workerLoop, this, i);
    }

    //! NOTE This (engine) thread only reports progress and handles incoming messages (e.g. abort).
    //! The workers never send messages themselves, see SignalNode::notifyAboutChanges()
    const uint64_t totalFrames = static_cast<uint64_t>(m_totalSamples) * m_jobs.size();
    int lastProgress = -1;

    auto allJobsDone = [this]() {
        for (const auto& encodedJob : m_jobs) {
            if (encodedJob->framesWritten.load() < m_totalSamples) {
                return false;
            }
        }
        return true;
    };

    while (true) {
        uint64_t framesWritten = 0;
        for (const auto& encodedJob : m_jobs) {
            framesWritten += encodedJob->framesWritten.load();
        }

        const int current = static_cast<int>((framesWritten * 100) / totalFrames);
        if (current != lastProgress) {
            lastProgress = current;
            m_progress.progress(current, 100);
        }

        rpcChannel()->process();

        if (m_isAborted || m_hasEncodeError || allJobsDone() || m_nextJobIdx.load() >= m_jobs.size() + workerCount) {
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    for (std::thread& worker : workers) {
        worker.join();
    }

    if (m_isAborted) {
        m_isAborted = false;
        return make_ret(Ret::Code::Cancel);
    }

    for (auto& encodedJob : m_jobs) {
        encodedJob->encoder->end();
    }

    if (m_hasEncodeError) {
        return make_ret(Err::ErrorEncode);
    }

    return muse::make_ok();
}

void ParallelSoundTrackWriter::abort()
{
    m_isAborted = true;
}

Progress ParallelSoundTrackWriter::progress()
{
    return m_progress;
}

void ParallelSoundTrackWriter::workerLoop(size_t workerIdx)
{
    const AuxChannels& auxChannels = m_auxChannelsPerWorker.at(workerIdx);

    while (!m_isAborted && !m_hasEncodeError) {
        const size_t jobIdx = m_nextJobIdx.fetch_add(1);
        if (jobIdx >= m_jobs.size()) {
            break;
        }

        if (!renderJob(*m_jobs.at(jobIdx), auxChannels)) {
            m_hasEncodeError = true;
        }
    }
}

bool ParallelSoundTrackWriter::renderJob(EncodedJob& encodedJob, const AuxChannels& auxChannels)
{
    const Job& job = encodedJob.job;
    encode::AbstractAudioEncoder& encoder = *encodedJob.encoder;

    const size_t channelCount = m_outputSpec.audioChannelCount;
    const samples_t renderStep = m_outputSpec.samplesPerChannel;
    const size_t bufferSize = renderStep * channelCount;

    //! NOTE Only the aux channels this job actually sends to are processed. The others would get
    //! no input and stay silent, so Mixer::process() would skip them as well
    std::vector<bool> auxUsed(auxChannels.size(), false);
    for (const AuxSendsParams& sends : job.auxSends) {
        for (size_t auxIdx = 0; auxIdx < sends.size() && auxIdx < auxChannels.size(); ++auxIdx) {
            if (auxChannels.at(auxIdx) && sends.at(auxIdx).active && !muse::is_zero(sends.at(auxIdx).signalAmount)) {
                auxUsed[auxIdx] = true;
            }
        }
    }

    //! NOTE This worker's aux copies may still hold the tail of its previous job
    for (size_t auxIdx = 0; auxIdx < auxChannels.size(); ++auxIdx) {
        if (auxUsed[auxIdx]) {
            if (FxChainPtr fxChain = auxChannels.at(auxIdx)->fxChain()) {
                fxChain->resetState();
            }
        }
    }

    std::vector<float> mixBuffer(bufferSize, 0.f);
    std::vector<float> trackBuffer(bufferSize, 0.f);
    std::vector<std::vector<float> > auxBuffers(auxChannels.size());
    std::vector<bool> auxReceived(auxChannels.size(), false);
    for (size_t auxIdx = 0; auxIdx < auxChannels.size(); ++auxIdx) {
        if (auxUsed[auxIdx]) {
            auxBuffers[auxIdx].assign(bufferSize, 0.f);
        }
    }

    auto encode = [&](samples_t chunk, const float* buffer) {
        if (encoder.encode(chunk, buffer) == 0) {
            LOGE() << "Failed to encode";
            return false;
        }

        encodedJob.framesWritten += chunk;
        return true;
    };

    samples_t framesWritten = 0;

    // Phase 1: leading silence
    while (framesWritten < m_leadingSilenceSamples) {
        if (m_isAborted) {
            return true;
        }

        const samples_t chunk = std::min<samples_t>(renderStep, m_leadingSilenceSamples - framesWritten);
        if (!encode(chunk, mixBuffer.data())) {
            return false;
        }
        framesWritten += chunk;
    }

    // Phase 2: actual audio data, mixed like Mixer::process()
    const samples_t audioEnd = m_leadingSilenceSamples + m_dataSamples;
    while (framesWritten < audioEnd) {
        if (m_isAborted) {
            return true;
        }

        const samples_t chunk = std::min<samples_t>(renderStep, audioEnd - framesWritten);
        const size_t chunkSize = chunk * channelCount;

        std::fill(mixBuffer.begin(), mixBuffer.end(), 0.f);
        for (size_t auxIdx = 0; auxIdx < auxChannels.size(); ++auxIdx) {
            if (auxUsed[auxIdx]) {
                std::fill(auxBuffers[auxIdx].begin(), auxBuffers[auxIdx].end(), 0.f);
                auxReceived[auxIdx] = false;
            }
        }

        for (size_t trackIdx = 0; trackIdx < job.tracks.size(); ++trackIdx) {
            const TrackChainPtr& track = job.tracks.at(trackIdx);

            std::fill(trackBuffer.begin(), trackBuffer.end(), 0.f);
            track->process(trackBuffer.data(), chunk);

            if (isChainSilent(track)) {
                continue;
            }

            mixInto(mixBuffer.data(), trackBuffer.data(), chunkSize);

            const AuxSendsParams& sends = job.auxSends.at(trackIdx);
            for (size_t auxIdx = 0; auxIdx < sends.size() && auxIdx < auxChannels.size(); ++auxIdx) {
                const AuxSendParams& send = sends.at(auxIdx);
                if (!auxUsed[auxIdx] || !send.active || muse::is_zero(send.signalAmount)) {
                    continue;
                }

                mixInto(auxBuffers[auxIdx].data(), trackBuffer.data(), chunkSize, send.signalAmount);
                auxReceived[auxIdx] = true;
            }
        }

        for (size_t auxIdx = 0; auxIdx < auxChannels.size(); ++auxIdx) {
            if (!auxUsed[auxIdx]) {
                continue;
            }

            //! NOTE Process when the aux received a signal this block and/or if it's not yet silent
            //! (e.g. reverb is still ringing out), same as Mixer::processAuxChannels()
            const TrackChainPtr& aux = auxChannels.at(auxIdx);
            if (!auxReceived[auxIdx] && isChainSilent(aux)) {
                continue;
            }

            aux->process(auxBuffers[auxIdx].data(), chunk);

            if (!isChainSilent(aux)) {
                mixInto(mixBuffer.data(), auxBuffers[auxIdx].data(), chunkSize);
            }
        }

        if (!encode(chunk, mixBuffer.data())) {
            return false;
        }
        framesWritten += chunk;
    }

    // Phase 3: trailing silence
    std::fill(mixBuffer.begin(), mixBuffer.end(), 0.f);
    while (framesWritten < m_totalSamples) {
        if (m_isAborted) {
            return true;
        }

        const samples_t chunk = std::min<samples_t>(renderStep, m_totalSamples - framesWritten);
        if (!encode(chunk, mixBuffer.data())) {
            return false;
        }
        framesWritten += chunk;
    }

    return true;
}
