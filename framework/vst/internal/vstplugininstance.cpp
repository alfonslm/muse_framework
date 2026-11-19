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

#include "vstplugininstance.h"

#include <cassert>
#include <chrono>

#include "modularity/ioc.h"
#include "vstpluginprovider.h"
#include "vsttrace.h"

#include "pluginterfaces/vst/ivstaudioprocessor.h"

#include "async/async.h"

#include "defer.h"
#include "log.h"

using namespace muse;
using namespace muse::vst;
using namespace muse::async;

static const std::string_view COMPONENT_STATE_KEY = "componentState";
static const std::string_view CONTROLLER_STATE_KEY = "controllerState";

static VstPluginInstanceId s_lastId = 0;
static std::atomic_bool s_isShuttingDown = false;

static void stateBufferFromString(VstMemoryStream& buffer, char* strData, const size_t strSize)
{
    if (strSize == 0) {
        return;
    }

    buffer.setSize(0);
    buffer.write(strData, static_cast<Steinberg::int32>(strSize), nullptr);
    buffer.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
}

VstPluginInstance::VstPluginInstance(const muse::audio::AudioResourceId& resourceId)
    : m_resourceId(resourceId), m_componentHandlerPtr(new VstComponentHandler())
{
    ONLY_AUDIO_THREAD(threadSecurer);

    m_id = ++s_lastId;

    //! NOTE: cached, because the destructor may run late in the shutdown, when the IoC is being reset
    m_mainThreadId = threadSecurer()->mainThreadId();

    m_componentHandlerPtr->pluginParamsChanged().onNotify(this, [this]() {
        Async::call(this, [this]() {
            rescanParams();
        }, threadSecurer()->mainThreadId());
    });
}

VstPluginInstance::~VstPluginInstance()
{
    //! NOTE: Signal early so any rescanParams() already queued to the main thread
    //! returns before touching members (provider, etc.)
    m_isLoaded = false;

    //! NOTE: No editor can be open here: an editor holds a reference to the instance

    Resources resources = takeResources();
    if (!resources.provider && !resources.module) {
        return;
    }

    //! NOTE: The plugin must be released on the main thread (see releaseResources).
    //! At shutdown the main event loop no longer runs, so a release posted to it would never
    //! happen and the module would stay loaded until exit(), where plugins crash in static
    //! destructors (e.g. Kontakt 8 on macOS). VSTModule::onDeinit() runs the deferred releases.
    if (s_isShuttingDown && std::this_thread::get_id() == m_mainThreadId) {
        releaseResources(resources);
        return;
    }

    {
        std::lock_guard lock(deferredResourcesMutex());
        deferredResources().push_back(std::move(resources));
    }

    Async::call(nullptr, []() {
        releaseDeferredResources();
    }, m_mainThreadId);
}

VstPluginInstance::Resources VstPluginInstance::takeResources()
{
    Resources resources;
    resources.resourceId = m_resourceId;
    resources.provider = std::move(m_pluginProvider);
    resources.module = std::move(m_module);
    resources.isComponentActive = m_isComponentActive.exchange(false);
    resources.isProcessing = m_isProcessing.exchange(false);
    return resources;
}

void VstPluginInstance::releaseResources(Resources& resources)
{
    //! MAIN THREAD

    //! NOTE: the order of destruction is important here.
    //! Stop processing and deactivate the component before the provider terminates it,
    //! and terminate it before the module is released (its destructor calls bundleExit / ModuleExit).
    //! This must happen on the main thread, after any editor view
    //! has been closed, to avoid crashes in plugins (e.g. ZENOLOGY)
    //! that free editor-dependent resources in setActive(false).
    if (resources.provider) {
        PluginComponentPtr component = resources.provider->component();
        if (component) {
            if (resources.isProcessing) {
                IAudioProcessorPtr processor(component.get());
                if (processor) {
                    processor->setProcessing(false);
                }
            }

            if (resources.isComponentActive) {
                component->setActive(false);
            }
        }
    }

    resources.provider.reset();

    if (auto repo = muse::modularity::globalIoc()->resolve<IVstModulesRepository>("vst")) {
        repo->removePluginModule(resources.resourceId);
    }

    resources.module.reset();
}

std::vector<VstPluginInstance::Resources>& VstPluginInstance::deferredResources()
{
    //! NOTE: intentionally leaked: it must not release plugins from a static destructor at exit
    static auto* resources = new std::vector<Resources>();
    return *resources;
}

