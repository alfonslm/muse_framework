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

#include <cstddef>
#include <cstdint>
#include <vector>

#include "global/types/retval.h"

#include "codectypes.h"

namespace muse::audio::codec {
//! Decoded audio: interleaved floats, nominally in [-1, 1]
struct PcmAudio {
    sample_rate_t sampleRate = 0;
    audioch_t channels = 0;
    std::vector<float> samples;

    samples_t frames() const
    {
        return channels > 0 ? static_cast<samples_t>(samples.size() / channels) : 0;
    }

    bool isValid() const
    {
        return sampleRate > 0 && channels > 0;
    }
};

//! Where the samples of a WAV file are and how they are stored (see parseWav)
struct WavLayout {
    uint16_t tag = 0;               //! 1 = integer PCM, 3 = IEEE float (extensible resolved)
    uint16_t channels = 0;
    uint32_t sampleRate = 0;
    uint16_t blockAlign = 0;        //! bytes per frame
    uint16_t bitsPerSample = 0;
    const uint8_t* samples = nullptr;   //! first frame, inside the parsed bytes
    size_t frames = 0;
};

//! Walks the RIFF chunks and checks the format (the rules and limits of WavDecoder::decode,
//! except the length limit). `samples` points into `data`, which must outlive the result.
RetVal<WavLayout> parseWav(const uint8_t* data, size_t size);

//! Converts one sample (at `p`) to float
float decodeWavSample(const uint8_t* p, const WavLayout& layout);

//! RIFF/WAVE: PCM 8/16/24/32 bit integer and 32/64 bit float,
//! plain or WAVE_FORMAT_EXTENSIBLE.
//! Works on bytes that are already in memory, never touches the file system.
//! Audio longer than maxSamples decoded samples (all channels together) is refused.
class WavDecoder
{
public:
    static constexpr audioch_t MAX_CHANNELS = 8;
    static constexpr sample_rate_t MAX_SAMPLE_RATE = 384000;

    //! The input can come from untrusted project files: without a limit a huge
    //! file would make the engine allocate many times its size
    static constexpr size_t MAX_SAMPLES = size_t(1) << 30; // decoded floats (4 GiB), all channels together

    static RetVal<PcmAudio> decode(const uint8_t* data, size_t size, size_t maxSamples = MAX_SAMPLES);
};
}
