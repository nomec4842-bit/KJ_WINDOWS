#pragma once
#include "core/tracks.h"
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

namespace piano {
constexpr int ticksPerStep = 24;
constexpr int ticksPerBeat = 96;
struct Note {
    int start = 0, length = 24, pitch = 60;
    float velocity = 0.8f, probability = 1.0f;
    bool locked = false;
};
struct Pattern { int length = 384; std::vector<Note> notes; };
enum class Algorithm { Scramble, Chaos, Probability, Euclidean, RandomWalk, Mutate };
struct Settings {
    Algorithm algorithm = Algorithm::Scramble;
    bool continuous = false, evolve = false;
    int every = 1, key = 0, scale = 0, hits = 4;
    float amount = 0.5f;
    uint32_t seed = 1;
};
struct Document { Pattern pattern, original; Settings settings; bool hasOriginal = false; };
Pattern generate(const Pattern& source, const Settings& settings, uint64_t iteration, bool drums, int bankSize);
void normalize(Pattern& pattern);
Document document(int track);
void commit(int track, Document value);
bool undo(int track, bool redo = false);
void reset();
void generateOnce(int track);
void freeze(int track);
void restoreOriginal(int track);
std::string serialize(int track);
void deserialize(int track, const std::string& data);

struct Prepared { Pattern pattern; uint64_t generation = 0; };
struct Runtime {
    Settings settings;
    std::atomic<uint64_t> cycle{0};
    std::shared_ptr<const Prepared> current, next;
    std::shared_ptr<const Pattern> displayed;
};
// Cache-thread preparation keeps generation and allocations out of the sample loop.
std::shared_ptr<Runtime> prepare(int track);
struct Playback {
    std::shared_ptr<Runtime> runtime;
    std::shared_ptr<const Prepared> plan;
    int lastTick = -1;
    uint64_t cycle = 0;
    bool gate = false;
    std::vector<StepNoteInfo> notes;
};
bool render(Playback& state, const std::shared_ptr<Runtime>& runtime, int tick, bool restart,
            std::vector<StepNoteInfo>& on, std::vector<int>& present, bool& gate);
Pattern displayed(int track);
}