std::mutex& VstPluginInstance::deferredResourcesMutex()
{
    static auto* mutex = new std::mutex();
    return *mutex;
}

void VstPluginInstance::releaseDeferredResources()
{
    //! MAIN THREAD

    std::vector<Resources> resources;
    {
        std::lock_guard lock(deferredResourcesMutex());
        resources.swap(deferredResources());
    }

    for (Resources& r : resources) {
        releaseResources(r);
    }
}

void VstPluginInstance::setShuttingDown()
{
    s_isShuttingDown = true;
}

void VstPluginInstance::unload()
{
    //! MAIN THREAD, audio stopped: see the precondition in the header
    assert(std::this_thread::get_id() == m_mainThreadId);

    m_isLoaded = false;

    closeEditors();

    Resources resources = takeResources();
    releaseResources(resources);
}

void VstPluginInstance::closeEditors()
{
    //! MAIN THREAD

    std::map<const void*, std::function<void()> > editors;
    {
        std::lock_guard lock(m_editorsMutex);
        editors.swap(m_editors);
    }

    for (auto& [editor, close] : editors) {
        if (close) {
            close();
        }
    }
}

void VstPluginInstance::registerEditor(const void* editor, const std::function<void()>& close)
{
    std::lock_guard lock(m_editorsMutex);
    m_editors[editor] = close;
}

void VstPluginInstance::unregisterEditor(const void* editor)
{
    std::lock_guard lock(m_editorsMutex);
    m_editors.erase(editor);
}

bool VstPluginInstance::setComponentActive(bool active)
{
    if (!m_isLoaded || !m_pluginProvider) {
        return false;
    }

    PluginComponentPtr component = m_pluginProvider->component();
    if (!component) {
        return false;
    }

    if (m_isComponentActive == active) {
        return true;
    }

    const Steinberg::tresult res = component->setActive(active);

    //! NOTE: remembered even if the call failed, so that the plugin
    //! is deactivated in any case before it is terminated
    m_isComponentActive = active;

    return res == Steinberg::kResultOk;
}

bool VstPluginInstance::setProcessing(bool processing)
{
    if (!m_isLoaded || !m_pluginProvider) {
        return false;
    }

    IAudioProcessorPtr processor(m_pluginProvider->component().get());
    if (!processor) {
        return false;
    }

    if (m_isProcessing == processing) {
        return true;
    }

    const Steinberg::tresult res = processor->setProcessing(processing);

    //! NOTE: many plugins return kNotImplemented here; the state is remembered anyway
    m_isProcessing = processing;

    return res == Steinberg::kResultOk || res == Steinberg::kNotImplemented;
}

const muse::audio::AudioResourceId& VstPluginInstance::resourceId() const
{
    return m_resourceId;
}

const std::string& VstPluginInstance::name() const
{
    return m_resourceId;
}

std::optional<uint32_t> VstPluginInstance::processContextRequirements() const
{
    return m_processContextRequirements;
}

VstPluginInstanceId VstPluginInstance::id() const
{
    return m_id;
}

/**
 * @brief VstPluginInstance::load
 * @note Some Vst plugins might not support loading in the background threads.
 *       So we have to ensure that this function being executed in the main thread
 */
void VstPluginInstance::load()
{
    Async::call(this, [this]() {
        ONLY_MAIN_THREAD(threadSecurer);

        const bool traced = trace::isEnabled();
        if (traced) {
            trace::setLoadingPlugin(m_resourceId);
        }
        DEFER {
            if (traced) {
                trace::setLoadingPlugin(std::string());
            }
        };

        m_componentHandlerPtr->setTraceName(m_resourceId);

        m_module = modulesRepo()->pluginModule(m_resourceId);
        if (!m_module) {
            modulesRepo()->addPluginModule(m_resourceId);
            m_module = modulesRepo()->pluginModule(m_resourceId);
        }

        if (!m_module) {
            LOGE() << "Unable to find vst plugin module, resourceId: " << m_resourceId;
            return;
        }

        const auto& factory = m_module->getFactory();

        for (const ClassInfo& classInfo : factory.classInfos()) {
            if (classInfo.category() != kVstAudioEffectClass) {
                continue;
            }

            m_pluginProvider = std::make_unique<VstPluginProvider>(factory, classInfo);
            break;
        }

        if (!m_pluginProvider) {
            LOGE() << "Unable to load vst plugin provider";
            return;
        }

        if (!m_pluginProvider->init()) {
            LOGE() << "Unable to initialize vst plugin provider";
            return;
        }

        //! Asked once, after initialize and before the component is activated
        if (PluginComponentPtr component = m_pluginProvider->component()) {
            Steinberg::FUnknownPtr<Steinberg::Vst::IProcessContextRequirements> requirements(component);
            if (requirements) {
                m_processContextRequirements = requirements->getProcessContextRequirements();
            }
        }

        PluginControllerPtr controller = m_pluginProvider->controller();
        if (!controller) {
            return;
        }

        controller->setComponentHandler(m_componentHandlerPtr);
        syncControllerToComponentState();

        m_isLoaded = true;
        m_loadingCompleted.notify();
    }, threadSecurer()->mainThreadId());
}

