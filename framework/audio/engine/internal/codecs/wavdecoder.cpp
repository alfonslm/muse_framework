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

#include "wavdecoder.h"

#include <cstring>

#include "audio/common/audioerrors.h"

using namespace muse;
using namespace muse::audio;
using namespace muse::audio::codec;

namespace {
constexpr uint16_t WAV_FORMAT_PCM = 0x0001;
constexpr uint16_t WAV_FORMAT_IEEE_FLOAT = 0x0003;
constexpr uint16_t WAV_FORMAT_EXTENSIBLE = 0xFFFE;

uint16_t wavU16(const uint8_t* p)
{
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

uint32_t wavU32(const uint8_t* p)
{
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8)
           | (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

bool isWavTag(const uint8_t* p, const char* tag)
{
    return std::memcmp(p, tag, 4) == 0;
}

RetVal<PcmAudio> wavError(const std::string& text)
{
    return RetVal<PcmAudio>::make_ret(make_ret(Err::InvalidAudioSource, text));
}

RetVal<WavLayout> wavLayoutError(const std::string& text)
{
    return RetVal<WavLayout>::make_ret(make_ret(Err::InvalidAudioSource, text));
}
}

float codec::decodeWavSample(const uint8_t* p, const WavLayout& f)
{
    if (f.tag == WAV_FORMAT_IEEE_FLOAT) {
        if (f.bitsPerSample == 32) {
            float v = 0.f;
            std::memcpy(&v, p, sizeof(v)); // WAV is little-endian, like every platform we build for
            return v;
        }
        double v = 0.0;
        std::memcpy(&v, p, sizeof(v));
        return static_cast<float>(v);
    }

    switch (f.bitsPerSample) {
    case 8:
        return (static_cast<int>(p[0]) - 128) / 128.f; // 8 bit is unsigned
    case 16:
        return static_cast<int16_t>(wavU16(p)) / 32768.f;
    case 24: {
        int32_t v = p[0] | (p[1] << 8) | (p[2] << 16);
        if (v & 0x800000) {
            v -= 0x1000000;
        }
        return v / 8388608.f;
    }
    case 32:
        return static_cast<float>(static_cast<int32_t>(wavU32(p)) / 2147483648.0);
    }
    return 0.f;
}

RetVal<WavLayout> codec::parseWav(const uint8_t* data, size_t size)
{
    if (!data || size < 12 || !isWavTag(data, "RIFF") || !isWavTag(data + 8, "WAVE")) {
        return wavLayoutError("not a RIFF/WAVE file");
    }

    WavLayout format;
    bool hasFormat = false;
    const uint8_t* samples = nullptr;
    size_t samplesSize = 0;

    size_t pos = 12;
    while (pos + 8 <= size) {
        const uint8_t* chunk = data + pos;
        const size_t chunkSize = wavU32(chunk + 4);
        const size_t bodyPos = pos + 8;
        const size_t available = size - bodyPos;

        if (isWavTag(chunk, "fmt ")) {
            if (chunkSize < 16 || available < 16) {
                return wavLayoutError("damaged WAV format chunk");
            }
            const uint8_t* body = data + bodyPos;
            format.tag = wavU16(body);
            format.channels = wavU16(body + 2);
            format.sampleRate = wavU32(body + 4);
            format.blockAlign = wavU16(body + 12);
            format.bitsPerSample = wavU16(body + 14);
            if (format.tag == WAV_FORMAT_EXTENSIBLE) {
                // The real format is the first two bytes of the SubFormat GUID
                if (chunkSize < 40 || available < 40) {
                    return wavLayoutError("damaged WAV format chunk");
                }
                format.tag = wavU16(body + 24);
            }
            hasFormat = true;
        } else if (isWavTag(chunk, "data")) {
            samples = data + bodyPos;
            // Some writers leave the size 0 or too large when recording was cut off:
            // use what is there
            samplesSize = (chunkSize == 0 || chunkSize > available) ? available : chunkSize;
            if (hasFormat) {
                break;
            }
        }

        if (chunkSize > available) {
            break;
        }
        pos = bodyPos + chunkSize + (chunkSize & 1); // chunks are padded to even sizes
    }

    if (!hasFormat || !samples) {
        return wavLayoutError("WAV file without format or data chunk");
    }

    const bool isInt = format.tag == WAV_FORMAT_PCM
                       && (format.bitsPerSample == 8 || format.bitsPerSample == 16
                           || format.bitsPerSample == 24 || format.bitsPerSample == 32);
    const bool isFloat = format.tag == WAV_FORMAT_IEEE_FLOAT && (format.bitsPerSample == 32 || format.bitsPerSample == 64);
    if (!isInt && !isFloat) {
        return wavLayoutError("unsupported WAV sample format");
    }

    const size_t bytesPerSample = format.bitsPerSample / 8;
    if (format.channels == 0 || format.channels > WavDecoder::MAX_CHANNELS
        || format.sampleRate == 0 || format.sampleRate > WavDecoder::MAX_SAMPLE_RATE
        || format.blockAlign != bytesPerSample * format.channels) {
        return wavLayoutError("unsupported WAV channel count, sample rate or block size");
    }

    format.samples = samples;
    format.frames = samplesSize / format.blockAlign;
    return RetVal<WavLayout>::make_ok(format);
}

RetVal<PcmAudio> WavDecoder::decode(const uint8_t* data, size_t size, size_t maxSamples)
{
    RetVal<WavLayout> parsed = parseWav(data, size);
    if (!parsed.ret) {
        return RetVal<PcmAudio>::make_ret(parsed.ret);
    }

    const WavLayout& format = parsed.val;
    const size_t bytesPerSample = format.bitsPerSample / 8;
    if (format.frames > maxSamples / format.channels) {
        return wavError("audio file is too long to load");
    }

    PcmAudio audio;
    audio.sampleRate = format.sampleRate;
    audio.channels = static_cast<audioch_t>(format.channels);
    audio.samples.resize(format.frames * format.channels);

    const uint8_t* p = format.samples;
    float* out = audio.samples.data();
    for (size_t i = 0, n = audio.samples.size(); i < n; ++i, p += bytesPerSample) {
        out[i] = decodeWavSample(p, format);
    }

    return RetVal<PcmAudio>::make_ok(std::move(audio));
}
