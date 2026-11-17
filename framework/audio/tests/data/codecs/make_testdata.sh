#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# FontDaw – test files for the clip decoder tests (sndfileaudiodecoder_tests.cpp,
# mp3audiodecoder_tests.cpp). Every file is encoded from the same source signal, so each decoder's
# output can be compared with reference.wav. Needs ffmpeg with libmp3lame, libvorbis and libopus.
# Run from this folder; the files are committed, so this is only needed to change them.
set -euo pipefail

# 0.5 s, 48 kHz, stereo. Left: a 1 kHz click every 125 ms (10 ms, decaying), so a delay shows as
# a moved onset. Right: a quiet 330 Hz sine, so the channels differ.
SRC="aevalsrc=exprs='0.8*sin(2*PI*1000*t)*lt(mod(t\,0.125)\,0.01)*exp(-300*mod(t\,0.125))|0.3*sin(2*PI*330*t)':s=48000:d=0.5"
ff() { ffmpeg -hide_banner -loglevel error -y "$@"; }

ff -f lavfi -i "$SRC" -c:a pcm_s16le reference.wav
ff -i reference.wav -c:a flac reference.flac
ff -i reference.wav -c:a libmp3lame -b:a 128k reference.mp3              # with a LAME/Xing tag
ff -i reference.wav -c:a libmp3lame -b:a 128k -write_xing 0 notag.mp3     # without: keeps the delay
ff -i reference.wav -c:a libvorbis -q:a 4 reference.ogg
ff -i reference.wav -c:a libopus -b:a 96k reference.opus

# 24-bit mono at 44.1 kHz: FLAC must give exactly what the WAV gives
ff -f lavfi -i "aevalsrc=exprs='0.5*sin(2*PI*440*t)':s=44100:d=0.25" -c:a pcm_s24le mono24.wav
ff -i mono24.wav -c:a flac mono24.flac
