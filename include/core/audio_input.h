#pragma once
#include "core/tracks.h"
#include <atomic>
#include <memory>
#include <vector>
#include <string>
#include <cstdint>
namespace input {
constexpr int capacity=16384, maxChannels=64;
// Single capture producer, independent readers per track; samples remain atomic during wrapping.
class Buffer {
    std::unique_ptr<std::atomic<float>[]> data;
    // Used only by the single playback thread. One timeline per device avoids comb
    // filtering when multiple tracks monitor different routes from the same input.
    friend class Reader;
    uint64_t renderBlock=~uint64_t(0),renderGeneration=0;
    double renderPosition=-1,renderNext=-1,renderIncrement=1,renderRate=0;
public:
    std::atomic<uint64_t> written{0},generation{0};
    std::atomic<int> channels{0},rate{0};
    std::atomic<bool> available{false};
    Buffer();
    void configure(int channelCount,int sampleRate);
    void push(const float* interleaved,int frames);
    float sample(uint64_t frame,int channel) const;
};
class Reader {
    std::shared_ptr<Buffer> source;
    uint64_t generation=0;
    double position=-1,increment=1,cutoff=.96;
public:
    bool bind(std::shared_ptr<Buffer> buffer);
    void beginBlock(double outputRate,int frames=0,uint64_t block=0);
    void read(int firstChannel,bool stereo,double& left,double& right);
    bool ready(int first,bool stereo) const;
    void reset(){position=-1;}
};
struct Device { std::string id,name;int channels=0; };
std::vector<Device> devices();
// Cache/UI thread only; may create/retire capture workers, never called by the audio callback.
std::vector<std::shared_ptr<Buffer>> synchronize(const std::vector<Track>& tracks);
void shutdown();
std::string status(const std::string& deviceId);
void setMonitorWhileStopped(bool enabled);
bool monitorWhileStopped();
}
