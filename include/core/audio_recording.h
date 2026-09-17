#pragma once
#include <filesystem>
#include <string>

bool startAudioRecording(const std::filesystem::path& path, std::wstring& error);
bool stopAudioRecording(std::wstring& error);
bool isAudioRecording();
// Called only by the audio render thread; never writes to disk or waits.
void captureRecordingFrame(double left, double right, unsigned sampleRate);
