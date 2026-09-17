#include "hosting/TrackVST3.h"
#include "hosting/VST3Host.h"
#include "core/audio_engine.h"
#include "core/sequencer.h"
#include "core/tracks.h"
#include <windows.h>
#include <objbase.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>

void pump() {
    MSG message;
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message); DispatchMessageW(&message);
    }
    Sleep(10);
}
int main(int argc, char** argv) {
    if (argc < 2 || FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 1;
    std::cout << std::unitbuf;
    int result = 0;
    try {
        initTracks(); initSequencer(); initAudio(false);
        for (int i = 0; i < 200; ++i) pump();
        const auto rate = getAudioSampleRate();
        std::wcout << L"Device: " << getActiveAudioOutputDevice().name << L"; rate=" << rate << L'\n';
        const auto id = getTracks().front().id;
        shutdownAudio();
        auto info = kj::VST3Host::scan(argv[1]).front();
        // Deliberately exercise startup with a stale rate, as well as normal restart.
        kj::loadTrackVst3(id, true, argv[1], info.ID().toString(), rate == 44100 ? 48000 : 44100);
        trackSetType(id, TrackType::Vst3);
        trackSetStepState(id, 0, true); trackSetStepNote(id, 0, 60);
        trackSetVolume(id, 0.2f);
        setActiveSequencerTrackId(id);
        initAudio(false); requestSequencerReset(); isPlaying = true;
        double peak = 0;
        bool renegotiated = false;
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (std::chrono::steady_clock::now() < end) {
            pump();
            AudioThreadNotification notification;
            while (consumeAudioThreadNotification(notification))
                std::wcerr << notification.title << L": " << notification.message << L'\n';
            if (const auto newRate = kj::takeTrackVst3RateRequest(); newRate > 0) {
                shutdownAudio(); kj::prepareTrackVst3(newRate); initAudio(false);
                requestSequencerReset(); isPlaying = true; renegotiated = true;
                std::cout << "Reconfigured plugin to device rate " << newRate << '\n';
            }
            if (auto error = kj::takeTrackVst3Error(); !error.empty()) throw std::runtime_error(error);
            for (float value : getMasterWaveformSnapshot(512)) peak = std::max(peak, std::abs(double(value)));
        }
        if (!renegotiated) {
            std::cout << "playing=" << isPlaying.load() << " step=" << sequencerCurrentStep.load() << " peak=" << peak << '\n';
            throw std::runtime_error("Rate recovery was not exercised");
        }
        if (peak < 1e-6) throw std::runtime_error("Sequencer produced no audio");
        std::cout << "PASS actual sequencer/device playback, rate recovery; peak=" << peak << '\n';
    } catch (const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; result = 1; }
    shutdownAudio(); kj::clearTrackVst3(); CoUninitialize();
    return result;
}
