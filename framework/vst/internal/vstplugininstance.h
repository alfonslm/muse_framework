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

#pragma once

#include <atomic>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

#include "../ivstplugininstance.h"

#include "modularity/ioc.h"
#include "async/asyncable.h"
#include "async/notification.h"
#include "async/channel.h"
#include "audio/common/iaudiothreadsecurer.h"
#include "audio/common/audiotypes.h"

#include "../ivstmodulesrepository.h"
#include "../vsttypes.h"
#include "vstcomponenthandler.h"

namespace muse::vst {
class VstPluginProvider;
class VstPluginInstance : public IVstPluginInstance, public async::Asyncable
{
    muse::GlobalInject<muse::audio::IAudioThreadSecurer> threadSecurer;
    muse::GlobalInject<IVstModulesRepository> modulesRepo;

public:
    VstPluginInstance(const muse::audio::AudioResourceId& resourceId);
    ~VstPluginInstance() override;

    const muse::audio::AudioResourceId& resourceId() const override;
    const std::string& name() const override;
    VstPluginInstanceId id() const override;

    PluginViewPtr createView() const override;

    PluginControllerPtr controller() const override;
    PluginComponentPtr component() const override;
    PluginMidiMappingPtr midiMapping() const override;
    std::optional<uint32_t> processContextRequirements() const override;

    bool setComponentActive(bool active) override;
    bool setProcessing(bool processing) override;

    void registerEditor(const void* editor, const std::function<void()>& close) override;
    void unregisterEditor(const void* editor) override;

    void updatePluginConfig(const muse::audio::AudioUnitConfig& config) override;
    void refreshConfig() override;

    void load();

    //! Main thread, at shutdown only. Closes the editors, stops processing,
    //! deactivates and terminates the plugin and releases its module, so that the module's
    //! bundleExit / ModuleExit runs now. The instance stays alive but is no longer usable.
    //! PRECONDITION: the audio engine and driver have stopped (no process() call can run).
    //! Nothing synchronises this with the audio threads that read the provider
    //! (setComponentActive, setProcessing, component()), and an audio client's cached
    //! component pointer dangles afterwards. VSTModule::onDeinit() is the only caller.
    void unload();

    //! Main thread. Releases the plugins of instances that were destroyed on another thread,
    //! or before the main thread got round to it (see the destructor).
    static void releaseDeferredResources();

    //! Main thread, at shutdown. From now on, an instance destroyed on the main thread releases
    //! its plugin at once instead of deferring it to the main event loop, which no longer runs.
    static void setShuttingDown();

    bool isLoaded() const override;

    async::Notification loadingCompleted() const override;

    async::Channel<muse::audio::AudioUnitConfig> pluginSettingsChanged() const override;

private:
    struct Resources {
        muse::audio::AudioResourceId resourceId;
        std::shared_ptr<VstPluginProvider> provider;
        PluginModulePtr module;
        bool isComponentActive = false;
        bool isProcessing = false;
    };

    Resources takeResources();
    void closeEditors();

    static void releaseResources(Resources& resources);
    static std::vector<Resources>& deferredResources();
    static std::mutex& deferredResourcesMutex();

    void syncControllerToComponentState();
    void rescanParams();
    void setPluginConfig(const muse::audio::AudioUnitConfig& config);

    VstPluginInstanceId m_id = 0;
    muse::audio::AudioResourceId m_resourceId;

    PluginModulePtr m_module = nullptr;
    std::unique_ptr<VstPluginProvider> m_pluginProvider;

    Steinberg::FUnknownPtr<VstComponentHandler> m_componentHandlerPtr = nullptr;

    VstMemoryStream m_componentStateBuffer;
    VstMemoryStream m_controllerStateBuffer;
    mutable async::Channel<muse::audio::AudioUnitConfig> m_pluginSettingsChanges;

    std::atomic_bool m_isLoaded = false;
    async::Notification m_loadingCompleted;

    std::optional<uint32_t> m_processContextRequirements;

    std::atomic_bool m_isComponentActive = false;
    std::atomic_bool m_isProcessing = false;

    std::mutex m_editorsMutex;
    std::map<const void*, std::function<void()> > m_editors;

    std::thread::id m_mainThreadId;
};
}
