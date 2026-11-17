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

#include "wavaudiodecoder.h"

#include <algorithm>

using namespace muse;
using namespace muse::audio;
using namespace muse::audio::codec;

Ret WavAudioDecoder::open(const uint8_t* data, size_t size)
{
    m_opened = false;
    m_position = 0;

    RetVal<WavLayout> parsed = parseWav(data, size);
    if (!parsed.ret) {
        return parsed.ret;
    }

    m_layout = parsed.val;
    m_opened = true;
    return muse::make_ok();
}

AudioStreamInfo WavAudioDecoder::info() const
{
    AudioStreamInfo info;
    if (!m_opened) {
        return info;
    }

    info.sampleRate = static_cast<sample_rate_t>(m_layout.sampleRate);
    info.channels = static_cast<audioch_t>(m_layout.channels);
    info.frames = static_cast<samples_t>(m_layout.frames);
    return info;
}

samples_t WavAudioDecoder::readFrames(float* dst, samples_t frames)
{
    if (!m_opened || !dst || frames == 0) {
        return 0;
    }

    const size_t left = m_layout.frames - m_position;
    const size_t count = std::min(static_cast<size_t>(frames), left);

    const size_t bytesPerSample = m_layout.bitsPerSample / 8;
    const uint8_t* p = m_layout.samples + m_position * m_layout.blockAlign;
    for (size_t i = 0, n = count * m_layout.channels; i < n; ++i, p += bytesPerSample) {
        dst[i] = decodeWavSample(p, m_layout);
    }

    m_position += count;
    return static_cast<samples_t>(count);
}

bool WavAudioDecoder::seek(samples_t frame)
{
    if (!m_opened || frame > m_layout.frames) {
        return false;
    }

    m_position = static_cast<size_t>(frame);
    return true;
}
