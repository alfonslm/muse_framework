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

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace muse::audio::tests {
//! Builds a RIFF/WAVE file. `samples` are interleaved, in [-1, 1].
inline std::vector<uint8_t> makeWav(uint32_t sampleRate, uint16_t channels, uint16_t bits, bool isFloat,
                                    const std::vector<double>& samples, bool extensible = false)
{
    std::vector<uint8_t> out;
    auto u16 = [&out](uint16_t v) { out.push_back(v & 0xff); out.push_back(v >> 8); };
    auto u32 = [&out](uint32_t v) {
        for (int i = 0; i < 4; ++i) {
            out.push_back((v >> (8 * i)) & 0xff);
        }
    };
    auto tag = [&out](const char* t) { out.insert(out.end(), t, t + 4); };

    const uint16_t bytesPerSample = bits / 8;
    const uint16_t blockAlign = channels * bytesPerSample;
    const uint32_t dataSize = static_cast<uint32_t>(samples.size() / channels) * blockAlign;
    const uint16_t formatTag = isFloat ? 3 : 1;

    tag("RIFF");
    u32(0); // patched below
    tag("WAVE");

    // An unrelated chunk with an odd size first: readers must skip what they do not know
    tag("LIST");
    u32(3);
    out.push_back('a');
    out.push_back('b');
    out.push_back('c');
    out.push_back(0); // pad byte

    tag("fmt ");
    u32(extensible ? 40 : 16);
    u16(extensible ? 0xFFFE : formatTag);
    u16(channels);
    u32(sampleRate);
    u32(sampleRate * blockAlign);
    u16(blockAlign);
    u16(bits);
    if (extensible) {
        u16(22);   // cbSize
        u16(bits); // valid bits
        u32(0);    // channel mask
        u16(formatTag); // SubFormat GUID: the format tag, then a fixed tail
        static const uint8_t guidTail[14] = { 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71 };
        out.insert(out.end(), guidTail, guidTail + 14);
    }

    tag("data");
    u32(dataSize);
    for (double s : samples) {
        if (isFloat && bits == 32) {
            const float f = static_cast<float>(s);
            uint8_t b[4];
            std::memcpy(b, &f, 4);
            out.insert(out.end(), b, b + 4);
        } else if (isFloat) {
            uint8_t b[8];
            std::memcpy(b, &s, 8);
            out.insert(out.end(), b, b + 8);
        } else if (bits == 8) {
            out.push_back(static_cast<uint8_t>(std::lround(s * 127.0) + 128));
        } else {
            const double scale = bits == 16 ? 32767.0 : bits == 24 ? 8388607.0 : 2147483647.0;
            const int64_t v = std::llround(s * scale);
            for (int i = 0; i < bytesPerSample; ++i) {
                out.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xff));
            }
        }
    }

    const uint32_t riffSize = static_cast<uint32_t>(out.size() - 8);
    for (int i = 0; i < 4; ++i) {
        out[4 + i] = (riffSize >> (8 * i)) & 0xff;
    }
    return out;
}

//! Position of the chunk header with this tag, or npos
inline size_t findChunk(const std::vector<uint8_t>& wav, const char* tag)
{
    for (size_t i = 12; i + 4 <= wav.size(); ++i) {
        if (std::memcmp(wav.data() + i, tag, 4) == 0) {
            return i;
        }
    }
    return std::string::npos;
}
}
