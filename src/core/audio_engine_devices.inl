std::vector<AudioOutputDevice> getAvailableAudioOutputDevices() {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool shouldUninitialize = SUCCEEDED(hr);
    if (hr == RPC_E_CHANGED_MODE) {
        shouldUninitialize = false;
    } else if (FAILED(hr)) {
        return {};
    }

    std::vector<AudioOutputDevice> result;
    auto devices = AudioDeviceHandler::enumerateRenderDevices();
    result.reserve(devices.size());
    for (auto& device : devices) {
        AudioOutputDevice info;
        info.id = std::move(device.id);
        info.name = std::move(device.name);
        result.push_back(std::move(info));
    }

    if (shouldUninitialize) {
        CoUninitialize();
    }
    return result;
}

AudioOutputDevice getActiveAudioOutputDevice() {
    const auto& snapshot = getDeviceSnapshot();
    AudioOutputDevice info;
    info.id = snapshot.activeId;
    info.name = snapshot.activeName;
    return info;
}

std::wstring getRequestedAudioOutputDeviceId() {
    std::lock_guard<std::mutex> lock(gDeviceStateMutex);
    return gRequestedDeviceId;
}

bool setActiveAudioOutputDevice(const std::wstring& deviceId) {
    std::lock_guard<std::mutex> lock(gDeviceStateMutex);
    // Compare with the latest request, not the last completed device change.
    // Selecting A -> B -> A before B opens must cancel the pending switch.
    if(deviceId==gRequestedDeviceId)return false;
    gRequestedDeviceId=deviceId;
    deviceChangeRequested.store(true,std::memory_order_release);
    return true;
}
