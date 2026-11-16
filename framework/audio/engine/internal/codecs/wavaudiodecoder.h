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

#include "audiodecoder.h"

namespace muse::audio::codec {
//! IAudioDecoder for RIFF/WAVE. open() parses the header with the same rules and limits as
//! WavDecoder::decode (parseWav); readFrames() converts the samples on demand.
//! seek() past the end (frame > info().frames) fails and leaves the position where it was;
//! seek(info().frames) is valid and the next readFrames() returns 0.
class WavAudioDecoder : public IAudioDecoder
{
public:
    Ret open(const uint8_t* data, size_t size) override;
    AudioStreamInfo info() const override;
    samples_t readFrames(float* dst, samples_t frames) override;
    bool seek(samples_t frame) override;

private:
    WavLayout m_layout;
    bool m_opened = false;
    size_t m_position = 0; // in frames
};
}
