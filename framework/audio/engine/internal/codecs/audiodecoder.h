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
#include <functional>
#include <memory>

#include "global/types/ret.h"
#include "global/types/retval.h"

#include "codectypes.h"

#include "wavdecoder.h"

namespace muse::audio::codec {
//! The decoder seam (FontDaw, FRAMEWORK_CHANGES F1 step 2 and F4)
//! ===========================================================
//! Everything FontDaw decodes for a clip goes through IAudioDecoder: the engine for playback, and
//! the app for its import checks and waveform peaks, so the two cannot disagree on whether a file
//! is usable or how long it is (FD-050). Formats (Batch 2 workstream 04):
//! - WAV: WavAudioDecoder, FontDaw's own parser (wavdecoder.h);
//! - FLAC, Ogg Vorbis, Ogg Opus: SndFileAudioDecoder (libsndfile);
//! - MP3: Mp3AudioDecoder (libmpg123; libsndfile's own MP3 support is built off in muse_deps).
//! 01b changes when and on which thread it is called.
//!
//! A decoder works on bytes that are already in memory and never touches the file system. The
//! bytes come from project files and from files the user picked, so they are untrusted: each
//! implementation checks its input, and makeDecoder() refuses what no decoder recognises and
//! streams whose channels or sample rate are out of range (the limits of WavDecoder).

struct AudioStreamInfo {
    sample_rate_t sampleRate = 0;
    audioch_t channels = 0;
    //! What the file says it holds, after encoder delay and padding are removed (MP3, Opus).
    //! readAll() reads to the real end, which is what counts; a decoder whose format states its
    //! length exactly (FLAC) reports a shortfall as error()
    samples_t frames = 0;
};

//! What a file is, by its content (magic numbers), never by its name
enum class AudioFileFormat {
    Unknown,
    Wav,        //! RIFF/WAVE
    Flac,       //! native FLAC ("fLaC"); FLAC inside Ogg is not supported
    Mp3,        //! MPEG-1/2 layer III, with or without an ID3v2 tag in front
    OggVorbis,  //! Ogg whose first packet is a Vorbis identification header
    OggOpus,    //! Ogg whose first packet is "OpusHead"
};

//! Looks at the first bytes only; the decoder still checks the rest
AudioFileFormat detectFormat(const uint8_t* data, size_t size);

class IAudioDecoder
{
public:
    virtual ~IAudioDecoder() = default;

    //! `data` stays owned by the caller and must outlive the decoder
    virtual Ret open(const uint8_t* data, size_t size) = 0;

    //! Valid after a successful open()
    virtual AudioStreamInfo info() const = 0;

    //! Reads up to `frames` frames (interleaved floats, info().channels per frame) from the current
    //! position into `dst` and moves the position on. Returns the frames read: fewer only at the end
    //! of the stream or when the data is damaged (then error() says so), never fewer just because a
    //! compressed frame ended.
    virtual samples_t readFrames(float* dst, samples_t frames) = 0;

    //! Moves the read position to `frame` (0 <= frame <= info().frames)
    virtual bool seek(samples_t frame) = 0;

    //! Not ok once the decoder found the data damaged (readFrames() then returns 0 from there on);
    //! cleared by seek()
    virtual Ret error() const { return muse::make_ok(); }
};

using IAudioDecoderPtr = std::unique_ptr<IAudioDecoder>;

//! A decoder that recognises `data` (by content, not by file name), already opened; or an error
RetVal<IAudioDecoderPtr> makeDecoder(const uint8_t* data, size_t size);

//! Called by readAll() with each block of interleaved frames, in order
using AudioBlockHandler = std::function<void (const float* interleaved, samples_t frames)>;

//! Reads a decoder from its start to its end in blocks of `blockFrames` frames (the last one may be
//! shorter) and hands each to `onBlock`. Refuses audio longer than maxSamples (all channels
//! together): before reading when info().frames already says so, and while reading for formats
//! whose header does not give the length. Refuses audio the decoder finds damaged and audio with
//! no frames. Returns the frames read.
//! The one loop every user of decoded audio goes through (decodeAll, the app's import and peaks).
RetVal<samples_t> readAll(IAudioDecoder& decoder, const AudioBlockHandler& onBlock,
                          size_t maxSamples = WavDecoder::MAX_SAMPLES, samples_t blockFrames = 4096);

//! Decodes everything a decoder has (readAll into one buffer)
RetVal<PcmAudio> decodeAll(IAudioDecoder& decoder, size_t maxSamples = WavDecoder::MAX_SAMPLES);
}
