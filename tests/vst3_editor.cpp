#include "hosting/VST3Host.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "public.sdk/source/vst/hosting/processdata.h"
#include "public.sdk/source/vst/hosting/eventlist.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include <windows.h>
#include <objbase.h>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <cmath>

void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
int main(int argc, char** argv) {
    if (argc < 2 || FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 1;
    int result = 0;
    try {
        kj::VST3Host host;
        const auto info = kj::VST3Host::scan(argv[1]).front();
        host.load(argv[1], info.ID().toString()); host.prepare(48000, 64);
        for (int run = 0; run < 3; ++run) {
            HWND window = static_cast<HWND>(host.openEditor(nullptr, false));
            check(IsWindow(window) != 0, "Missing native editor window");
            for (int i = 0; i < 30; ++i) {
                MSG message;
                while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                    TranslateMessage(&message); DispatchMessageW(&message);
                }
                host.serviceController(); Sleep(10);
            }
            check(GetWindow(window, GW_CHILD) != nullptr, "Plugin editor did not create a child window");
            RECT size {}; GetClientRect(window, &size);
            check(size.right > 100 && size.bottom > 100, "Invalid editor dimensions");
            if (run == 0) SendMessageW(window, WM_CLOSE, 0, 0);
            else if (run == 1) host.closeEditor();
            else host.unload();
            check(!IsWindow(window), "Plugin editor survived close/unload");
        }
        std::cout << "PASS editor attach, close, reopen, unload with editor open\n";
        if (argc > 2) {
            using namespace Steinberg;
            using namespace Steinberg::Vst;
            const auto effect = kj::VST3Host::scan(argv[2]).front();
            host.load(argv[2], effect.ID().toString()); host.prepare(48000, 64);
            bool found = false;
            for (int32 i = 0; i < host.controller()->getParameterCount(); ++i) {
                ParameterInfo info {};
                host.controller()->getParameterInfo(i, info);
                if (info.flags & ParameterInfo::kIsBypass) {
                    host.setParameter(info.id, 1.0); found = true; break;
                }
            }
            check(found, "No bypass parameter available for editor-to-audio test");
            HostProcessData data;
            check(data.prepare(*host.component(), 64, kSample32), "Cannot allocate parameter test buffers");
            data.numSamples = 64; data.processMode = kRealtime;
            ProcessContext context {}; context.sampleRate = 48000; context.tempo = 120;
            data.processContext = &context;
            EventList inputEvents, outputEvents;
            data.inputEvents = &inputEvents; data.outputEvents = &outputEvents;
            std::exception_ptr failure;
            std::thread audio([&] {
                try {
                    host.startProcessing();
                    for (int block = 0; block < 64; ++block) {
                        for (int bus = 0; bus < data.numInputs; ++bus) {
                            data.inputs[bus].silenceFlags = 0;
                            for (int ch = 0; ch < data.inputs[bus].numChannels; ++ch)
                                for (int i = 0; i < 64; ++i) data.inputs[bus].channelBuffers32[ch][i] =
                                    static_cast<float>(0.1 * std::sin((block * 64 + i) * 0.031));
                        }
                        host.process(data);
                        check(data.inputParameterChanges == nullptr, "Host did not restore caller's parameter pointer");
                        if (block == 63) for (int i = 0; i < 64; ++i)
                            check(std::abs(data.outputs[0].channelBuffers32[0][i] - data.inputs[0].channelBuffers32[0][i]) < 1e-5,
                                  "Editor bypass edit did not reach the processor");
                    }
                    host.stopProcessing();
                } catch (...) { host.stopProcessing(); failure = std::current_exception(); }
            });
            audio.join();
            if (failure) std::rethrow_exception(failure);
            std::cout << "PASS editor parameter transfer to audio processor (effect bypass)\n";
        }
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; result = 1; }
    CoUninitialize(); return result;
}
