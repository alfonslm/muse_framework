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

struct mpg123_handle_struct;

namespace muse::audio::codec {
//! IAudioDecoder for MP3 (MPEG-1/2/2.5 layer III), through libmpg123 (1.32 from muse_deps).
//! libsndfile's own MP3 support would need the same library and is built off in muse_deps.
//!
//! - The bytes are read through mpg123's reader callbacks (mpg123_reader64 + mpg123_open_handle)
//!   from memory; nothing touches the file system.
//! - Gapless (MPG123_GAPLESS): the encoder delay and padding a LAME/Xing "Info" tag declares are
//!   removed, so an MP3 made from a click track starts on the beat. A file without that tag (for
//!   example FontDaw's own MP3 export today, FD-110) keeps the encoder's delay.
//! - Output is float (MPG123_FORCE_FLOAT or the float encoding), interleaved, at the stream's rate
//!   and channel count. A stream that changes rate or channels part way is damaged (error()).
//! - open() scans the stream (mpg123_scan, which reads frame headers without decoding) so
//!   info().frames is exact; the length check in readAll() then happens before decoding. Damaged
//!   frames mpg123 skips are not an error; a stream with no decodable frame is refused by open().
//! - Libraries that need a one-time global init (mpg123_init, a no-op since 1.27) get it here,
//!   once, thread-safe.
//! - seek() past the end fails and leaves the position; seek(info().frames) is valid.
class Mp3AudioDecoder : public IAudioDecoder
{
public:
    Mp3AudioDecoder() = default;
    ~Mp3AudioDecoder() override;

    Mp3AudioDecoder(const Mp3AudioDecoder&) = delete;
    Mp3AudioDecoder& operator=(const Mp3AudioDecoder&) = delete;

    Ret open(const uint8_t* data, size_t size) override;
    AudioStreamInfo info() const override;
    samples_t readFrames(float* dst, samples_t frames) override;
    bool seek(samples_t frame) override;
    Ret error() const override;

    //! The bytes mpg123 reads through its reader callbacks
    struct Memory {
        const uint8_t* data = nullptr;
        size_t size = 0;
        size_t position = 0;
    };

private:
    void close();

    Memory m_memory;
    mpg123_handle_struct* m_handle = nullptr;
    AudioStreamInfo m_info;
    samples_t m_position = 0;
    Ret m_error = muse::make_ok();
};
}
