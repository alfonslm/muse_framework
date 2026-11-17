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

#include "mp3audiodecoder.h"

#include <algorithm>
#include <cstdint>
#include <mutex>

#include <mpg123.h>

#include "audio/common/audioerrors.h"

using namespace muse;
using namespace muse::audio;
using namespace muse::audio::codec;

namespace {
Ret mp3Error(const std::string& text)
{
    return make_ret(Err::InvalidAudioSource, text);
}

void initMpg123Once()
{
    static std::once_flag flag;
    std::call_once(flag, []() {
        mpg123_init(); // a no-op since 1.27, kept for older libraries
    });
}

//! mpg123 reader callback: copies from the memory block
int readMemory(void* handle, void* buffer, size_t count, size_t* got)
{
    auto* memory = static_cast<Mp3AudioDecoder::Memory*>(handle);
    if (!memory || !got || (!buffer && count > 0)) {
        return -1;
    }

    const size_t left = memory->size - std::min(memory->position, memory->size);
    const size_t n = std::min(count, left);
    if (n > 0) {
        std::copy_n(memory->data + memory->position, n, static_cast<uint8_t*>(buffer));
    }
    memory->position += n;
    *got = n;
    return 0;
}

//! mpg123 reader callback: lseek over the memory block; refuses positions outside [0, size]
int64_t seekMemory(void* handle, int64_t offset, int whence)
{
    auto* memory = static_cast<Mp3AudioDecoder::Memory*>(handle);
    if (!memory || memory->size > static_cast<size_t>(INT64_MAX)) {
        return -1;
    }

    const int64_t size = static_cast<int64_t>(memory->size);
    int64_t base = 0;
    switch (whence) {
    case SEEK_SET: base = 0;
        break;
    case SEEK_CUR: base = static_cast<int64_t>(std::min(memory->position, memory->size));
        break;
    case SEEK_END: base = size;
        break;
    default: return -1;
    }

    // base is within [0, size], so this cannot overflow unless offset is extreme
    if ((offset > 0 && offset > size - base) || (offset < 0 && offset < -base)) {
        return -1;
    }

    const int64_t target = base + offset;
    memory->position = static_cast<size_t>(target);
    return target;
}
}

Mp3AudioDecoder::~Mp3AudioDecoder()
{
    close();
}

void Mp3AudioDecoder::close()
{
    if (m_handle) {
        mpg123_close(m_handle);
        mpg123_delete(m_handle);
        m_handle = nullptr;
    }
    m_memory = Memory();
    m_info = AudioStreamInfo();
    m_position = 0;
}

