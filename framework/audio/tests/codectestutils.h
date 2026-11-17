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

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

// Helpers for the clip decoder tests (sndfileaudiodecoder_tests.cpp, mp3audiodecoder_tests.cpp).
// The test files are in data/codecs/, made by data/codecs/make_testdata.sh from one source signal:
// 0.5 s, 48 kHz, stereo, left a 1 kHz click every 125 ms, right a quiet 330 Hz sine
// (reference.wav, and the same encoded as FLAC, MP3, Ogg Vorbis and Ogg Opus), plus mono24.wav /
// mono24.flac (24-bit mono, 44.1 kHz).
namespace muse::audio::tests {
//! The bytes of a file under data/codecs/ (empty if it cannot be read)
inline std::vector<uint8_t> readCodecTestFile(const std::string& name)
{
    std::ifstream in(std::string(muse_audio_tests_DATA_ROOT) + "/data/codecs/" + name, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

//! The lag (in frames, within ±maxLag) at which `decoded` best matches `reference` on `channel`:
//! decoded[f + lag] ~ reference[f]. 0 means no offset.
inline long bestLag(const std::vector<float>& reference, const std::vector<float>& decoded, size_t channels,
                    size_t channel, long maxLag)
{
    const long refFrames = static_cast<long>(reference.size() / channels);
    const long decFrames = static_cast<long>(decoded.size() / channels);
    long best = 0;
    double bestScore = -1e300;
    for (long lag = -maxLag; lag <= maxLag; ++lag) {
        double score = 0.0;
        for (long f = 0; f < refFrames; ++f) {
            const long g = f + lag;
            if (g >= 0 && g < decFrames) {
                score += double(reference[f * channels + channel]) * decoded[g * channels + channel];
            }
        }
        if (score > bestScore) {
            bestScore = score;
            best = lag;
        }
    }
    return best;
}

//! The largest absolute difference between `reference` and `decoded` over the frames both have
inline float maxDifference(const std::vector<float>& reference, const std::vector<float>& decoded)
{
    float worst = 0.f;
    for (size_t i = 0, n = std::min(reference.size(), decoded.size()); i < n; ++i) {
        worst = std::max(worst, std::abs(reference[i] - decoded[i]));
    }
    return worst;
}

//! The first `size` bytes of `bytes`
inline std::vector<uint8_t> truncated(const std::vector<uint8_t>& bytes, size_t size)
{
    return std::vector<uint8_t>(bytes.begin(), bytes.begin() + std::min(size, bytes.size()));
}
}
