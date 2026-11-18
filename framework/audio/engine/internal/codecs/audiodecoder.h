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

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>

#include "global/types/ret.h"
#include "global/types/retval.h"

#include "codectypes.h"

#include "wavdecoder.h"

namespace muse::audio::codec {
//! The decoder seam for audio clips
//! =================================
//! Everything decoded for an audio clip goes through IAudioDecoder: the engine for playback, and an
//! application that shows the audio (import checks, waveform overviews) can link this library and
//! use the same code, so the two cannot disagree on whether a file is usable or how long it is.
//! Formats:
//! - WAV: WavAudioDecoder (wavdecoder.h);
//! - FLAC, Ogg Vorbis, Ogg Opus: SndFileAudioDecoder (libsndfile);
//! - MP3: Mp3AudioDecoder (libmpg123; libsndfile's own MP3 support is not built in the libsndfile used).
//!
//! A decoder works on bytes that are already in memory and never touches the file system; the
//! bytes passed to open() stay the caller's and must outlive the decoder, which reads them on
//! every readFrames() and seek(). The bytes come from project files and from files the user
//! picked, so they are untrusted: each implementation checks its input, makeDecoder() refuses what
//! no decoder recognises and streams whose channels or sample rate are out of range (the limits of
//! WavDecoder), and decodeAll() limits how much decoded audio is held (DecodedAudioBudget).

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

//! The longest source readAll() and decodeAll() take, in seconds at the source's own rate (and
//! never more than their maxSamples)
inline constexpr size_t MAX_DECODED_SECONDS = 2 * 60 * 60;

//! Called by readAll() with each block of interleaved frames, in order. Returning false stops the
//! read: readAll() then returns an error.
using AudioBlockHandler = std::function<bool (const float* interleaved, samples_t frames)>;

//! Reads a decoder from its start to its end in blocks of `blockFrames` frames (the last one may be
//! shorter) and hands each to `onBlock`. Refuses audio longer than maxSamples (all channels
//! together) or MAX_DECODED_SECONDS: before reading when info().frames already says so, and while
//! reading for formats whose header does not give the length. Refuses audio the decoder finds damaged and audio with
//! no frames. Returns the frames read.
//! The one loop every user of decoded audio goes through (decodeAll, and callers that only look at the
//! audio, such as an import check or a waveform overview).
RetVal<samples_t> readAll(IAudioDecoder& decoder, const AudioBlockHandler& onBlock,
                          size_t maxSamples = WavDecoder::MAX_SAMPLES, samples_t blockFrames = 4096);

//! How much decoded clip audio may be held in memory at once, across every source decodeAll() has
//! decoded and that is still alive (PcmAudio::budgetLease gives its share back). A compressed file
//! can expand by a factor of thousands (a FLAC of silence: 151 KB for 528 million frames), so the
//! per-source length cap alone does not bound memory, and a project can hold many sources.
//! Thread-safe. Holding whole sources in memory is a stopgap until clips stream from their bytes.
class DecodedAudioBudget
{
public:
    //! 2^30 floats (4 GiB) in all
    static constexpr size_t DEFAULT_MAX_SAMPLES = size_t(1) << 30;

    explicit DecodedAudioBudget(size_t maxSamples = DEFAULT_MAX_SAMPLES);

    //! The budget decodeAll() uses unless it is given another
    static DecodedAudioBudget& global();

    //! Takes `samples` from the budget if they fit
    bool tryAcquire(size_t samples);
    void release(size_t samples);

    size_t used() const;
    size_t maxSamples() const;

private:
    const size_t m_maxSamples;
    std::atomic<size_t> m_used { 0 };
};

//! Decodes everything a decoder has (readAll into one buffer). Refuses, without a crash: what
//! readAll() refuses, audio that does not fit in what is left of
//! `budget`, and an allocation that fails. The result holds its share of `budget` until the last
//! copy of it is destroyed.
RetVal<PcmAudio> decodeAll(IAudioDecoder& decoder, size_t maxSamples = WavDecoder::MAX_SAMPLES,
                           DecodedAudioBudget& budget = DecodedAudioBudget::global());
}
