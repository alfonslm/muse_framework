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

#include "vsthostcontext.h"

#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstpluginterfacesupport.h"
#include "public.sdk/source/vst/hosting/pluginterfacesupport.h"

#include "vsttrace.h"

using namespace muse::vst;
using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {
const char* answerName(tresult result)
{
    return result == kResultOk ? "yes" : "no";
}

std::string plugin()
{
    const std::string name = trace::loadingPlugin();
    return name.empty() ? std::string("a plugin") : name;
}

//! Passes IPlugInterfaceSupport questions to the SDK's object and logs them
class TracingPlugInterfaceSupport : public IPlugInterfaceSupport
{
public:
    explicit TracingPlugInterfaceSupport(IPtr<IPlugInterfaceSupport> inner)
        : m_inner(std::move(inner))
    {
        FUNKNOWN_CTOR
    }

    virtual ~TracingPlugInterfaceSupport()
    {
        FUNKNOWN_DTOR
    }

    tresult PLUGIN_API isPlugInterfaceSupported(const TUID iid) override
    {
        const tresult result = m_inner ? m_inner->isPlugInterfaceSupported(iid) : kResultFalse;
        trace::log("%s asks whether the host supports %s: %s", plugin().c_str(), trace::iidName(iid).c_str(),
                   result == kResultTrue ? "yes" : "no");
        return result;
    }

    DECLARE_FUNKNOWN_METHODS

private:
    IPtr<IPlugInterfaceSupport> m_inner;
};

IMPLEMENT_FUNKNOWN_METHODS(TracingPlugInterfaceSupport, IPlugInterfaceSupport, IPlugInterfaceSupport::iid)
}

VstHostContext::VstHostContext()
{
    //! VstAudioClient asks plugins for their process context requirements after setupProcessing
    if (PlugInterfaceSupport* support = getPlugInterfaceSupport()) {
        support->addPlugInterfaceSupported(IProcessContextRequirements::iid);
    }
}

tresult PLUGIN_API VstHostContext::queryInterface(const TUID iid, void** obj)
{
    tresult result = kNoInterface;

    if (FUnknownPrivate::iidEqual(iid, IPlugInterfaceSupport::iid) && getPlugInterfaceSupport()) {
        //! NOTE HostApplication::queryInterface in VST3 SDK 3.7.12 asks its PlugInterfaceSupport for
        //! the wrong interface (its own iid instead of the one requested), so plugins never got it.
        //! Later SDK versions fix that; answer it here
        IPlugInterfaceSupport* support = getPlugInterfaceSupport();
        if (trace::isEnabled()) {
            //! A wrapper that logs the questions
            *obj = new TracingPlugInterfaceSupport(IPtr<IPlugInterfaceSupport>(support));
        } else {
            support->addRef();
            *obj = support;
        }
        result = kResultOk;
    } else {
        result = HostApplication::queryInterface(iid, obj);
    }

    trace::log("%s asks the host context for %s: %s", plugin().c_str(), trace::iidName(iid).c_str(), answerName(result));

    return result;
}

tresult PLUGIN_API VstHostContext::createInstance(TUID cid, TUID iid, void** obj)
{
    const tresult result = HostApplication::createInstance(cid, iid, obj);
    trace::log("%s asks the host to create %s (as %s): %s", plugin().c_str(), trace::iidName(cid).c_str(),
               trace::iidName(iid).c_str(), answerName(result));
    return result;
}
