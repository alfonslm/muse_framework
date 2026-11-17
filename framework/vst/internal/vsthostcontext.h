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

#include "public.sdk/source/vst/hosting/hostclasses.h"

namespace muse::vst {
//! The host application object every plugin gets (IHostApplication).
//! It behaves exactly like the SDK's HostApplication; with the VST trace on (vsttrace.h) it also
//! logs which interfaces plugins ask it for, which objects they create through it, and which
//! optional host interfaces they ask about through IPlugInterfaceSupport.
class VstHostContext : public Steinberg::Vst::HostApplication
{
public:
    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid, void** obj) override;
    Steinberg::tresult PLUGIN_API createInstance(Steinberg::TUID cid, Steinberg::TUID iid, void** obj) override;
};
}
