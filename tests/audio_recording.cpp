#include "core/audio_recording.h"
#include <fstream>
#include <vector>
#include <iostream>
#include <stdexcept>
void check(bool ok, const char* text) { if (!ok) throw std::runtime_error(text); }
int main(int argc, char** argv) {
    std::wstring error;
    try {
        check(argc > 1, "Missing output path");
        for (unsigned hz : {44100u, 48000u, 96000u}) {
            check(startAudioRecording(argv[1], error), "Start failed");
            check(isAudioRecording(), "Missing active status");
            check(!startAudioRecording(argv[1], error), "Duplicate start accepted");
            for (int i = 0; i < 1000; ++i) captureRecordingFrame(0.5, -0.5, hz);
            check(stopAudioRecording(error), "Stop failed");
            check(!isAudioRecording(), "Active status not reset");
            std::ifstream file(argv[1], std::ios::binary);
            std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(file)), {});
            auto u32 = [&](size_t p) { return unsigned(bytes[p]) | unsigned(bytes[p+1]) << 8 |
                unsigned(bytes[p+2]) << 16 | unsigned(bytes[p+3]) << 24; };
            check(bytes.size() == 4044, "Missing frames");
            check(u32(4) == 4036 && u32(40) == 4000, "Invalid WAV sizes");
            check(u32(24) == hz && bytes[22] == 2 && bytes[34] == 16, "Invalid WAV format");
            check(bytes[44] == 0 && bytes[45] == 64 && bytes[46] == 0 && bytes[47] == 192,
                "Incorrect stereo samples");
        }
        check(startAudioRecording(argv[1], error), "Empty start failed");
        check(stopAudioRecording(error), "Empty stop failed");
        check(std::filesystem::file_size(argv[1]) == 44, "Empty WAV invalid");
        check(startAudioRecording(argv[1], error), "Rate-change start failed");
        captureRecordingFrame(0, 0, 48000);
        captureRecordingFrame(0, 0, 44100);
        check(!stopAudioRecording(error) && !error.empty(), "Rate change not reported");
        check(std::filesystem::file_size(argv[1]) == 48, "Partial WAV not finalized");
        std::cout << "Recording tests passed\n";
        return 0;
    } catch (const std::exception& exception) {
        stopAudioRecording(error);
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
