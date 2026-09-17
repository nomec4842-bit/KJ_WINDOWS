#include "hosting/VST3Host.h"
#include "public.sdk/source/vst/hosting/processdata.h"
#include "public.sdk/source/vst/hosting/eventlist.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include <windows.h>
#include <objbase.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace Steinberg;
using namespace Steinberg::Vst;
void check(bool condition, const char* text) {
    if (!condition) throw std::runtime_error(text);
}
struct ComScope {
    ComScope() { check(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)), "COM initialization failed"); }
    ~ComScope() { CoUninitialize(); }
};
double render(kj::VST3Host& host, double rate, int blockSize, bool instrument) {
    host.prepare(rate, blockSize);
    HostProcessData data;
    check(data.prepare(*host.component(), blockSize, kSample32), "Buffer allocation failed");
    check(data.numOutputs > 0, "No audio output bus");
    const int eventInputs = host.component()->getBusCount(kEvent, kInput);
    check(!instrument || eventInputs > 0, "Instrument has no event input");
    EventList events(8), outputEvents(1024);
    ParameterChanges inputParameters, outputParameters;
    ProcessContext context {};
    context.sampleRate = rate;
    context.tempo = 120;
    context.timeSigNumerator = 4; context.timeSigDenominator = 4;
    context.state = ProcessContext::kPlaying | ProcessContext::kTempoValid |
        ProcessContext::kTimeSigValid | ProcessContext::kProjectTimeMusicValid;
    data.processMode = kRealtime;
    data.inputEvents = &events; data.outputEvents = &outputEvents;
    data.inputParameterChanges = &inputParameters; data.outputParameterChanges = &outputParameters;
    data.processContext = &context;
    std::exception_ptr failure;
    std::atomic<bool> done {false};
    double peak = 0;
    std::thread worker([&] {
        try {
            host.startProcessing();
            try {
                for (int block = 0; block < 256; ++block) {
                    data.numSamples = block % 3 == 0 ? 1 : (block % 3 == 1 ? blockSize / 2 : blockSize);
                    events.clear(); outputEvents.clear(); outputParameters.clearQueue();
                    if (instrument && (block == 0 || block == 192)) {
                        Event event {};
                        event.busIndex = 0; event.sampleOffset = 0;
                        event.ppqPosition = context.projectTimeMusic;
                        if (block == 0) {
                            event.type = Event::kNoteOnEvent;
                            event.noteOn.channel = 0; event.noteOn.pitch = 60;
                            event.noteOn.velocity = 0.8f; event.noteOn.noteId = 1;
                        } else {
                            event.type = Event::kNoteOffEvent;
                            event.noteOff.channel = 0; event.noteOff.pitch = 60;
                            event.noteOff.velocity = 0; event.noteOff.noteId = 1;
                        }
                        check(events.addEvent(event) == kResultOk, "Cannot enqueue note");
                    }
                    for (int bus = 0; bus < data.numInputs; ++bus) {
                        data.inputs[bus].silenceFlags = instrument ? HostProcessData::kAllChannelsSilent : 0;
                        for (int ch = 0; ch < data.inputs[bus].numChannels; ++ch)
                            for (int i = 0; i < data.numSamples; ++i)
                                data.inputs[bus].channelBuffers32[ch][i] = instrument ? 0.f :
                                    static_cast<float>(0.1 * std::sin(6.283185307179586 * 220 *
                                        (context.projectTimeSamples + i) / rate));
                    }
                    for (int bus = 0; bus < data.numOutputs; ++bus) {
                        data.outputs[bus].silenceFlags = 0;
                        for (int ch = 0; ch < data.outputs[bus].numChannels; ++ch)
                            std::fill_n(data.outputs[bus].channelBuffers32[ch], data.numSamples, 0.f);
                    }
                    host.process(data);
                    for (int bus = 0; bus < data.numOutputs; ++bus)
                        for (int ch = 0; ch < data.outputs[bus].numChannels; ++ch)
                            for (int i = 0; i < data.numSamples; ++i) {
                                const auto sample = data.outputs[bus].channelBuffers32[ch][i];
                                check(std::isfinite(sample), "Non-finite audio output");
                                if (ch >= 64 || !(data.outputs[bus].silenceFlags & (uint64(1) << ch)))
                                    peak = std::max(peak, std::abs(static_cast<double>(sample)));
                            }
                    context.projectTimeSamples += data.numSamples;
                    context.projectTimeMusic = context.projectTimeSamples * 2.0 / rate;
                }
            } catch (...) { host.stopProcessing(); throw; }
            host.stopProcessing();
        } catch (...) { failure = std::current_exception(); }
        done.store(true);
    });
    // Service plugins' main-thread timers while their processor runs on the audio thread.
    while (!done.load()) {
        MSG message;
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message); DispatchMessageW(&message);
        }
        Sleep(1);
    }
    worker.join();
    if (failure) std::rethrow_exception(failure);
    return peak;
}
int main(int argc, char** argv) {
    std::cout << std::unitbuf;
    try {
        ComScope com;
        check(argc >= 2, "Usage: kj_vst3_probe <bundle.vst3> [effect|instrument] [--require-audio], or --self-test");
        kj::VST3Host host;
        if (std::string(argv[1]) == "--self-test") {
            bool rejected = false;
            try { host.load("Z:/__kj_nonexistent_plugin__.vst3", "invalid"); }
            catch (const std::exception&) { rejected = true; }
            check(rejected && !host.component(), "Invalid path not rejected cleanly");
            rejected = false;
            try { host.prepare(48000, 64); } catch (const std::exception&) { rejected = true; }
            check(rejected, "prepare accepted unloaded host");
            host.unload(); host.unload();
            std::cout << "PASS invalid path, unloaded-state guard, repeated unload\n";
            return 0;
        }
        const std::string expected = argc >= 3 ? argv[2] : "";
        check(expected.empty() || expected == "effect" || expected == "instrument", "Invalid expected category");
        const bool requireAudio = argc >= 4 && std::string(argv[3]) == "--require-audio";
        auto classes = kj::VST3Host::scan(argv[1]);
        std::cout << "Host SDK: 3.8.1; audio classes: " << classes.size() << '\n';
        for (const auto& info : classes) {
            const auto& categories = info.subCategories();
            const bool instrument = std::find(categories.begin(), categories.end(), "Instrument") != categories.end();
            check(expected.empty() || instrument == (expected == "instrument"), "Unexpected plugin category");
            std::cout << "CLASS " << info.name() << " | " << info.ID().toString() << " | "
                << info.subCategoriesString() << " | plugin SDK " << info.sdkVersion() << '\n';
            bool rejected = false;
            try { host.load(argv[1], "00000000000000000000000000000000"); }
            catch (const std::exception&) { rejected = true; }
            check(rejected && !host.component(), "Unknown class not rejected cleanly");
            for (int cycle = 0; cycle < 2; ++cycle) {
                host.load(argv[1], info.ID().toString());
                std::cout << "LOAD cycle=" << cycle + 1 << '\n';
                for (double rate : {44100., 48000.}) {
                    for (int block : {64, 512}) {
                        const auto peak = render(host, rate, block, instrument);
                        std::cout << "PROCESS rate=" << rate << " maxBlock=" << block << " peak=" << peak << '\n';
                        check(!requireAudio || peak > 1e-7, "Expected audible output, got silence");
                    }
                }
                host.unload();
                check(!host.component(), "Component retained after unload");
                std::cout << "UNLOAD cycle=" << cycle + 1 << '\n';
            }
        }
        std::cout << "PASS loading, processing, note delivery (instruments), unload/reload\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
