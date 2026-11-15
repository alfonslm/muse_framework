/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * MuseScore Studio
 * Music Composition & Notation
 *
 * Copyright (C) 2025 MuseScore Limited and others
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

#include <functional>

#include "global/async/notification.h"

#include "vsttypes.h"
#include "audio/common/audiotypes.h"

namespace muse::vst {
class IVstPluginInstance
{
public:

    virtual ~IVstPluginInstance() = default;

    virtual const muse::audio::AudioResourceId& resourceId() const = 0;
    virtual const std::string& name() const = 0;
    virtual VstPluginInstanceId id() const = 0;

    virtual bool isLoaded() const = 0;
    virtual async::Notification loadingCompleted() const = 0;

    virtual PluginViewPtr createView() const = 0;
    virtual PluginControllerPtr controller() const = 0;
    virtual PluginComponentPtr component() const = 0;
    virtual PluginMidiMappingPtr midiMapping() const = 0;

    //! Audio processing state: IComponent::setActive and IAudioProcessor::setProcessing.
    //! The audio client switches it through the instance, so that the instance knows
    //! how to stop and deactivate the plugin when it is released.
    virtual bool setComponentActive(bool active) = 0;
    virtual bool setProcessing(bool processing) = 0;

    //! Main thread. An open editor registers how to close itself (detach its IPlugView),
    //! so that the instance can close it before the plugin is deactivated and unloaded.
    virtual void registerEditor(const void* editor, const std::function<void()>& close) = 0;
    virtual void unregisterEditor(const void* editor) = 0;

    virtual void updatePluginConfig(const muse::audio::AudioUnitConfig& config) = 0;
    virtual void refreshConfig() = 0;
    virtual async::Channel<muse::audio::AudioUnitConfig> pluginSettingsChanged() const = 0;
};
}
