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

#include <algorithm>
#include <cstring>
#include <limits>
#include <string>

#include <sndfile.h>

#include "audio/common/audioerrors.h"

using namespace muse;
using namespace muse::audio;
using namespace muse::audio::codec;

namespace {
using SfMemory = SndFileAudioDecoder::Memory;

sf_count_t sfMemLength(void* user)
{
    return static_cast<sf_count_t>(static_cast<SfMemory*>(user)->size);
}

sf_count_t sfMemSeek(sf_count_t offset, int whence, void* user)
{
    SfMemory* mem = static_cast<SfMemory*>(user);
    const sf_count_t size = static_cast<sf_count_t>(mem->size);
    sf_count_t base = 0;
    switch (whence) {
    case SEEK_SET: base = 0;
        break;
    case SEEK_CUR: base = static_cast<sf_count_t>(mem->position);
        break;
    case SEEK_END: base = size;
        break;
    default:
        return -1;
    }

    // Guard the addition against overflow, then refuse positions outside the data
    if ((offset > 0 && base > std::numeric_limits<sf_count_t>::max() - offset)
        || (offset < 0 && base < std::numeric_limits<sf_count_t>::min() - offset)) {
        return -1;
    }
    const sf_count_t target = base + offset;
    if (target < 0 || target > size) {
        return -1;
    }

    mem->position = static_cast<size_t>(target);
    return target;
}

sf_count_t sfMemRead(void* ptr, sf_count_t count, void* user)
{
    SfMemory* mem = static_cast<SfMemory*>(user);
    if (count <= 0 || mem->position >= mem->size) {
        return 0;
    }

    const size_t n = std::min(static_cast<size_t>(count), mem->size - mem->position);
    std::memcpy(ptr, mem->data + mem->position, n);
    mem->position += n;
    return static_cast<sf_count_t>(n);
}

sf_count_t sfMemWrite(const void*, sf_count_t, void*)
{
    return 0;
}

sf_count_t sfMemTell(void* user)
{
    return static_cast<sf_count_t>(static_cast<SfMemory*>(user)->position);
}

SF_VIRTUAL_IO sfVirtualIo()
{
    SF_VIRTUAL_IO io;
    io.get_filelen = sfMemLength;
    io.seek = sfMemSeek;
    io.read = sfMemRead;
    io.write = sfMemWrite;
    io.tell = sfMemTell;
    return io;
}

//! libsndfile 1.2.2 leaks the Vorbis decoder state when the stream ends inside the three Vorbis
//! header packets (identification, comment, setup). So an Ogg Vorbis stream is only handed over once
//! those three packets are complete in `data`; shorter input is refused here. A walk over the page
//! headers, which makes no allocation and reads inside [data, data + size) only.
bool oggVorbisHeadersComplete(const uint8_t* data, size_t size)
{
    constexpr size_t PAGE_HEADER = 27;
    size_t pos = 0;
    int packets = 0;
    while (packets < 3) {
        if (size - pos < PAGE_HEADER || std::memcmp(data + pos, "OggS", 4) != 0) {
            return false;
        }
        const size_t segments = data[pos + 26];
        if (size - pos < PAGE_HEADER + segments) {
            return false;
        }
        size_t body = 0;
        for (size_t i = 0; i < segments; ++i) {
            const uint8_t lace = data[pos + PAGE_HEADER + i];
            body += lace;
            if (lace < 255) {
                ++packets;
            }
        }
        const size_t pageSize = PAGE_HEADER + segments + body;
        if (size - pos < pageSize) {
            return false;
        }
        pos += pageSize;
    }
    return true;
}

Ret sndFileError(const std::string& text)
{
    return make_ret(Err::InvalidAudioSource, text);
}
}

SndFileAudioDecoder::~SndFileAudioDecoder()
{
    close();
}

void SndFileAudioDecoder::close()
{
    if (m_file) {
        sf_close(reinterpret_cast<SNDFILE*>(m_file));
        m_file = nullptr;
    }
    m_info = AudioStreamInfo();
    m_lengthIsExact = false;
    m_position = 0;
    m_error = muse::make_ok();
}

