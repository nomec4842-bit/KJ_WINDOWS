#include "public.sdk/source/vst/vstaudioeffect.h"
#include "public.sdk/source/vst/vsteditcontroller.h"
#include "public.sdk/source/main/pluginfactory.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include <algorithm>
#include <thread>
#include <chrono>
#include <vector>
using namespace Steinberg;
using namespace Steinberg::Vst;
static const FUID gainId(0x173A0101, 0x341A4020, 0x9923AA10, 0x783CAB01);
static const FUID clipId(0x173A0102, 0x341A4020, 0x9923AA10, 0x783CAB01);
static const FUID controllerId(0x173A0103, 0x341A4020, 0x9923AA10, 0x783CAB01);
class Processor : public AudioEffect {
    bool clip; double gain = 0.5;
public:
    explicit Processor(bool clip) : clip(clip) { setControllerClass(controllerId); }
    static FUnknown* makeGain(void*) { return static_cast<IAudioProcessor*>(new Processor(false)); }
    static FUnknown* makeClip(void*) { return static_cast<IAudioProcessor*>(new Processor(true)); }
    tresult PLUGIN_API initialize(FUnknown* context) override {
        // Make initialization measurably slow for the live-insertion test.
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        auto r = AudioEffect::initialize(context); if (r != kResultOk) return r;
        addAudioInput(STR16("Stereo In"), SpeakerArr::kStereo); addAudioOutput(STR16("Stereo Out"), SpeakerArr::kStereo); return kResultOk;
    }
    tresult PLUGIN_API setState(IBStream* s) override { return s->read(&gain, sizeof(gain)); }
    tresult PLUGIN_API getState(IBStream* s) override { return s->write(&gain, sizeof(gain)); }
    tresult PLUGIN_API setProcessing(TBool) override { return kResultOk; }
    tresult PLUGIN_API process(ProcessData& d) override {
        if (d.inputParameterChanges) for (int32 i = 0; i < d.inputParameterChanges->getParameterCount(); ++i) {
            auto* q = d.inputParameterChanges->getParameterData(i); int32 offset;
            if (q && q->getParameterId() == 1 && q->getPointCount()) q->getPoint(q->getPointCount() - 1, offset, gain);
        }
        if (!d.numInputs || !d.numOutputs) return kResultOk;
        d.outputs[0].silenceFlags = 0;
        for (int c = 0; c < 2; ++c) for (int i = 0; i < d.numSamples; ++i) {
            float x = d.inputs[0].channelBuffers32[c][i];
            d.outputs[0].channelBuffers32[c][i] = clip ? std::clamp(x, -0.25f, 0.25f) : x * static_cast<float>(gain * 4);
        }
        return kResultOk;
    }
};
class Controller;
static std::vector<Controller*> controllers;
class Controller : public EditController {
public:
    Controller() { controllers.push_back(this); }
    ~Controller() override { controllers.erase(std::remove(controllers.begin(), controllers.end(), this), controllers.end()); }
    static FUnknown* make(void*) { return static_cast<IEditController*>(new Controller); }
    tresult PLUGIN_API initialize(FUnknown* context) override {
        auto r = EditController::initialize(context); if (r != kResultOk) return r;
        parameters.addParameter(STR16("Gain"), nullptr, 0, 0.5, ParameterInfo::kCanAutomate, 1);
        parameters.addParameter(STR16("Meter"), nullptr, 0, 0, ParameterInfo::kIsReadOnly, 2); return kResultOk;
    }
    tresult PLUGIN_API setComponentState(IBStream* s) override {
        double v; auto r = s->read(&v, sizeof(v)); if (r == kResultOk) setParamNormalized(1, v); return r;
    }
    tresult PLUGIN_API setState(IBStream* s) override { return setComponentState(s); }
    tresult PLUGIN_API getState(IBStream* s) override { double v = getParamNormalized(1); return s->write(&v, sizeof(v)); }
};
// Simulate the same begin/perform/endEdit callbacks as a native plugin knob.
extern "C" __declspec(dllexport) void KJTestTweakAll(unsigned id, double value) {
    for (auto* c : controllers) { c->setParamNormalized(id, value); c->beginEdit(id); c->performEdit(id, value); c->endEdit(id); }
}
extern "C" __declspec(dllexport) bool InitDll() { return true; }
extern "C" __declspec(dllexport) bool ExitDll() { return true; }
BEGIN_FACTORY_DEF("KJ Tests", "", "")
DEF_CLASS2(INLINE_UID_FROM_FUID(gainId), PClassInfo::kManyInstances, kVstAudioEffectClass, "KJ Test Gain", kDistributable, "Fx", "1.0", kVstVersionString, Processor::makeGain)
DEF_CLASS2(INLINE_UID_FROM_FUID(clipId), PClassInfo::kManyInstances, kVstAudioEffectClass, "KJ Test Clip", kDistributable, "Fx", "1.0", kVstVersionString, Processor::makeClip)
DEF_CLASS2(INLINE_UID_FROM_FUID(controllerId), PClassInfo::kManyInstances, kVstComponentControllerClass, "KJ Test Controller", 0, "", "1.0", kVstVersionString, Controller::make)
END_FACTORY
