#include "hosting/VST3Host.h"
#include "hosting/VST3Editor.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/common/memorystream.h"
#include "pluginterfaces/base/funknownimpl.h"
#include "pluginterfaces/base/ustring.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include <cmath>
#include <algorithm>
#include <windows.h>
#include <atomic>
#include <stdexcept>

using namespace Steinberg;
using namespace Steinberg::Vst;
namespace kj {
namespace {
void require(tresult result, const char* operation) {
    if (result != kResultOk)
        throw std::runtime_error(std::string(operation) + " failed (" + std::to_string(result) + ")");
}
struct Context : HostApplication {
    tresult PLUGIN_API queryInterface(const TUID iid, void** object) override {
        // The SDK example advertises optional interfaces this probe does not implement.
        if (FUnknownPrivate::iidEqual(iid, IPlugInterfaceSupport::iid)) {
            *object = nullptr;
            return kNoInterface;
        }
        return HostApplication::queryInterface(iid, object);
    }
    tresult PLUGIN_API getName(String128 name) override {
        UString(name, 128).fromAscii("KJ VST3 Test Host");
        return kResultOk;
    }
};
struct Handler : U::Implements<U::Directly<IComponentHandler>> {
    struct Parameter {
        ParamID id {}; std::atomic<double> value {0};
        std::atomic<bool> dirty {false}, touched {false};
        bool automatable = false;
        double modulation = 0, lastModulation = 0; // Audio thread only.
    };
    std::unique_ptr<Parameter[]> parameters;
    int32 count = 0;
    std::atomic<int32> restartFlags {0};
    void configure(IEditController* controller) {
        count = controller ? controller->getParameterCount() : 0;
        if (count < 0 || count > 100000) throw std::runtime_error("Invalid plugin parameter count");
        parameters = std::make_unique<Parameter[]>(count);
        for (int32 i = 0; i < count; ++i) {
            ParameterInfo info {};
            require(controller->getParameterInfo(i, info), "getParameterInfo");
            parameters[i].id = info.id;
            parameters[i].value = controller->getParamNormalized(info.id);
            parameters[i].automatable = (info.flags & ParameterInfo::kCanAutomate) && !(info.flags & ParameterInfo::kIsReadOnly);
        }
    }
    tresult PLUGIN_API beginEdit(ParamID) override { return kResultOk; }
    tresult PLUGIN_API performEdit(ParamID id, ParamValue value) override {
        return update(id, value, true);
    }
    tresult update(ParamID id, ParamValue value, bool touched = false) {
        if (!std::isfinite(value) || value < 0 || value > 1) return kInvalidArgument;
        for (int32 i = 0; i < count; ++i) if (parameters[i].id == id) {
            parameters[i].value.store(value, std::memory_order_relaxed);
            parameters[i].dirty.store(true, std::memory_order_release);
            if (touched && parameters[i].automatable) parameters[i].touched.store(true, std::memory_order_release);
            return kResultOk;
        }
        return kInvalidArgument;
    }
    tresult PLUGIN_API endEdit(ParamID) override { return kResultOk; }
    tresult PLUGIN_API restartComponent(int32 flags) override {
        if (flags & ~(kParamValuesChanged | kParamTitlesChanged)) return kNotImplemented;
        restartFlags.fetch_or(flags, std::memory_order_release);
        return kResultOk;
    }
};
}
struct VST3Host::Impl {
    // HostApplication has intentionally non-owning addRef/release methods.
    std::unique_ptr<Context> context = std::make_unique<Context>();
    IPtr<Handler> handler = owned(new Handler);
    std::unique_ptr<VST3Editor> editor;
    ParameterChanges parameters;
    VST3::Hosting::Module::Ptr module;
    IPtr<IComponent> component;
    IPtr<IEditController> controller;
    IPtr<IAudioProcessor> processor;
    IPtr<IConnectionPoint> componentConnection, controllerConnection;
    bool componentInitialized = false, controllerInitialized = false;
    bool componentConnected = false, controllerConnected = false;
    bool active = false, processing = false;
    int maxBlock = 0;
};
VST3Host::VST3Host() : impl(std::make_unique<Impl>()) {}
VST3Host::~VST3Host() { unload(); }
std::vector<VST3::Hosting::ClassInfo> VST3Host::scan(const std::string& path) {
    std::string error;
    auto module = VST3::Hosting::Module::create(path, error);
    if (!module) throw std::runtime_error("Module load: " + error);
    std::vector<VST3::Hosting::ClassInfo> classes;
    for (const auto& info : module->getFactory().classInfos())
        if (info.category() == kVstAudioEffectClass) classes.push_back(info);
    if (classes.empty()) throw std::runtime_error("No VST3 audio component classes");
    return classes;
}
void VST3Host::load(const std::string& path, const std::string& classId) {
    unload();
    try {
        auto& h = *impl;
        std::string error;
        h.module = VST3::Hosting::Module::create(path, error);
        if (!h.module) throw std::runtime_error("Module load: " + error);
        const auto& factory = h.module->getFactory();
        factory.setHostContext(h.context.get());
        for (const auto& info : factory.classInfos()) {
            if (info.category() == kVstAudioEffectClass && info.ID().toString() == classId) {
                h.component = factory.createInstance<IComponent>(info.ID());
                break;
            }
        }
        if (!h.component) throw std::runtime_error("Audio component class not found: " + classId);
        // setIoMode is optional and must precede initialize.
        h.component->setIoMode(kAdvanced);
        require(h.component->initialize(h.context.get()), "component.initialize");
        h.componentInitialized = true;
        h.processor = U::cast<IAudioProcessor>(h.component);
        if (!h.processor) throw std::runtime_error("Missing IAudioProcessor");
        h.controller = U::cast<IEditController>(h.component);
        if (!h.controller) {
            TUID id {};
            if (h.component->getControllerClassId(id) == kResultOk) {
                h.controller = factory.createInstance<IEditController>(VST3::UID(id));
                if (!h.controller) throw std::runtime_error("Cannot create edit controller");
                require(h.controller->initialize(h.context.get()), "controller.initialize");
                h.controllerInitialized = true;
            }
        }
        if (h.controller) {
            require(h.controller->setComponentHandler(h.handler), "setComponentHandler");
            if (h.controllerInitialized) {
                h.componentConnection = U::cast<IConnectionPoint>(h.component);
                h.controllerConnection = U::cast<IConnectionPoint>(h.controller);
                if (h.componentConnection && h.controllerConnection) {
                    require(h.componentConnection->connect(h.controllerConnection), "component.connect");
                    h.componentConnected = true;
                    require(h.controllerConnection->connect(h.componentConnection), "controller.connect");
                    h.controllerConnected = true;
                }
                MemoryStream state;
                if (h.component->getState(&state) == kResultOk) {
                    state.seek(0, IBStream::kIBSeekSet, nullptr);
                    // Stateless controllers may leave this optional operation unimplemented.
                    auto result = h.controller->setComponentState(&state);
                    if (result != kNotImplemented) require(result, "setComponentState");
                }
            }
        }
        // Some controllers (including JUCE plugins) expose parameters only after
        // the processor/controller connection and initial state synchronization.
        h.handler->configure(h.controller);
        h.parameters.setMaxParameters(h.handler->count);
        for (int32 i = 0; i < h.handler->count; ++i) {
            int32 queueIndex = 0, pointIndex = 0;
            if (auto* queue = h.parameters.addParameterData(h.handler->parameters[i].id, queueIndex))
                queue->addPoint(0, 0, pointIndex);
        }
        h.parameters.clearQueue();
    } catch (...) { unload(); throw; }
}
void VST3Host::prepare(double sampleRate, int maxBlockSize) {
    auto& h = *impl;
    if (!h.component || h.processing) throw std::runtime_error("prepare requires loaded, stopped plugin");
    if (!std::isfinite(sampleRate) || sampleRate <= 0 || maxBlockSize <= 0)
        throw std::invalid_argument("Invalid sample rate or block size");
    if (h.active) { require(h.component->setActive(false), "setActive(false)"); h.active = false; }
    require(h.processor->canProcessSampleSize(kSample32), "32-bit float processing");
    // Preserve the plugin's preferred layout, including instrument zero-input layouts.
    for (auto media : {kAudio, kEvent}) {
        for (auto direction : {kInput, kOutput}) {
            for (int32 i = 0; i < h.component->getBusCount(media, direction); ++i) {
                BusInfo info {};
                require(h.component->getBusInfo(media, direction, i, info), "getBusInfo");
                const bool enable = info.busType == kMain || (info.flags & BusInfo::kDefaultActive);
                require(h.component->activateBus(media, direction, i, enable), "activateBus");
            }
        }
    }
    ProcessSetup setup {kRealtime, kSample32, maxBlockSize, sampleRate};
    require(h.processor->setupProcessing(setup), "setupProcessing");
    require(h.component->setActive(true), "setActive(true)");
    h.active = true;
    h.maxBlock = maxBlockSize;
}
void VST3Host::startProcessing() {
    auto& h = *impl;
    if (!h.active || h.processing) throw std::runtime_error("Invalid startProcessing state");
    require(h.processor->setProcessing(true), "setProcessing(true)");
    h.processing = true;
}
void VST3Host::process(ProcessData& data) {
    auto& h = *impl;
    if (!h.processing || data.numSamples < 0 || data.numSamples > h.maxBlock ||
        data.symbolicSampleSize != kSample32 || data.processMode != kRealtime)
        throw std::runtime_error("Invalid process state, block size or format");
    h.parameters.clearQueue();
    auto* originalParameters = data.inputParameterChanges;
    if (originalParameters) for (int32 i = 0; i < originalParameters->getParameterCount(); ++i) {
        auto* source = originalParameters->getParameterData(i);
        if (!source) continue;
        int32 index = 0;
        auto* target = h.parameters.addParameterData(source->getParameterId(), index);
        if (!target) continue;
        for (int32 point = 0; point < source->getPointCount(); ++point) {
            int32 offset = 0; ParamValue value = 0;
            if (source->getPoint(point, offset, value) == kResultOk) target->addPoint(offset, value, index);
        }
    }
    for (int32 i = 0; i < h.handler->count; ++i) {
        auto& parameter = h.handler->parameters[i];
        bool dirty = parameter.dirty.exchange(false, std::memory_order_acq_rel);
        if (!dirty && parameter.modulation == parameter.lastModulation) continue;
        parameter.lastModulation = parameter.modulation;
        int32 index = 0;
        if (auto* queue = h.parameters.addParameterData(parameter.id, index))
            queue->addPoint(0, std::clamp(parameter.value.load(std::memory_order_relaxed) + parameter.modulation, 0.0, 1.0), index);
    }
    data.inputParameterChanges = &h.parameters;
    tresult result;
    try { result = h.processor->process(data); }
    catch (...) { data.inputParameterChanges = originalParameters; throw; }
    data.inputParameterChanges = originalParameters;
    require(result, "process");
}
void VST3Host::stopProcessing() {
    if (impl->processing) {
        require(impl->processor->setProcessing(false), "setProcessing(false)");
        impl->processing = false;
    }
}
IComponent* VST3Host::component() const { return impl->component; }
IEditController* VST3Host::controller() const { return impl->controller; }
void* VST3Host::openEditor(void* owner, bool visible) {
    auto& h = *impl;
    if (!h.controller) throw std::runtime_error("Plugin has no edit controller");
    if (!h.editor || !h.editor->isOpen()) {
        h.editor.reset();
        auto view = owned(h.controller->createView(ViewType::kEditor));
        h.editor = std::make_unique<VST3Editor>(std::move(view), owner, visible);
    } else if (visible) h.editor->show();
    return h.editor->nativeWindow();
}
void VST3Host::closeEditor() { impl->editor.reset(); }
Vst3PluginState VST3Host::captureState() {
    auto& h = *impl;
    if (!h.component || h.processing) throw std::runtime_error("Stop audio before capturing plugin state");
    serviceController();
    Vst3PluginState state;
    MemoryStream component, controller;
    auto capture = [](tresult result, MemoryStream& stream, bool& present, std::string& bytes) {
        if (result == kNotImplemented || result == kResultFalse) return;
        require(result, "getState");
        if (stream.getSize() > 64 * 1024 * 1024) throw std::runtime_error("Plugin state exceeds 64 MB");
        present = true;
        if (stream.getSize()) bytes.assign(stream.getData(), static_cast<size_t>(stream.getSize()));
    };
    capture(h.component->getState(&component), component, state.hasComponent, state.component);
    if (h.controller) {
        capture(h.controller->getState(&controller), controller, state.hasController, state.controller);
        for (int32 i = 0; i < h.handler->count; ++i) {
            auto id = h.handler->parameters[i].id;
            auto value = h.controller->getParamNormalized(id);
            if (std::isfinite(value) && value >= 0 && value <= 1) state.parameters.emplace_back(id, value);
        }
    }
    return state;
}
void VST3Host::restoreState(const Vst3PluginState& state) {
    auto& h = *impl;
    if (!h.component || h.active || h.processing) throw std::runtime_error("Restore requires an inactive loaded plugin");
    if (state.hasComponent) {
        MemoryStream stream(const_cast<char*>(state.component.data()), static_cast<TSize>(state.component.size()));
        require(h.component->setState(&stream), "component.setState");
        if (h.controller) {
            stream.seek(0, IBStream::kIBSeekSet, nullptr);
            const auto result = h.controller->setComponentState(&stream);
            if (result != kNotImplemented) require(result, "controller.setComponentState");
        }
    }
    if (state.hasController && h.controller) {
        MemoryStream stream(const_cast<char*>(state.controller.data()), static_cast<TSize>(state.controller.size()));
        require(h.controller->setState(&stream), "controller.setState");
    }
    if (h.controller) for (int32 i = 0; i < h.handler->count; ++i) {
        auto id = h.handler->parameters[i].id;
        h.handler->update(id, h.controller->getParamNormalized(id));
    }
    if (h.controller) for (const auto& parameter : state.parameters) {
        // Parameter IDs may disappear in a newer plugin version; retain the
        // plugin's restored state for those IDs instead of rejecting the project.
        bool known = false;
        for (int32 i = 0; i < h.handler->count; ++i) if (h.handler->parameters[i].id == parameter.first) known = true;
        if (known) setParameter(parameter.first, parameter.second);
    }
}
void VST3Host::setParameter(ParamID id, double value) {
    if (!impl->controller || !std::isfinite(value) || value < 0 || value > 1)
        throw std::invalid_argument("Invalid parameter edit");
    require(impl->controller->setParamNormalized(id, value), "setParamNormalized");
    require(impl->handler->update(id, value), "performEdit");
}
void VST3Host::serviceController() {
    auto& h = *impl;
    if (h.controller && (h.handler->restartFlags.exchange(0) & kParamValuesChanged))
        for (int32 i = 0; i < h.handler->count; ++i) {
            const auto id = h.handler->parameters[i].id;
            h.handler->update(id, h.controller->getParamNormalized(id));
        }
}
std::vector<Vst3ParameterTarget> VST3Host::takeTweakedParameters() {
    std::vector<Vst3ParameterTarget> result;
    auto& h = *impl;
    if (!h.controller) return result;
    for (int32 i = 0; i < h.handler->count; ++i) {
        auto& p = h.handler->parameters[i];
        if (!p.touched.exchange(false, std::memory_order_acq_rel)) continue;
        ParameterInfo info {};
        if (h.controller->getParameterInfo(i, info) != kResultOk || info.id != p.id) continue;
        int length = 0; while (length < 128 && info.title[length]) ++length;
        int size = WideCharToMultiByte(CP_UTF8, 0, reinterpret_cast<const wchar_t*>(info.title), length, nullptr, 0, nullptr, nullptr);
        std::string name(size, '\0');
        if (size) WideCharToMultiByte(CP_UTF8, 0, reinterpret_cast<const wchar_t*>(info.title), length, name.data(), size, nullptr, nullptr);
        if (name.empty()) name = "Parameter " + std::to_string(p.id);
        result.push_back({p.id, std::move(name)});
    }
    return result;
}
void VST3Host::clearModulation() noexcept {
    for (int32 i = 0; i < impl->handler->count; ++i) impl->handler->parameters[i].modulation = 0;
}
void VST3Host::addModulation(std::uint32_t id, double amount) noexcept {
    if (!std::isfinite(amount)) return;
    for (int32 i = 0; i < impl->handler->count; ++i) {
        auto& p = impl->handler->parameters[i];
        if (p.id == id && p.automatable) { p.modulation += amount; return; }
    }
}
void VST3Host::unload() {
    auto& h = *impl;
    h.editor.reset();
    if (h.processing) h.processor->setProcessing(false);
    if (h.active) h.component->setActive(false);
    h.processing = h.active = false;
    if (h.controller) h.controller->setComponentHandler(nullptr);
    if (h.controllerConnected) h.controllerConnection->disconnect(h.componentConnection);
    if (h.componentConnected) h.componentConnection->disconnect(h.controllerConnection);
    h.controllerConnected = h.componentConnected = false;
    h.controllerConnection.reset(); h.componentConnection.reset(); h.processor.reset();
    if (h.controllerInitialized) h.controller->terminate();
    h.controller.reset();
    if (h.componentInitialized) h.component->terminate();
    h.component.reset();
    h.controllerInitialized = h.componentInitialized = false;
    if (h.module) h.module->getFactory().setHostContext(nullptr);
    h.module.reset();
}
}
