#pragma once

#include <atomic>
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

extern std::atomic<bool> isPlaying;
void initAudio(bool loadDefaultSample = true);
double getAudioSampleRate();
bool isAudioRunning();
// True for one second after the final stereo mix exceeds full scale.
bool isAudioOutputClipping();
void shutdownAudio();
bool loadSampleFile(int trackId, const std::filesystem::path& path);

struct AudioThreadNotification
{
    std::wstring title;
    std::wstring message;
};

bool consumeAudioThreadNotification(AudioThreadNotification& notification);

struct AudioOutputDevice {
    std::wstring id;
    std::wstring name;
};

std::vector<AudioOutputDevice> getAvailableAudioOutputDevices();
AudioOutputDevice getActiveAudioOutputDevice();
std::wstring getRequestedAudioOutputDeviceId();
bool setActiveAudioOutputDevice(const std::wstring& deviceId);

// Returns the most recent samples from the master output. The number of
// samples returned will not exceed the internal capture buffer size.
std::vector<float> getMasterWaveformSnapshot(std::size_t sampleCount);
std::size_t getMasterWaveformCapacity();
