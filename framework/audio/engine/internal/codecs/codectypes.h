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

#include <cstdint>

//! The three audio integer types the clip decoders use, declared exactly as in
//! audio/common/audiotypes.h (repeating an alias of the same type is allowed). The decoders need
//! nothing else from that header, which pulls in much of the framework; an application that links
//! only this library gets these three. Keep them identical to audiotypes.h.
namespace muse::audio {
using samples_t = uint64_t;
using sample_rate_t = uint64_t;
using audioch_t = uint8_t;
}
