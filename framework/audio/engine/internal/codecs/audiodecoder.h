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
#include <memory>

#include "global/types/ret.h"
#include "global/types/retval.h"

#include "audio/common/audiotypes.h"

#include "wavdecoder.h"

namespace muse::audio::codec {
//! The decoder seam (FontDaw, FRAMEWORK_CHANGES F1 step 2 and F4)
//! ===========================================================
//! Everything the engine decodes for a clip track goes through IAudioDecoder.
//! WAV (WavDecoder) is the only format for now; F4 (Batch 2 workstream 04) adds FLAC, MP3 and OGG
//! behind the same interface, and 01b changes when and on which thread it is called.
//!
//! A decoder works on bytes that are already in memory and never touches the file system. The
//! bytes come from project files, so each implementation checks its input and makeDecoder()
//! refuses what no decoder recognises.

struct AudioStreamInfo {
    sample_rate_t sampleRate = 0;
    audioch_t channels = 0;
    samples_t frames = 0;
};

class IAudioDecoder
{
public:
    virtual ~IAudioDecoder() = default;

    //! `data` stays owned by the caller and must outlive the decoder
    virtual Ret open(const uint8_t* data, size_t size) = 0;

    //! Valid after a successful open()
    virtual AudioStreamInfo info() const = 0;

    //! Reads up to `frames` frames (interleaved floats, info().channels per frame) from the current
    //! position into `dst` and moves the position on. Returns the frames read: fewer at the end.
    virtual samples_t readFrames(float* dst, samples_t frames) = 0;

    //! Moves the read position to `frame` (0 <= frame <= info().frames)
    virtual bool seek(samples_t frame) = 0;
};

using IAudioDecoderPtr = std::unique_ptr<IAudioDecoder>;

//! A decoder that recognises `data` (by content, not by file name), already opened; or an error
RetVal<IAudioDecoderPtr> makeDecoder(const uint8_t* data, size_t size);

//! Decodes everything a decoder has, refusing audio longer than maxSamples (all channels together)
RetVal<PcmAudio> decodeAll(IAudioDecoder& decoder, size_t maxSamples = WavDecoder::MAX_SAMPLES);
}