Ret SndFileAudioDecoder::open(const uint8_t* data, size_t size)
{
    close();

    if (!data || size == 0) {
        return sndFileError("empty audio file");
    }

    if (detectFormat(data, size) == AudioFileFormat::OggVorbis
        && !oggVorbisHeadersComplete(data, size)) {
        return sndFileError("the Ogg stream ends inside its headers");
    }

    m_memory.data = data;
    m_memory.size = size;
    m_memory.position = 0;

    SF_VIRTUAL_IO io = sfVirtualIo();
    SF_INFO sfinfo = {};
    SNDFILE* file = sf_open_virtual(&io, SFM_READ, &sfinfo, &m_memory);
    if (!file) {
        return sndFileError("not a readable FLAC or Ogg file");
    }

    const int major = sfinfo.format & SF_FORMAT_TYPEMASK;
    const int subtype = sfinfo.format & SF_FORMAT_SUBMASK;
    const bool isFlac = major == SF_FORMAT_FLAC;
    const bool isOgg = major == SF_FORMAT_OGG && (subtype == SF_FORMAT_VORBIS || subtype == SF_FORMAT_OPUS);
    if (!isFlac && !isOgg) {
        sf_close(file);
        return sndFileError("unsupported audio format");
    }

    if (sfinfo.channels <= 0 || sfinfo.channels > 255 || sfinfo.samplerate <= 0 || sfinfo.frames < 0) {
        sf_close(file);
        return sndFileError("invalid audio stream parameters");
    }

    sf_command(file, SFC_SET_NORM_FLOAT, nullptr, SF_TRUE);

    m_file = reinterpret_cast<sf_private_tag*>(file);
    m_info.sampleRate = static_cast<sample_rate_t>(sfinfo.samplerate);
    m_info.channels = static_cast<audioch_t>(sfinfo.channels);

    // A FLAC stream may leave the total unknown (0); libsndfile may report that as 0 or as the maximum
    const bool unknownLength = sfinfo.frames == 0 || sfinfo.frames == SF_COUNT_MAX;
    m_info.frames = unknownLength ? 0 : static_cast<samples_t>(sfinfo.frames);
    m_lengthIsExact = isFlac && !unknownLength;
    m_position = 0;
    m_error = muse::make_ok();
    return muse::make_ok();
}

AudioStreamInfo SndFileAudioDecoder::info() const
{
    return m_info;
}

samples_t SndFileAudioDecoder::readFrames(float* dst, samples_t frames)
{
    if (!m_file || !dst || frames == 0 || !m_error) {
        return 0;
    }

    SNDFILE* file = reinterpret_cast<SNDFILE*>(m_file);

    samples_t want = frames;
    if (m_lengthIsExact) {
        want = m_position >= m_info.frames ? 0 : std::min(frames, m_info.frames - m_position);
    }

    samples_t done = 0;
    while (done < want) {
        const sf_count_t got = sf_readf_float(file, dst + static_cast<size_t>(done) * m_info.channels,
                                              static_cast<sf_count_t>(want - done));
        if (got <= 0) {
            break;
        }
        done += static_cast<samples_t>(got);
    }
    m_position += done;

    if (sf_error(file) != SF_ERR_NO_ERROR) {
        m_error = sndFileError(std::string("the audio file is damaged: ") + sf_strerror(file));
        return done;
    }

    if (m_lengthIsExact && done < want) {
        m_error = sndFileError("the audio file ends early");
    }

    return done;
}

bool SndFileAudioDecoder::seek(samples_t frame)
{
    if (!m_file || frame > m_info.frames) {
        return false;
    }

    // An unknown length (0) allows only the start
    const sf_count_t result = sf_seek(reinterpret_cast<SNDFILE*>(m_file), static_cast<sf_count_t>(frame), SEEK_SET);
    if (result < 0) {
        return false;
    }

    m_position = static_cast<samples_t>(result);
    m_error = muse::make_ok();
    return true;
}

Ret SndFileAudioDecoder::error() const
{
    return m_error;
}
