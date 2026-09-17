#pragma once
#include <array>
#include <memory>
#include <cstdint>
namespace spectrum {
// ~5.9 Hz bins at 48 kHz, so the bass region has useful resolution.
constexpr int size=8192;
class View {
public:
    View(int slot,int track,std::uint64_t start);
    ~View();
    View(const View&)=delete;
    int track;
    float inputDb=-120, outputDb=-120; // Stereo RMS of the same unwindowed audio frame.
    bool hasInput=false;
    // GUI thread only. Returns false when no new complete frame is available.
    bool read(std::array<float,size/2+1>& db);
private:
    int slot;
    std::uint64_t start,last;
};
std::shared_ptr<View> watch(int track); // GUI thread
void capture(int track,float left,float right,float inputLeft,float inputRight) noexcept;
void capture(int track,float left,float right) noexcept; // Audio thread, no allocation/locks
}
