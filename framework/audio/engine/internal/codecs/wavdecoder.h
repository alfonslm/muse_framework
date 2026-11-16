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

#include <cstddef>
#include <cstdint>
#include <vector>

#include "global/types/retval.h"

#include "audio/common/audiotypes.h"

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

//! RIFF/WAVE: PCM 8/16/24/32 bit integer and 32/64 bit float,
//! plain or WAVE_FORMAT_EXTENSIBLE.
//! Works on bytes that are already in memory, never touches the file system.
class WavDecoder
{
public:
    static constexpr audioch_t MAX_CHANNELS = 8;
    static constexpr sample_rate_t MAX_SAMPLE_RATE = 384000;

    static RetVal<PcmAudio> decode(const uint8_t* data, size_t size);
};
}
