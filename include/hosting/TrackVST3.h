#pragma once
#include "core/tracks.h"
#include "core/mod_matrix.h"
#include <string>
#include <vector>

namespace kj {
using NativeFxProcess = void (*)(void*, const std::string&, int, double&, double&);
int trackVst3FrameOffset(int trackId) noexcept;
struct Vst3SlotInfo { std::string id, name, path, classId, error; bool instrument = false, bypass = false; };
// The audio thread services GUI edits between processing passes, even when
// transport is stopped. Its start/exit must bracket this registration.
void setTrackVst3AudioActive(bool active);
void serviceTrackVst3Changes() noexcept;
// GUI-thread slot edits are handed off to live audio; creation and disposal
// stay on the GUI thread. Whole-project capture/restore still require joined audio.
std::string addTrackVst3(int trackId, bool instrument, const std::string& path, const std::string& classId, double rate);
void removeTrackVst3(int trackId, const std::string& slotId);
void moveTrackVst3(int trackId, const std::string& slotId, int direction);
void bypassTrackVst3(int trackId, const std::string& slotId, bool bypass);
void locateTrackVst3(int trackId, const std::string& slotId, const std::string& path, double rate);
std::vector<Vst3SlotInfo> getTrackVst3Slots(int trackId);
struct Vst3ModTarget { std::string slotId, label; std::uint32_t parameterId; };
std::vector<Vst3ModTarget> getTrackVst3ModTargets(int trackId);
Vst3RackState captureTrackVst3(int trackId);
void restoreTrackVst3(int trackId, const Vst3RackState& state, double rate);
void openTrackVst3SlotEditor(int trackId, const std::string& slotId, void* owner);
// clear/prepare/restore require stopped/joined audio on the GUI thread.
void loadTrackVst3(int trackId, bool instrument, const std::string& path,
                  const std::string& classId, double sampleRate);
void unloadTrackVst3(int trackId, bool instrument);
void clearTrackVst3();
void prepareTrackVst3(double sampleRate);
bool hasTrackVst3();
// Audio-thread operations; registry edits only occur between processing passes.
// Render the whole chain exactly once per frame. instrument=true replaces input
// with instrument output; false processes supplied audio. One 64-frame buffer
// for the entire chain, independent of effect count.
void renderTrackVst3(int trackId, bool instrument, double rate, double position,
                     double tempo, bool reset, bool gate, bool notesChanged,
                     const std::vector<StepNoteInfo>& noteOns,
                     const std::vector<int>& notesPresent, double& left, double& right,
                     const std::vector<std::string>* order = nullptr,
                     NativeFxProcess nativeProcess = nullptr, void* nativeContext = nullptr,
                     const std::vector<ModMatrixAssignment>* routes = nullptr,
                     const double* sources = nullptr, size_t sourceCount = 0, bool transportPlaying = true) noexcept;
void stopTrackVst3Audio(int trackId = 0) noexcept;
std::string takeTrackVst3Error();
double takeTrackVst3RateRequest();
void openTrackVst3Editor(int trackId, bool instrument, void* owner);
void serviceTrackVst3Controllers();
}