Ret Mp3AudioDecoder::open(const uint8_t* data, size_t size)
{
    close();
    m_error = muse::make_ok();

    if (!data || size == 0 || size > static_cast<size_t>(INT64_MAX)) {
        return mp3Error("empty MP3 data");
    }

    initMpg123Once();

    int err = MPG123_OK;
    m_handle = mpg123_new(nullptr, &err);
    if (!m_handle || err != MPG123_OK) {
        close();
        return mp3Error("cannot create the MP3 decoder");
    }

    // Gapless trimming from the LAME/Xing tag, bounded scan for the next frame after damage
    if (mpg123_param2(m_handle, MPG123_ADD_FLAGS, MPG123_GAPLESS | MPG123_QUIET, 0) != MPG123_OK
        || mpg123_param2(m_handle, MPG123_RESYNC_LIMIT, 1024, 0) != MPG123_OK
        || mpg123_reader64(m_handle, readMemory, seekMemory, nullptr) != MPG123_OK) {
        close();
        return mp3Error("cannot set up the MP3 decoder");
    }

    // Output: exactly 32-bit float at the stream's own rate and channels (MPG123_FORCE_FLOAT
    // could give 64-bit floats in a library built with double precision)
    const long* rates = nullptr;
    size_t rateCount = 0;
    mpg123_rates(&rates, &rateCount);
    if (mpg123_format_none(m_handle) != MPG123_OK) {
        close();
        return mp3Error("cannot set up the MP3 decoder");
    }
    for (size_t i = 0; i < rateCount; ++i) {
        mpg123_format(m_handle, rates[i], MPG123_MONO | MPG123_STEREO, MPG123_ENC_FLOAT_32);
    }

    m_memory.data = data;
    m_memory.size = size;
    m_memory.position = 0;

    if (mpg123_open_handle(m_handle, &m_memory) != MPG123_OK) {
        close();
        return mp3Error("cannot read the MP3 data");
    }

    long rate = 0;
    int channels = 0;
    int encoding = 0;
    if (mpg123_getformat(m_handle, &rate, &channels, &encoding) != MPG123_OK) {
        close();
        return mp3Error("no MP3 audio found");
    }

    if ((channels != 1 && channels != 2) || rate <= 0 || encoding != MPG123_ENC_FLOAT_32) {
        close();
        return mp3Error("unsupported MP3 format");
    }

    if (mpg123_scan(m_handle) != MPG123_OK) {
        close();
        return mp3Error("cannot scan the MP3 data");
    }

    const auto length = mpg123_length(m_handle);
    if (length <= 0) {
        close();
        return mp3Error("MP3 without audio");
    }

    // The scan may have found a different first format; the stream's format is the first one
    if (mpg123_seek(m_handle, 0, SEEK_SET) < 0) {
        close();
        return mp3Error("cannot read the MP3 data");
    }

    long rate2 = 0;
    int channels2 = 0;
    int encoding2 = 0;
    if (mpg123_getformat(m_handle, &rate2, &channels2, &encoding2) != MPG123_OK
        || rate2 != rate || channels2 != channels || encoding2 != MPG123_ENC_FLOAT_32) {
        close();
        return mp3Error("unsupported MP3 format");
    }

    m_info.sampleRate = static_cast<sample_rate_t>(rate);
    m_info.channels = static_cast<audioch_t>(channels);
    m_info.frames = static_cast<samples_t>(length);
    m_position = 0;
    return muse::make_ok();
}

AudioStreamInfo Mp3AudioDecoder::info() const
{
    return m_info;
}

samples_t Mp3AudioDecoder::readFrames(float* dst, samples_t frames)
{
    if (!m_handle || !dst || frames == 0 || !m_error) {
        return 0;
    }

    const size_t frameBytes = static_cast<size_t>(m_info.channels) * sizeof(float);
    const size_t wanted = static_cast<size_t>(frames);
    size_t done = 0; // frames
    int emptyReads = 0;

    while (done < wanted) {
        unsigned char* out = reinterpret_cast<unsigned char*>(dst) + done * frameBytes;
        const size_t bytes = (wanted - done) * frameBytes;
        size_t got = 0;
        const int ret = mpg123_read(m_handle, out, bytes, &got);

        if (got > bytes || got % frameBytes != 0) {
            m_error = mp3Error("damaged MP3 data");
            break;
        }
        done += got / frameBytes;

        if (ret == MPG123_DONE) {
            break;
        }

        if (ret == MPG123_NEW_FORMAT) {
            long rate = 0;
            int channels = 0;
            int encoding = 0;
            if (mpg123_getformat(m_handle, &rate, &channels, &encoding) != MPG123_OK
                || rate != static_cast<long>(m_info.sampleRate) || channels != static_cast<int>(m_info.channels)
                || encoding != MPG123_ENC_FLOAT_32) {
                m_error = mp3Error("the MP3 changes its sample rate or channels");
                break;
            }
            continue;
        }

        if (ret != MPG123_OK) {
            m_error = mp3Error("damaged MP3 data");
            break;
        }

        if (got == 0) {
            // OK without data should not repeat; do not spin on a misbehaving stream
            if (++emptyReads > 8) {
                m_error = mp3Error("damaged MP3 data");
                break;
            }
        } else {
            emptyReads = 0;
        }
    }

    m_position += done;
    return static_cast<samples_t>(done);
}

bool Mp3AudioDecoder::seek(samples_t frame)
{
    if (!m_handle || frame > m_info.frames) {
        return false;
    }

    const auto result = mpg123_seek(m_handle, static_cast<decltype(mpg123_seek(m_handle, 0, 0))>(frame), SEEK_SET);
    if (result < 0) {
        return false;
    }

    m_position = static_cast<samples_t>(result);
    m_error = muse::make_ok();
    return true;
}

Ret Mp3AudioDecoder::error() const
{
    return m_error;
}
