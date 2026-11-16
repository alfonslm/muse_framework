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

#include "audiodecoder.h"

#include <algorithm>
#include <cstring>

#include "audio/common/audioerrors.h"

#include "wavaudiodecoder.h"

using namespace muse;
using namespace muse::audio;
using namespace muse::audio::codec;

RetVal<IAudioDecoderPtr> codec::makeDecoder(const uint8_t* data, size_t size)
{
    if (data && size >= 12 && std::memcmp(data, "RIFF", 4) == 0 && std::memcmp(data + 8, "WAVE", 4) == 0) {
        IAudioDecoderPtr decoder = std::make_unique<WavAudioDecoder>();
        Ret ret = decoder->open(data, size);
        if (!ret) {
            return RetVal<IAudioDecoderPtr>::make_ret(ret);
        }
        RetVal<IAudioDecoderPtr> result; // make_ok copies, and the decoder cannot be copied
        result.ret = muse::make_ok();
        result.val = std::move(decoder);
        return result;
    }

    return RetVal<IAudioDecoderPtr>::make_ret(make_ret(Err::InvalidAudioSource, "unrecognised audio format"));
}

RetVal<PcmAudio> codec::decodeAll(IAudioDecoder& decoder, size_t maxSamples)
{
    const AudioStreamInfo info = decoder.info();
    if (info.channels == 0 || info.sampleRate == 0) {
        return RetVal<PcmAudio>::make_ret(make_ret(Err::InvalidAudioSource, "audio without channels or sample rate"));
    }

    const size_t frames = static_cast<size_t>(info.frames);
    if (frames > maxSamples / info.channels) {
        return RetVal<PcmAudio>::make_ret(make_ret(Err::InvalidAudioSource, "audio file is too long to load"));
    }

    if (!decoder.seek(0)) {
        return RetVal<PcmAudio>::make_ret(make_ret(Err::InvalidAudioSource, "audio cannot be read"));
    }

    PcmAudio audio;
    audio.sampleRate = info.sampleRate;
    audio.channels = info.channels;
    audio.samples.resize(frames * info.channels);

    constexpr size_t CHUNK_FRAMES = 4096;
    size_t done = 0;
    while (done < frames) {
        const size_t want = std::min(CHUNK_FRAMES, frames - done);
        const samples_t got = decoder.readFrames(audio.samples.data() + done * info.channels, static_cast<samples_t>(want));
        if (got == 0) {
            break;
        }
        done += got;
    }

    audio.samples.resize(done * info.channels);
    return RetVal<PcmAudio>::make_ok(std::move(audio));
}
