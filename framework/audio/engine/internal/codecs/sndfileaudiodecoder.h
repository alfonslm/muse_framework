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

#pragma once

#include "audiodecoder.h"

struct sf_private_tag;

namespace muse::audio::codec {
//! IAudioDecoder for FLAC, Ogg Vorbis and Ogg Opus, through libsndfile (1.2.2 from muse_deps,
//! built with its external codec libraries and without MPEG).
//!
//! - The bytes are read through libsndfile's virtual I/O (sf_open_virtual) from memory; nothing
//!   touches the file system.
//! - open() accepts only FLAC and Ogg with Vorbis or Opus inside (SF_FORMAT_FLAC,
//!   SF_FORMAT_OGG|SF_FORMAT_VORBIS, SF_FORMAT_OGG|SF_FORMAT_OPUS), whatever else libsndfile could
//!   read: the list of formats is this decoder's, not the library's.
//! - Samples are read as normalised floats (sf_readf_float, SFC_SET_NORM_FLOAT on), so FLAC
//!   decodes bit-exactly to what WavDecoder gives for the same samples in a WAV file.
//! - Opus: libsndfile removes the pre-skip and the end padding given by the granule positions;
//!   the sample rate is what libsndfile reports (the stream's input rate when Opus supports it,
//!   otherwise 48000).
//! - info().frames is libsndfile's frame count. For FLAC it is the STREAMINFO total; a stream that
//!   ends before it is damaged (error()). A FLAC stream with an unknown total (0) is read to its end.
//! - seek() past the end fails and leaves the position; seek(info().frames) is valid.
class SndFileAudioDecoder : public IAudioDecoder
{
public:
    SndFileAudioDecoder() = default;
    ~SndFileAudioDecoder() override;

    SndFileAudioDecoder(const SndFileAudioDecoder&) = delete;
    SndFileAudioDecoder& operator=(const SndFileAudioDecoder&) = delete;

    Ret open(const uint8_t* data, size_t size) override;
    AudioStreamInfo info() const override;
    samples_t readFrames(float* dst, samples_t frames) override;
    bool seek(samples_t frame) override;
    Ret error() const override;

    //! The bytes libsndfile reads through its virtual I/O callbacks. `data` is the caller's (see
    //! IAudioDecoder::open) and must outlive the decoder: it is read on every readFrames() and seek().
    struct Memory {
        const uint8_t* data = nullptr;
        size_t size = 0;
        size_t position = 0;
    };

private:
    void close();

    Memory m_memory;
    sf_private_tag* m_file = nullptr;
    AudioStreamInfo m_info;
    bool m_lengthIsExact = false; // FLAC with a STREAMINFO total
    samples_t m_position = 0;
    Ret m_error = muse::make_ok();
};
}
