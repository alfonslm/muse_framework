# SPDX-License-Identifier: GPL-3.0-only
# MuseScore-CLA-applies
#
# MuseScore Studio
# Music Composition & Notation
#
# Copyright (C) 2025 MuseScore Limited and others
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

set(MODULE_TEST muse_audio_tests)

set(MODULE_TEST_SRC
    ${CMAKE_CURRENT_LIST_DIR}/mocks/audioconfigurationmock.h
    ${CMAKE_CURRENT_LIST_DIR}/mocks/audiocontextmock.h
    ${CMAKE_CURRENT_LIST_DIR}/mocks/audiodriverfactorymock.h
    ${CMAKE_CURRENT_LIST_DIR}/mocks/audiodrivermock.h
    ${CMAKE_CURRENT_LIST_DIR}/mocks/audioenginemock.h
    ${CMAKE_CURRENT_LIST_DIR}/mocks/rpcchannelmock.h
    ${CMAKE_CURRENT_LIST_DIR}/mocks/startaudiocontrollermock.h

    ${CMAKE_CURRENT_LIST_DIR}/alignbuffer_tests.cpp
    ${CMAKE_CURRENT_LIST_DIR}/playbackpositionestimator_tests.cpp
    ${CMAKE_CURRENT_LIST_DIR}/audioresourcetypes_tests.cpp
    ${CMAKE_CURRENT_LIST_DIR}/wavtestutils.h
    ${CMAKE_CURRENT_LIST_DIR}/wavdecoder_tests.cpp
    ${CMAKE_CURRENT_LIST_DIR}/audiodecoder_tests.cpp
    ${CMAKE_CURRENT_LIST_DIR}/clipaudionode_tests.cpp
    ${CMAKE_CURRENT_LIST_DIR}/musicaltimemap_tests.cpp
    ${CMAKE_CURRENT_LIST_DIR}/codectestutils.h
    ${CMAKE_CURRENT_LIST_DIR}/sndfileaudiodecoder_tests.cpp
    ${CMAKE_CURRENT_LIST_DIR}/mp3audiodecoder_tests.cpp
    ${CMAKE_CURRENT_LIST_DIR}/audiodrivercontroller_tests.cpp
    ${CMAKE_CURRENT_LIST_DIR}/enginerpccontroller_tests.cpp
    ${CMAKE_CURRENT_LIST_DIR}/playback_tests.cpp
)

set(MODULE_TEST_LINK
    muse_audio
    muse_audio_codecs # the clip decoders, a library of their own (engine/internal/codecs)
)

set(MODULE_TEST_DATA_ROOT ${CMAKE_CURRENT_LIST_DIR})

include(SetupGTest)

add_subdirectory(rpcpacker)