void VstPluginInstance::syncControllerToComponentState()
{
    ONLY_MAIN_THREAD(threadSecurer);

    // Synchronize controller to the component's default state.
    // Some plugins (e.g. Roland Cloud ZENOLOGY) rely on this to
    // fully initialize internal data structures; without it the
    // controller may crash when the editor UI is opened.
    PluginComponentPtr component = m_pluginProvider->component();
    PluginControllerPtr controller = m_pluginProvider->controller();

    if (!component || !controller) {
        return;
    }

    m_componentStateBuffer.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    m_componentStateBuffer.setSize(0);

    if (component->getState(&m_componentStateBuffer) != Steinberg::kResultOk) {
        return;
    }

    if (m_componentStateBuffer.getSize() > 0) {
        m_componentStateBuffer.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
        controller->setComponentState(&m_componentStateBuffer);
    }
}

void VstPluginInstance::rescanParams()
{
    ONLY_MAIN_THREAD(threadSecurer);

    if (!m_isLoaded) {
        return;
    }

    if (!m_pluginProvider) {
        LOGE() << "Plugin provider is not initialized";
        return;
    }

    PluginComponentPtr component = m_pluginProvider->component();
    PluginControllerPtr controller = m_pluginProvider->controller();

    if (!controller || !component) {
        return;
    }

    //! NOTE Traced: a plugin may rebuild internal data while its state is read or written on the
    //! main thread, at the same time as process() runs on the audio thread
    const bool traced = trace::isEnabled();
    const auto traceStart = std::chrono::steady_clock::now();
    if (traced) {
        trace::log("%s: reading its state on the main thread (the plugin reported a change)", m_resourceId.c_str());
    }
    DEFER {
        if (traced) {
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - traceStart).count();
            trace::log("%s: state read done in %.1f ms (component %lld bytes, controller %lld bytes)", m_resourceId.c_str(), ms,
                       static_cast<long long>(m_componentStateBuffer.getSize()),
                       static_cast<long long>(m_controllerStateBuffer.getSize()));
        }
    };

    m_componentStateBuffer.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    m_componentStateBuffer.setSize(0);

    Steinberg::tresult res = component->getState(&m_componentStateBuffer);
    if (res != Steinberg::kResultOk && res != Steinberg::kNotImplemented) {
        LOGW() << "Component state scan failed: " << m_resourceId;
        return;
    }

    m_controllerStateBuffer.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    m_controllerStateBuffer.setSize(0);

    res = controller->getState(&m_controllerStateBuffer);
    if (res != Steinberg::kResultOk && res != Steinberg::kNotImplemented) {
        LOGW() << "Controller state scan failed: " << m_resourceId;
        return;
    }

    muse::audio::AudioUnitConfig updatedConfig;

    if (m_componentStateBuffer.getSize() > 0) {
        updatedConfig.emplace(COMPONENT_STATE_KEY, std::string(m_componentStateBuffer.getData(), m_componentStateBuffer.getSize()));
    }

    if (m_controllerStateBuffer.getSize() > 0) {
        updatedConfig.emplace(CONTROLLER_STATE_KEY, std::string(m_controllerStateBuffer.getData(), m_controllerStateBuffer.getSize()));
    }

    m_pluginSettingsChanges.send(updatedConfig);
}

