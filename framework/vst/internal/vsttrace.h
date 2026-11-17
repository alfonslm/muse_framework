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

#include <cstdint>
#include <string>

#include "pluginterfaces/base/funknown.h"

//! VST host trace: what a plugin asks the host for, what it reports and what it is told.
//!
//! Off unless the environment variable MUSE_VST_TRACE is set to 1 when the program starts.
//! Lines go to the normal log with the prefix "[vst-trace]".
//!
//! log() may be called from any thread, including audio threads: it formats into a fixed buffer,
//! never allocates or blocks, and drops the line (counting it) when the queue is full. A
//! background thread, running only while tracing is on, writes the queued lines to the log.
namespace muse::vst::trace {
bool isEnabled();

//! Normally set from the environment at start; for tests
void setEnabled(bool enabled);

//! Start and stop the writer thread. Main thread, module init and deinit.
void start();
void stop();

void log(const char* format, ...)
#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 1, 2)))
#endif
;

//! Lines waiting to be written, and lines dropped because the queue was full. For tests.
size_t pendingCount();
size_t droppedCount();

//! Write the waiting lines through `sink` instead of the log. For tests; returns how many.
size_t drainTo(void (*sink)(const char* line, void* ctx), void* ctx);

//! The name of the plugin being loaded, so that host-context queries can be attributed. Main thread.
void setLoadingPlugin(const std::string& name);
std::string loadingPlugin();

//! Readable names for what crosses the interface
std::string iidName(const Steinberg::TUID iid);
std::string processContextStateToString(uint32_t state);
std::string processContextRequirementsToString(uint32_t flags);
std::string restartFlagsToString(int32_t flags);
}
