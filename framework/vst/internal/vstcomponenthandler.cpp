/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * MuseScore Studio
 * Music Composition & Notation
 *
 * Copyright (C) 2021 MuseScore Limited and others
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
#include "vstcomponenthandler.h"

#include "vstprocesscontext.h"
#include "vsttrace.h"

using namespace muse::vst;
using namespace muse::async;

IMPLEMENT_FUNKNOWN_METHODS(VstAdvancedHandler, Steinberg::Vst::IComponentHandler2, Steinberg::Vst::IComponentHandler2::iid)

IMPLEMENT_REFCOUNT(VstComponentHandler)

::Steinberg::tresult PLUGIN_API VstComponentHandler::queryInterface(const ::Steinberg::TUID _iid, void** obj)
{
    QUERY_INTERFACE(_iid, obj, ::Steinberg::FUnknown::iid, IComponentHandler)
    QUERY_INTERFACE(_iid, obj, IComponentHandler::iid, IComponentHandler)

    if (::Steinberg::FUnknownPrivate::iidEqual(_iid, IAdvancedComponentHandler::iid)) {
        return m_advancedHandler->queryInterface(_iid, obj);
    }

    if (::Steinberg::FUnknownPrivate::iidEqual(_iid, Steinberg::Vst::IComponentHandlerSystemTime::iid)) {
        return m_systemTimeHandler->queryInterface(_iid, obj);
    }

    if (trace::isEnabled()) {
        trace::log("%s asks the component handler for %s: no", m_traceName.c_str(), trace::iidName(_iid).c_str());
    }

    *obj = nullptr;
    return ::Steinberg::kNoInterface;
}

VstComponentHandler::VstComponentHandler()
    : m_advancedHandler(new VstAdvancedHandler(m_paramsChangedNotify)), m_systemTimeHandler(Steinberg::owned(new VstSystemTimeHandler()))
{
}

Channel<PluginParamId, PluginParamValue> VstComponentHandler::pluginParamChanged() const
{
    return m_paramChanged;
}

Notification VstComponentHandler::pluginParamsChanged() const
{
    return m_paramsChangedNotify;
}

void VstComponentHandler::setSuppressNotify(bool suppress)
{
    m_advancedHandler->setSuppressNotify(suppress);
}

void VstComponentHandler::setTraceName(const std::string& name)
{
    m_traceName = name;
}

Steinberg::tresult VstComponentHandler::beginEdit(Steinberg::Vst::ParamID /*id*/)
{
    return Steinberg::kResultOk;
}

Steinberg::tresult VstComponentHandler::performEdit(Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue valueNormalized)
{
    if (trace::isEnabled()) {
        //! Counted, not logged one by one: a knob turned in the editor sends many
        const auto now = std::chrono::steady_clock::now();
        if (m_traceEditCount == 0) {
            m_traceEditWindowStart = now;
        }
        ++m_traceEditCount;
        if (now - m_traceEditWindowStart >= std::chrono::seconds(1)) {
            trace::log("%s changed parameters %d times in %.1f s (last: id %u)", m_traceName.c_str(), m_traceEditCount,
                       std::chrono::duration<double>(now - m_traceEditWindowStart).count(), static_cast<unsigned>(id));
            m_traceEditCount = 0;
        }
    }

    if (!m_advancedHandler->suppressNotify()) {
        m_paramChanged.send(id, valueNormalized);
    }

    return Steinberg::kResultOk;
}

Steinberg::tresult VstComponentHandler::endEdit(Steinberg::Vst::ParamID /*id*/)
{
    if (!m_advancedHandler->suppressNotify()) {
        m_paramsChangedNotify.notify();
    }

    return Steinberg::kResultOk;
}

Steinberg::tresult VstComponentHandler::restartComponent(Steinberg::int32 flags)
{
    if (trace::isEnabled()) {
        trace::log("%s asks the host to restart the component: %s (ignored)", m_traceName.c_str(),
                   trace::restartFlagsToString(flags).c_str());
    }

    return Steinberg::kResultOk;
}

VstAdvancedHandler::VstAdvancedHandler(async::Notification notifier)
    : m_paramsChanged(notifier)
{
}

void VstAdvancedHandler::setSuppressNotify(bool suppress)
{
    m_suppressNotify.store(suppress, std::memory_order_relaxed);
}

bool VstAdvancedHandler::suppressNotify() const
{
    return m_suppressNotify.load(std::memory_order_relaxed);
}

Steinberg::tresult VstAdvancedHandler::setDirty(Steinberg::TBool /*state*/)
{
    if (!suppressNotify()) {
        m_paramsChanged.notify();
    }

    return Steinberg::kResultOk;
}

Steinberg::tresult VstAdvancedHandler::requestOpenEditor(Steinberg::FIDString /*name*/)
{
    return Steinberg::kResultOk;
}

Steinberg::tresult VstAdvancedHandler::startGroupEdit()
{
    return Steinberg::kResultOk;
}

Steinberg::tresult VstAdvancedHandler::finishGroupEdit()
{
    if (!suppressNotify()) {
        m_paramsChanged.notify();
    }

    return Steinberg::kResultOk;
}

IMPLEMENT_FUNKNOWN_METHODS(VstSystemTimeHandler, Steinberg::Vst::IComponentHandlerSystemTime,
                           Steinberg::Vst::IComponentHandlerSystemTime::iid)

VstSystemTimeHandler::VstSystemTimeHandler()
{
    FUNKNOWN_CTOR
}

Steinberg::tresult PLUGIN_API VstSystemTimeHandler::getSystemTime(Steinberg::int64& systemTime)
{
    systemTime = vstSystemTimeNs();
    return Steinberg::kResultOk;
}
