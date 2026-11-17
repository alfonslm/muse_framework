/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * FontDaw
 * The notation-first DAW
 *
 * Copyright (C) 2026 FontDaw contributors
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

#include "sndfileaudiodecoder.h"

#include <sndfile.h>

#include "audio/common/audioerrors.h"

using namespace muse;
using namespace muse::audio;
using namespace muse::audio::codec;

// TODO(FontDaw 04): implement as described in sndfileaudiodecoder.h (Batch 2 workstream 04)

SndFileAudioDecoder::~SndFileAudioDecoder()
{
    close();
}

void SndFileAudioDecoder::close()
{
}

Ret SndFileAudioDecoder::open(const uint8_t* data, size_t size)
{
    (void)data;
    (void)size;
    return make_ret(Err::InvalidAudioSource, "not implemented yet");
}

AudioStreamInfo SndFileAudioDecoder::info() const
{
    return m_info;
}

samples_t SndFileAudioDecoder::readFrames(float* dst, samples_t frames)
{
    (void)dst;
    (void)frames;
    return 0;
}

bool SndFileAudioDecoder::seek(samples_t frame)
{
    (void)frame;
    return false;
}

Ret SndFileAudioDecoder::error() const
{
    return m_error;
}
