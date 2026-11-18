# SPDX-License-Identifier: GPL-3.0-only
#
# FontDaw
# The notation-first DAW
#
# Copyright (C) 2026 FontDaw contributors
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License version 3 as
# published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.

# The decoders for audio clips, in a library of their own so that the engine (playback) and an
# application that shows the audio (import checks, waveform overviews) decode with the same code.
# vorbisdecoder.* (SoundFonts) stays in the engine.

muse_create_module(muse_audio_codecs NO_QT NO_PCH NO_UNITY) # each decoder has its own file-local helpers

target_sources(muse_audio_codecs PRIVATE
    audiodecoder.cpp
    audiodecoder.h
    codectypes.h
    wavdecoder.cpp
    wavdecoder.h
    wavaudiodecoder.cpp
    wavaudiodecoder.h
    sndfileaudiodecoder.cpp
    sndfileaudiodecoder.h
    mp3audiodecoder.cpp
    mp3audiodecoder.h
)

# Both come from muse_deps (ExtDepsManifest.cmake): prebuilt per platform, installed with the app
target_link_libraries(muse_audio_codecs PRIVATE SndFile::sndfile mpg123::libmpg123)
