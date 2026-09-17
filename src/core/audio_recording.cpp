#include "core/audio_recording.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <thread>

namespace {
constexpr size_t capacity = 262144;
struct Frame { int16_t left, right; };
std::array<Frame, capacity> frames{};
std::atomic<uint64_t> written{0}, consumed{0};
std::atomic<unsigned> rate{0};
std::atomic<bool> accepting{false}, finished{false};
std::atomic<int> failure{0};
std::atomic_flag producer = ATOMIC_FLAG_INIT;
std::thread writer;
std::ofstream output;
bool active = false; // UI thread owns start/stop/status.

void number(uint32_t value, int bytes) {
    for (int i = 0; i < bytes; ++i) output.put(static_cast<char>((value >> (8 * i)) & 255));
}
void header(uint32_t bytes, unsigned hz) {
    output.seekp(0);
    output.write("RIFF", 4); number(36 + bytes, 4);
    output.write("WAVEfmt ", 8); number(16, 4); number(1, 2); number(2, 2);
    number(hz, 4); number(hz * 4, 4); number(4, 2); number(16, 2);
    output.write("data", 4); number(bytes, 4);
}
void writeRecording() {
    uint32_t bytes = 0;
    while (true) {
        uint64_t read = consumed.load(std::memory_order_relaxed);
        uint64_t end = written.load(std::memory_order_acquire);
        if (read == end) {
            if (finished.load(std::memory_order_acquire) &&
                read == written.load(std::memory_order_acquire)) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        size_t count = std::min<size_t>(end - read, capacity - read % capacity);
        if (count * sizeof(Frame) > UINT32_MAX - 36u - bytes) {
            failure.store(3); accepting.store(false); break;
        }
        output.write(reinterpret_cast<const char*>(&frames[read % capacity]), count * sizeof(Frame));
        if (!output) { failure.store(2); accepting.store(false); break; }
        bytes += static_cast<uint32_t>(count * sizeof(Frame));
        consumed.store(read + count, std::memory_order_release);
    }
    output.clear();
    header(bytes, rate.load() ? rate.load() : 48000);
    output.flush();
    if (!output) failure.store(2);
}
}

bool isAudioRecording() { return active; }

bool startAudioRecording(const std::filesystem::path& path, std::wstring& error) {
    error.clear();
    if (active) { error = L"Recording is already in progress."; return false; }
    output.open(path, std::ios::binary | std::ios::trunc);
    if (!output) { output.clear(); error = L"Could not create the recording file."; return false; }
    header(0, 48000);
    if (!output) { output.close(); error = L"Could not write the WAV header."; return false; }
    written.store(0); consumed.store(0); rate.store(0); failure.store(0); finished.store(false);
    try { writer = std::thread(writeRecording); }
    catch (...) { output.close(); error = L"Could not start the recording writer."; return false; }
    active = true;
    accepting.store(true, std::memory_order_release);
    return true;
}

bool stopAudioRecording(std::wstring& error) {
    error.clear();
    if (!active) return true;
    accepting.store(false, std::memory_order_release);
    while (producer.test_and_set(std::memory_order_acquire)) std::this_thread::yield();
    producer.clear(std::memory_order_release);
    finished.store(true, std::memory_order_release);
    writer.join();
    output.close();
    active = false;
    switch (failure.load()) {
    case 1: error = L"Recording stopped because the disk writer could not keep up. The captured portion was saved."; break;
    case 2: error = L"Writing the recording failed. Check disk space and the destination file."; break;
    case 3: error = L"Recording reached the WAV size limit. The captured portion was saved."; break;
    case 4: error = L"The audio sample rate changed during recording. The captured portion was saved."; break;
    }
    return error.empty();
}

void captureRecordingFrame(double left, double right, unsigned sampleRate) {
    if (!accepting.load(std::memory_order_acquire)) return;
    if (producer.test_and_set(std::memory_order_acquire)) return;
    if (accepting.load(std::memory_order_acquire)) {
        unsigned hz = rate.load(std::memory_order_relaxed);
        if (!hz) rate.store(sampleRate);
        uint64_t end = written.load(std::memory_order_relaxed);
        if (!sampleRate || (hz && hz != sampleRate)) { failure.store(4); accepting.store(false); }
        else if (end - consumed.load(std::memory_order_acquire) >= capacity) { failure.store(1); accepting.store(false); }
        else {
            auto pcm = [](double value) { return static_cast<int16_t>(std::lround(std::clamp(std::isfinite(value) ? value : 0.0, -1.0, 1.0) * 32767.0)); };
            frames[end % capacity] = {pcm(left), pcm(right)};
            written.store(end + 1, std::memory_order_release);
        }
    }
    producer.clear(std::memory_order_release);
}
