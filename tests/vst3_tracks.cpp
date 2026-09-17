#include "hosting/TrackVST3.h"
#include "hosting/VST3Host.h"
#include <windows.h>
#include <objbase.h>
#include <atomic>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <thread>

void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
int main(int argc, char** argv) {
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 1;
    int result = 0;
    try {
        check(argc == 3, "Usage: kj_vst3_track_tests <instrument.vst3> <effect.vst3>");
        const auto synth = kj::VST3Host::scan(argv[1]).front();
        const auto effect = kj::VST3Host::scan(argv[2]).front();
        for (double rate : {44100., 48000.}) {
            kj::loadTrackVst3(1, true, argv[1], synth.ID().toString(), rate);
            kj::loadTrackVst3(1, false, argv[2], effect.ID().toString(), rate);
            kj::loadTrackVst3(2, false, argv[2], effect.ID().toString(), rate);
            const double otherRate = rate == 44100 ? 48000 : 44100;
            std::thread rateChange([&] {
                double left = 1, right = 1;
                const std::vector<StepNoteInfo> noOns;
                const std::vector<int> noNotes;
                kj::renderTrackVst3(1, true, otherRate, 0, 120, false, false, false, noOns, noNotes, left, right);
            });
            rateChange.join();
            check(kj::takeTrackVst3RateRequest() == otherRate, "Device rate mismatch did not request reconfiguration");
            check(kj::takeTrackVst3Error().empty(), "Device rate mismatch was treated as fatal");
            kj::prepareTrackVst3(otherRate);
            kj::prepareTrackVst3(rate);
            for (int run = 0; run < 2; ++run) {
                std::atomic<bool> done {false};
                std::exception_ptr failure;
                std::thread audio([&] {
                    try {
                        double instrumentPeak = 0, effectPeak = 0;
                        const std::vector<StepNoteInfo> note {{60, 0.8f, false}};
                        const std::vector<StepNoteInfo> noOns;
                        const std::vector<int> present {60}, noNotes;
                        for (int i = 0; i < 24000; ++i) {
                            bool gate = i < 16000;
                            double left = 0, right = 0;
                            kj::renderTrackVst3(1, true, rate, i, 120, i == 0, gate,
                                i == 17 || i == 16000, i == 17 ? note : noOns, gate ? present : noNotes, left, right);
                            if (i < 64) check(left == 0 && right == 0, "Instrument buffer latency violated");
                            if (i < 64)
                                check(left == 0 && right == 0, "Chained buffer latency violated");
                            check(std::isfinite(left) && std::isfinite(right), "Non-finite instrument chain");
                            instrumentPeak = std::max(instrumentPeak, std::abs(left));
                            left = right = 0.1 * std::sin(6.283185307 * 220 * i / rate);
                            kj::renderTrackVst3(2, false, rate, i, 120, false, false, false, noOns, noNotes, left, right);
                            effectPeak = std::max(effectPeak, std::abs(left));
                        }
                        check(instrumentPeak > 1e-6, "Instrument/effect chain is silent");
                        check(effectPeak > 1e-6, "Effect track is silent");
                        check(kj::takeTrackVst3Error().empty(), "Track adapter reported processing failure");
                        kj::stopTrackVst3Audio();
                    } catch (...) { kj::stopTrackVst3Audio(); failure = std::current_exception(); }
                    done = true;
                });
                while (!done.load()) {
                    MSG message;
                    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                        TranslateMessage(&message); DispatchMessageW(&message);
                    }
                    Sleep(1);
                }
                audio.join();
                if (failure) std::rethrow_exception(failure);
                if (run == 0) {
                    auto saved = kj::captureTrackVst3(1);
                    check(saved.size() == 2 && saved[0].state.hasComponent, "Instrument state capture failed");
                    kj::restoreTrackVst3(1, saved, rate);
                    for (const auto& slot : kj::getTrackVst3Slots(1)) check(slot.error.empty(), slot.error.c_str());
                }
                std::cout << "PASS track chain, separate effect, 64-sample buffering, stop/restart: " << rate << " run=" << run + 1 << '\n';
            }
            kj::clearTrackVst3();
            check(!kj::hasTrackVst3(), "Track plugins retained after clear");
        }
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; result = 1; }
    kj::clearTrackVst3();
    CoUninitialize();
    return result;
}
