#pragma once
#include "public.sdk/source/vst/hosting/module.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include <memory>
#include "core/vst3_state.h"
namespace Steinberg { namespace Vst { class IEditController; } }

namespace kj {
// Experimental headless host. Call lifecycle methods on one COM-initialized UI
// thread. The audio thread calls startProcessing/process/stopProcessing only;
// join it before prepare/unload. No concurrent access is supported yet.
class VST3Host {
public:
    VST3Host();
    ~VST3Host();
    VST3Host(const VST3Host&) = delete;
    VST3Host& operator=(const VST3Host&) = delete;
    static std::vector<VST3::Hosting::ClassInfo> scan(const std::string& path);
    void load(const std::string& path, const std::string& classId);
    void prepare(double sampleRate, int maxBlockSize);
    void startProcessing();
    void process(Steinberg::Vst::ProcessData& data);
    void stopProcessing();
    void unload();
    void* openEditor(void* owner = nullptr, bool visible = true);
    void closeEditor();
    void serviceController();
    std::vector<Vst3ParameterTarget> takeTweakedParameters(); // GUI thread.
    void clearModulation() noexcept; // Audio thread, once per block.
    void addModulation(std::uint32_t id, double amount) noexcept;
    void setParameter(Steinberg::Vst::ParamID id, double value);
    Vst3PluginState captureState(); // Main thread, with audio stopped/joined.
    void restoreState(const Vst3PluginState& state); // Loaded but not prepared.
    Steinberg::Vst::IEditController* controller() const;
    Steinberg::Vst::IComponent* component() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
