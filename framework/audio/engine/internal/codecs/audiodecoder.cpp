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

#include "audiodecoder.h"

#include <algorithm>
#include <cstring>
#include <vector>

#include "audio/common/audioerrors.h"

#include "mp3audiodecoder.h"
#include "sndfileaudiodecoder.h"
#include "wavaudiodecoder.h"

using namespace muse;
using namespace muse::audio;
using namespace muse::audio::codec;

namespace {
Ret decodeError(const std::string& text)
{
    return make_ret(Err::InvalidAudioSource, text);
}

//! The first packet of an Ogg stream, if the first page holds it whole
bool oggFirstPacketStartsWith(const uint8_t* data, size_t size, const char* magic, size_t magicSize)
{
    // Page header: "OggS", version, flags, granule (8), serial (4), sequence (4), CRC (4),
    // segment count (1), then the segment table
    constexpr size_t SEGMENT_COUNT_POS = 26;
    if (size <= SEGMENT_COUNT_POS) {
        return false;
    }
    const size_t segments = data[SEGMENT_COUNT_POS];
    const size_t packetPos = SEGMENT_COUNT_POS + 1 + segments;
    return segments > 0 && packetPos + magicSize <= size && std::memcmp(data + packetPos, magic, magicSize) == 0;
}

//! An MPEG audio layer III frame header at `p` (4 bytes)
bool isMp3FrameHeader(const uint8_t* p)
{
    const bool sync = p[0] == 0xFF && (p[1] & 0xE0) == 0xE0;
    const unsigned version = (p[1] >> 3) & 0x03;   // 1 is reserved
    const unsigned layer = (p[1] >> 1) & 0x03;     // 1 is layer III
    const unsigned bitrate = (p[2] >> 4) & 0x0F;   // 15 is invalid (0, free format, is refused too)
    const unsigned rate = (p[2] >> 2) & 0x03;      // 3 is reserved
    return sync && version != 1 && layer == 1 && bitrate != 0 && bitrate != 15 && rate != 3;
}
}

AudioFileFormat codec::detectFormat(const uint8_t* data, size_t size)
{
    if (!data || size < 4) {
        return AudioFileFormat::Unknown;
    }

    if (size >= 12 && std::memcmp(data, "RIFF", 4) == 0 && std::memcmp(data + 8, "WAVE", 4) == 0) {
        return AudioFileFormat::Wav;
    }

    if (std::memcmp(data, "fLaC", 4) == 0) {
        return AudioFileFormat::Flac;
    }

    if (std::memcmp(data, "OggS", 4) == 0) {
        if (oggFirstPacketStartsWith(data, size, "\x01vorbis", 7)) {
            return AudioFileFormat::OggVorbis;
        }
        if (oggFirstPacketStartsWith(data, size, "OpusHead", 8)) {
            return AudioFileFormat::OggOpus;
        }
        return AudioFileFormat::Unknown;
    }

    // ID3v2 tag ("ID3", version, flags, 4 syncsafe size bytes): an MP3 file in practice; the
    // decoder checks for audio frames behind it
    if (size >= 10 && std::memcmp(data, "ID3", 3) == 0) {
        return AudioFileFormat::Mp3;
    }

    if (isMp3FrameHeader(data)) {
        return AudioFileFormat::Mp3;
    }

    return AudioFileFormat::Unknown;
}

RetVal<IAudioDecoderPtr> codec::makeDecoder(const uint8_t* data, size_t size)
{
    IAudioDecoderPtr decoder;
    switch (detectFormat(data, size)) {
    case AudioFileFormat::Wav:
        decoder = std::make_unique<WavAudioDecoder>();
        break;
    case AudioFileFormat::Flac:
    case AudioFileFormat::OggVorbis:
    case AudioFileFormat::OggOpus:
        decoder = std::make_unique<SndFileAudioDecoder>();
        break;
    case AudioFileFormat::Mp3:
        decoder = std::make_unique<Mp3AudioDecoder>();
        break;
    case AudioFileFormat::Unknown:
        break;
    }

    if (!decoder) {
        return RetVal<IAudioDecoderPtr>::make_ret(decodeError("unrecognised audio format"));
    }

    Ret ret = decoder->open(data, size);
    if (!ret) {
        return RetVal<IAudioDecoderPtr>::make_ret(ret);
    }

    // The same limits for every format
    const AudioStreamInfo info = decoder->info();
    if (info.channels == 0 || info.channels > WavDecoder::MAX_CHANNELS
        || info.sampleRate == 0 || info.sampleRate > WavDecoder::MAX_SAMPLE_RATE) {
        return RetVal<IAudioDecoderPtr>::make_ret(decodeError("unsupported channel count or sample rate"));
    }

    RetVal<IAudioDecoderPtr> result; // make_ok copies, and the decoder cannot be copied
    result.ret = muse::make_ok();
    result.val = std::move(decoder);
    return result;
}

RetVal<samples_t> codec::readAll(IAudioDecoder& decoder, const AudioBlockHandler& onBlock, size_t maxSamples,
                                 samples_t blockFrames)
{
    const AudioStreamInfo info = decoder.info();
    if (info.channels == 0 || info.sampleRate == 0 || blockFrames == 0) {
        return RetVal<samples_t>::make_ret(decodeError("audio without channels or sample rate"));
    }

    const size_t maxFrames = maxSamples / info.channels;
    if (static_cast<size_t>(info.frames) > maxFrames) {
        return RetVal<samples_t>::make_ret(decodeError("audio file is too long to load"));
    }

    if (!decoder.seek(0)) {
        return RetVal<samples_t>::make_ret(decodeError("audio cannot be read"));
    }

    std::vector<float> block(static_cast<size_t>(blockFrames) * info.channels);
    size_t done = 0;
    for (;;) {
        const samples_t got = decoder.readFrames(block.data(), blockFrames);
        if (got == 0) {
            break;
        }
        done += got;
        if (done > maxFrames) {
            // A format whose header does not give the length (or gives a wrong one)
            return RetVal<samples_t>::make_ret(decodeError("audio file is too long to load"));
        }
        if (onBlock) {
            onBlock(block.data(), got);
        }
    }

    Ret error = decoder.error();
    if (!error) {
        return RetVal<samples_t>::make_ret(error);
    }
    if (done == 0) {
        return RetVal<samples_t>::make_ret(decodeError("audio file without audio"));
    }

    return RetVal<samples_t>::make_ok(static_cast<samples_t>(done));
}

RetVal<PcmAudio> codec::decodeAll(IAudioDecoder& decoder, size_t maxSamples)
{
    const AudioStreamInfo info = decoder.info();

    PcmAudio audio;
    audio.sampleRate = info.sampleRate;
    audio.channels = info.channels;
    if (info.channels > 0) {
        // Only a hint, and a bounded one: a compressed format's header states its length, and a few
        // crafted bytes must not make us reserve gigabytes. Longer audio grows the buffer as it is read.
        constexpr size_t MAX_RESERVED_SAMPLES = size_t(1) << 24;
        const size_t hint = std::min(static_cast<size_t>(info.frames), maxSamples / info.channels) * info.channels;
        audio.samples.reserve(std::min(hint, MAX_RESERVED_SAMPLES));
    }

    RetVal<samples_t> read = readAll(decoder, [&audio](const float* interleaved, samples_t frames) {
        audio.samples.insert(audio.samples.end(), interleaved, interleaved + static_cast<size_t>(frames) * audio.channels);
    }, maxSamples);

    if (!read.ret) {
        return RetVal<PcmAudio>::make_ret(read.ret);
    }

    return RetVal<PcmAudio>::make_ok(std::move(audio));
}
