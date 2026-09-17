#include "core/audio_device_handler.h"
#include <iostream>
#include <stdexcept>

// Seed the result of an asynchronous open without requiring audio hardware.
struct AudioDeviceInitializationTest {
    static void completed(AudioDeviceHandler& handler, const std::wstring& requested) {
        handler.initialized_ = true;
        handler.requestedDeviceId_ = requested;
        handler.deviceId_ = L"resolved-speaker-endpoint";
        handler.initThreadActive_ = true;
        handler.initCompleted_ = true;
        handler.initSuccess_ = true;
    }
};
int main() {
    try {
        for (const auto& request : {std::wstring{}, std::wstring{L"resolved-speaker-endpoint"}}) {
            AudioDeviceHandler handler;
            AudioDeviceInitializationTest::completed(handler, request);
            if (!handler.initialize(request) || handler.isInitializing())
                throw std::runtime_error("Completed device was reopened instead of becoming ready");
            if (!handler.initialize(request) || handler.deviceId() != L"resolved-speaker-endpoint")
                throw std::runtime_error("Repeated initialization lost the resolved endpoint");
            handler.shutdown();
            if (handler.isInitialized() || !handler.deviceId().empty())
                throw std::runtime_error("Shutdown retained device state");
        }
        std::cout << "PASS default and explicit asynchronous device completion and reuse\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n';return 1; }
    return 0;
}