void VstPluginInstance::setPluginConfig(const audio::AudioUnitConfig& config)
{
    ONLY_MAIN_THREAD(threadSecurer);

    if (!m_isLoaded) {
        return;
    }

    if (!m_pluginProvider) {
        LOGE() << "Plugin provider is not initialized";
        return;
    }

    PluginControllerPtr controller = m_pluginProvider->controller();
    PluginComponentPtr component = m_pluginProvider->component();

    if (!controller || !component) {
        LOGE() << "Unable to update settings for VST plugin";
        return;
    }

    auto componentState = config.find(COMPONENT_STATE_KEY.data());
    auto controllerState = config.find(CONTROLLER_STATE_KEY.data());

    if (componentState == config.end() && controllerState == config.end()) {
        return;
    }

    m_componentHandlerPtr->setSuppressNotify(true);
    DEFER {
        m_componentHandlerPtr->setSuppressNotify(false);
    };

    const bool traced = trace::isEnabled();
    const auto traceStart = std::chrono::steady_clock::now();
    if (traced) {
        trace::log("%s: writing its saved state on the main thread (component %lld bytes, controller %lld bytes)", m_resourceId.c_str(),
                   componentState != config.end() ? static_cast<long long>(componentState->second.size()) : 0LL,
                   controllerState != config.end() ? static_cast<long long>(controllerState->second.size()) : 0LL);
    }
    DEFER {
        if (traced) {
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - traceStart).count();
            trace::log("%s: state write done in %.1f ms", m_resourceId.c_str(), ms);
        }
    };

    try {
        if (componentState != config.end() && !componentState->second.empty()) {
            stateBufferFromString(m_componentStateBuffer, const_cast<char*>(componentState->second.c_str()), componentState->second.size());

            if (component->setState(&m_componentStateBuffer) == Steinberg::kResultOk) {
                m_componentStateBuffer.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
                controller->setComponentState(&m_componentStateBuffer);
            }
        }
    } catch (...) {
        LOGW() << "Component state restore failed: " << m_resourceId;
    }

    try {
        if (controllerState != config.end() && !controllerState->second.empty()) {
            stateBufferFromString(m_controllerStateBuffer, const_cast<char*>(controllerState->second.data()),
                                  controllerState->second.size());
            controller->setState(&m_controllerStateBuffer);
        }
    } catch (...) {
        LOGW() << "Controller state restore failed: " << m_resourceId;
    }
}

PluginViewPtr VstPluginInstance::createView() const
{
    ONLY_MAIN_THREAD(threadSecurer);

    if (!m_isLoaded || !m_pluginProvider) {
        return nullptr;
    }

    PluginControllerPtr controller = m_pluginProvider->controller();
    if (!controller) {
        return nullptr;
    }

    return owned(controller->createView(PluginEditorViewType::kEditor));
}

PluginControllerPtr VstPluginInstance::controller() const
{
    ONLY_AUDIO_THREAD(threadSecurer);

    if (!m_isLoaded || !m_pluginProvider) {
        return nullptr;
    }

    return m_pluginProvider->controller();
}

PluginComponentPtr VstPluginInstance::component() const
{
    // TODO: Audio engine or process thread
    // ONLY_AUDIO_THREAD(threadSecurer);

    if (!m_isLoaded || !m_pluginProvider) {
        return nullptr;
    }

    return m_pluginProvider->component();
}

PluginMidiMappingPtr VstPluginInstance::midiMapping() const
{
    ONLY_AUDIO_THREAD(threadSecurer);

    if (!m_isLoaded || !m_pluginProvider) {
        return nullptr;
    }

    return m_pluginProvider->midiMapping();
}

void VstPluginInstance::updatePluginConfig(const audio::AudioUnitConfig& config)
{
    ONLY_AUDIO_THREAD(threadSecurer);

    Async::call(this, [this, config]() {
        setPluginConfig(config);
    }, threadSecurer()->mainThreadId());
}

void VstPluginInstance::refreshConfig()
{
    ONLY_MAIN_THREAD(threadSecurer);

    rescanParams();
}

bool VstPluginInstance::isLoaded() const
{
    ONLY_AUDIO_OR_MAIN_THREAD(threadSecurer);

    return m_isLoaded;
}

Notification VstPluginInstance::loadingCompleted() const
{
    return m_loadingCompleted;
}

async::Channel<muse::audio::AudioUnitConfig> VstPluginInstance::pluginSettingsChanged() const
{
    ONLY_AUDIO_THREAD(threadSecurer);

    return m_pluginSettingsChanges;
}
