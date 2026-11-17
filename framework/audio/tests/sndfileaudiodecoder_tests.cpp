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

#include <gtest/gtest.h>

// pragma-pack matches rpcpacker_tests.cpp to avoid an ODR segfault at static init (FD-040).
#pragma pack(push, 1)
#include "audio/common/audiotypes.h"
#pragma pack(pop)

#include "audio/engine/internal/codecs/audiodecoder.h"
#include "audio/engine/internal/codecs/sndfileaudiodecoder.h"

#include "codectestutils.h"

using namespace muse;
using namespace muse::audio;
using namespace muse::audio::codec;
using namespace muse::audio::tests;

// TODO(FontDaw 04): tests, see the PR description (Batch 2 workstream 04)
