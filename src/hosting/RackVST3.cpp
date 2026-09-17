#include "hosting/TrackVST3.h"
#include "hosting/VST3Host.h"
#include "public.sdk/source/vst/hosting/processdata.h"
#include "public.sdk/source/vst/hosting/eventlist.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include <windows.h>
#include <objbase.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <map>
#include <mutex>
#include <functional>
#include <exception>
#include <set>
#include <stdexcept>

namespace kj {
using namespace Steinberg;
using namespace Steinberg::Vst;
namespace {
constexpr int blockSize = 64;
using Audio = std::array<std::array<float, blockSize>, 2>;
struct Slot {
    Vst3SlotState saved;
    std::string error;
    std::unique_ptr<VST3Host> host;
    HostProcessData data;
    EventList inputEvents {512}, outputEvents {512};
    ParameterChanges inputParameters, outputParameters;
    ProcessContext context {};
    std::array<bool, 128> notes {};
    bool processing = false, failed = false;
    int outputBus = -1, inputBus = -1, eventBus = -1;
    void prepare(double rate) {
        if (!host) return;
        inputBus = outputBus = eventBus = -1;
        host->prepare(rate, blockSize);
        if (!data.prepare(*host->component(), blockSize, kSample32)) throw std::runtime_error("VST3 buffer allocation failed");
        for (auto direction : {kInput, kOutput}) for (int i = 0; i < host->component()->getBusCount(kAudio, direction); ++i) {
            BusInfo bus {}; host->component()->getBusInfo(kAudio, direction, i, bus);
            if (bus.busType == kMain && bus.channelCount > 0) {
                if (bus.channelCount > 2) throw std::runtime_error("Only mono/stereo main buses are supported");
                (direction == kInput ? inputBus : outputBus) = i; break;
            }
        }
        for (int i = 0; i < host->component()->getBusCount(kEvent, kInput); ++i) {
            BusInfo bus {}; host->component()->getBusInfo(kEvent, kInput, i, bus);
            if (bus.busType == kMain) { eventBus = i; break; }
        }
        if (outputBus < 0) throw std::runtime_error("Plugin has no main audio output");
        if (saved.instrument && eventBus < 0) throw std::runtime_error("Instrument has no main note input");
        data.numSamples = blockSize; data.processMode = kRealtime;
        data.inputEvents = &inputEvents; data.outputEvents = &outputEvents;
        data.inputParameterChanges = &inputParameters; data.outputParameterChanges = &outputParameters;
        data.processContext = &context;
        context = {}; context.sampleRate = rate; context.timeSigNumerator = 4; context.timeSigDenominator = 4;
        notes = {}; failed = false; inputEvents.clear(); outputEvents.clear();
    }
    void note(int pitch, float velocity, bool on, int offset) {
        if (eventBus < 0 || !host || saved.bypass) return;
        Event event {}; event.busIndex = eventBus; event.sampleOffset = offset;
        event.ppqPosition = context.projectTimeMusic + offset * context.tempo / (60 * context.sampleRate);
        if (on) {
            event.type = Event::kNoteOnEvent; event.noteOn.pitch = static_cast<int16>(pitch);
            event.noteOn.velocity = velocity; event.noteOn.noteId = pitch;
        } else {
            event.type = Event::kNoteOffEvent; event.noteOff.pitch = static_cast<int16>(pitch); event.noteOff.noteId = pitch;
        }
        if (inputEvents.addEvent(event) != kResultOk) throw std::runtime_error("VST3 note queue overflow");
        notes[pitch] = on;
    }
    void process(Audio& audio) {
        if (!host || saved.bypass || failed) { if (saved.instrument) audio = {}; return; }
        if (!processing) { host->startProcessing(); processing = true; }
        for (int bus = 0; bus < data.numInputs; ++bus) {
            auto& input = data.inputs[bus];
            input.silenceFlags = saved.instrument || bus != inputBus ? HostProcessData::kAllChannelsSilent : 0;
            for (int ch = 0; ch < input.numChannels; ++ch) for (int i = 0; i < blockSize; ++i)
                input.channelBuffers32[ch][i] = saved.instrument || bus != inputBus ? 0.f :
                    (input.numChannels == 1 ? (audio[0][i] + audio[1][i]) * 0.5f : audio[ch][i]);
        }
        for (int bus = 0; bus < data.numOutputs; ++bus) {
            auto& output = data.outputs[bus]; output.silenceFlags = 0;
            for (int ch = 0; ch < output.numChannels; ++ch) std::fill_n(output.channelBuffers32[ch], blockSize, 0.f);
        }
        host->process(data);
        const auto& output = data.outputs[outputBus]; Audio result {};
        for (int ch = 0; ch < 2; ++ch) {
            const int source = std::min(ch, output.numChannels - 1);
            for (int i = 0; i < blockSize; ++i) {
                const float value = output.silenceFlags & (uint64(1) << source) ? 0 : output.channelBuffers32[source][i];
                if (!std::isfinite(value)) throw std::runtime_error("Non-finite audio output");
                result[ch][i] = value;
            }
        }
        audio = result; inputEvents.clear(); outputEvents.clear(); outputParameters.clearQueue();
    }
};
struct Rack {
    Rack() { slots.reserve(257); }
    std::vector<std::unique_ptr<Slot>> slots;
    Audio input {}, output {};
    int cursor = 0;
    double rate = 44100;
    void reset() { input = {}; output = {}; cursor = 0; }
};
std::map<int, Rack> racks;
// Only the GUI submits edits. Prepared objects and retired objects belong to
// that thread; the audio thread only commits a short graph edit between passes.
struct RackEdit {
    std::function<void()> apply;
    std::exception_ptr failure;
    std::atomic<bool> done {false};
};
std::atomic<RackEdit*> pendingEdit {nullptr};
std::mutex editLifecycle;
bool audioActive = false; // Protected by editLifecycle; never locked in render.
void applyPendingEdit() noexcept {
    if (auto* edit = pendingEdit.exchange(nullptr, std::memory_order_acquire)) {
        try { edit->apply(); } catch (...) { edit->failure = std::current_exception(); }
        edit->done.store(true, std::memory_order_release);
    }
}
void editRack(std::function<void()> apply) {
    RackEdit edit {std::move(apply)};
    {
        std::lock_guard<std::mutex> lock(editLifecycle);
        if (!audioActive) { edit.apply(); return; }
        pendingEdit.store(&edit, std::memory_order_release);
    }
    while (!edit.done.load(std::memory_order_acquire)) Sleep(1);
    if (edit.failure) std::rethrow_exception(edit.failure);
}
void stopSlot(Slot& s) {
    if (s.host && s.processing) {
        s.inputEvents.clear();
        for (int pitch = 0; pitch < 128; ++pitch) if (s.notes[pitch]) s.note(pitch, 0, false, 0);
        s.context.state &= ~ProcessContext::kPlaying;
        try { Audio silence {}; if (!s.failed) s.process(silence); }
        catch (...) { s.failed = true; }
        s.host->stopProcessing(); s.processing = false;
    }
    s.notes = {}; s.inputEvents.clear();
}

struct ErrorQueue {
    std::array<std::array<char, 512>, 8> messages {};
    std::atomic<unsigned> read {0}, write {0};
    void push(int id, const Slot& slot, const char* message) noexcept {
        const unsigned w = write.load(std::memory_order_relaxed), next = (w + 1) % messages.size();
        if (next == read.load(std::memory_order_acquire)) return;
        std::snprintf(messages[w].data(), messages[w].size(), "Track %d / %s: %s", id, slot.saved.name.c_str(), message);
        write.store(next, std::memory_order_release);
    }
    std::string pop() {
        const unsigned r = read.load(std::memory_order_relaxed);
        if (r == write.load(std::memory_order_acquire)) return {};
        std::string message(messages[r].data()); read.store((r + 1) % messages.size(), std::memory_order_release); return message;
    }
} errors;
std::atomic<double> requestedRate {0};
std::string newId() {
    GUID id; if (FAILED(CoCreateGuid(&id))) throw std::runtime_error("Cannot allocate rack slot ID");
    wchar_t text[40]; StringFromGUID2(id, text, 40);
    std::wstring wide(text); return std::string(wide.begin(), wide.end());
}
std::unique_ptr<Slot> makeSlot(Vst3SlotState saved, double rate, bool restore) {
    auto slot = std::make_unique<Slot>(); slot->saved = std::move(saved);
    auto classes = VST3Host::scan(slot->saved.path);
    auto found = std::find_if(classes.begin(), classes.end(), [&](const auto& c) { return c.ID().toString() == slot->saved.classId; });
    if (found == classes.end()) throw std::runtime_error("Saved plugin class is not present in this module");
    const auto& cats = found->subCategories();
    if ((std::find(cats.begin(), cats.end(), "Instrument") != cats.end()) != slot->saved.instrument)
        throw std::runtime_error("Plugin category does not match rack slot");
    slot->saved.name = found->name(); slot->host = std::make_unique<VST3Host>();
    slot->host->load(slot->saved.path, slot->saved.classId);
    if (restore) slot->host->restoreState(slot->saved.state);
    slot->prepare(rate); return slot;
}
Slot& findSlot(int id, const std::string& slotId) {
    auto rack = racks.find(id);
    if (rack != racks.end()) for (auto& slot : rack->second.slots) if (slot->saved.id == slotId) return *slot;
    throw std::runtime_error("Rack slot no longer exists");
}
}
void setTrackVst3AudioActive(bool active) {
    std::lock_guard<std::mutex> lock(editLifecycle);
    if (!active) applyPendingEdit();
    audioActive = active;
}
void serviceTrackVst3Changes() noexcept { applyPendingEdit(); }
std::string addTrackVst3(int id, bool instrument, const std::string& path, const std::string& classId, double rate) {
    Vst3SlotState saved; saved.id = newId(); saved.path = path; saved.classId = classId; saved.instrument = instrument;
    auto slot = makeSlot(saved, rate, false);
    std::map<int, Rack> prepared;
    if (!racks.count(id)) { prepared.try_emplace(id); prepared.at(id).rate = rate; }
    auto node = prepared.empty() ? decltype(prepared)::node_type{} : prepared.extract(id);
    std::unique_ptr<Slot> retired;
    editRack([&] {
        if (!node.empty()) racks.insert(std::move(node));
        auto& rack = racks.at(id);
        if (rack.rate != rate && !rack.slots.empty()) throw std::runtime_error("Audio device rate changed; retry loading the plugin");
        if (rack.slots.empty()) { rack.rate = rate; rack.reset(); }
        auto old = std::find_if(rack.slots.begin(), rack.slots.end(), [](const auto& s) { return s->saved.instrument; });
        if (instrument && old != rack.slots.end()) {
            stopSlot(**old); retired = std::move(*old); *old = std::move(slot);
        } else {
            if (rack.slots.size() >= 256) throw std::runtime_error("A rack supports up to 256 plugin slots");
            if (instrument) rack.slots.insert(rack.slots.begin(), std::move(slot));
            else rack.slots.push_back(std::move(slot));
        }
    });
    return saved.id;
}
void loadTrackVst3(int id, bool instrument, const std::string& path, const std::string& classId, double rate) { addTrackVst3(id, instrument, path, classId, rate); }
void removeTrackVst3(int id, const std::string& slotId) {
    std::unique_ptr<Slot> retired;
    editRack([&] {
        auto& slot = findSlot(id, slotId); stopSlot(slot);
        auto& list = racks.at(id).slots;
        auto it = std::find_if(list.begin(), list.end(), [&](const auto& s) { return s.get() == &slot; });
        retired = std::move(*it); list.erase(it);
    });
}
void moveTrackVst3(int id, const std::string& slotId, int direction) {
    editRack([&] {
        auto& s = findSlot(id, slotId);
        if (s.saved.instrument || (direction != -1 && direction != 1)) return;
        auto& list = racks.at(id).slots;
        auto it = std::find_if(list.begin(), list.end(), [&](const auto& p) { return p.get() == &s; });
        auto index = static_cast<int>(it - list.begin()), target = index + direction;
        if (target >= 0 && target < static_cast<int>(list.size()) && !list[target]->saved.instrument) std::swap(list[index], list[target]);
    });
}
void bypassTrackVst3(int id, const std::string& slotId, bool bypass) {
    editRack([&] { auto& slot = findSlot(id, slotId); if (bypass) stopSlot(slot); slot.saved.bypass = bypass; });
}
void locateTrackVst3(int id, const std::string& slotId, const std::string& path, double rate) {
    auto& old = findSlot(id, slotId); auto saved = old.saved;
    // The old slot alone passes through (or is silent for an instrument) while
    // its saved state is captured and its replacement is prepared.
    bypassTrackVst3(id, slotId, true);
    try {
        if (old.host) saved.state = old.host->captureState();
        saved.path = path; auto replacement = makeSlot(saved, rate, true);
        std::unique_ptr<Slot> retired;
        editRack([&] {
            if (racks.at(id).rate != rate) throw std::runtime_error("Audio device rate changed; retry locating the plugin");
            for (auto& slot : racks.at(id).slots) if (slot.get() == &old) { retired = std::move(slot); slot = std::move(replacement); break; }
        });
    } catch (...) { bypassTrackVst3(id, slotId, saved.bypass); throw; }
}
std::vector<Vst3SlotInfo> getTrackVst3Slots(int id) {
    std::vector<Vst3SlotInfo> result; auto rack = racks.find(id); if (rack == racks.end()) return result;
    for (const auto& s : rack->second.slots) result.push_back({s->saved.id, s->saved.name, s->saved.path, s->saved.classId, s->error, s->saved.instrument, s->saved.bypass});
    return result;
}
Vst3RackState captureTrackVst3(int id) {
    serviceTrackVst3Controllers();
    Vst3RackState state; auto rack = racks.find(id); if (rack == racks.end()) return state;
    for (auto& slot : rack->second.slots) { auto saved = slot->saved; if (slot->host) saved.state = slot->host->captureState(); state.push_back(std::move(saved)); }
    return state;
}
void restoreTrackVst3(int id, const Vst3RackState& state, double rate) {
    Rack rack; rack.rate = rate; std::set<std::string> ids; bool instrumentSeen = false;
    for (auto saved : state) {
        if (saved.id.empty() || !ids.insert(saved.id).second) { saved.id = newId(); ids.insert(saved.id); }
        if (saved.instrument && instrumentSeen) throw std::runtime_error("Multiple instruments in one rack");
        instrumentSeen |= saved.instrument;
        std::unique_ptr<Slot> slot;
        try { slot = makeSlot(saved, rate, true); }
        catch (const std::exception& error) { slot = std::make_unique<Slot>(); slot->saved = saved; slot->error = error.what(); }
        if (saved.instrument) rack.slots.insert(rack.slots.begin(), std::move(slot)); else rack.slots.push_back(std::move(slot));
    }
    if (rack.slots.empty()) racks.erase(id); else racks[id] = std::move(rack);
}
void unloadTrackVst3(int id, bool instrument) {
    for (const auto& slot : getTrackVst3Slots(id)) if (slot.instrument == instrument) removeTrackVst3(id, slot.id);
}
void clearTrackVst3() { racks.clear(); requestedRate = 0; while (!errors.pop().empty()) {} }
bool hasTrackVst3() { for (const auto& r : racks) if (!r.second.slots.empty()) return true; return false; }
void openTrackVst3SlotEditor(int id, const std::string& slotId, void* owner) {
    auto& s = findSlot(id, slotId); if (!s.host) throw std::runtime_error(s.error); s.host->openEditor(owner);
}
void openTrackVst3Editor(int id, bool instrument, void* owner) {
    for (const auto& s : getTrackVst3Slots(id)) if (s.instrument == instrument) { openTrackVst3SlotEditor(id, s.id, owner); return; }
    throw std::runtime_error("Load a plugin on this track first");
}
void serviceTrackVst3Controllers() {
    for (auto& r : racks) for (auto& s : r.second.slots) if (s->host) {
        s->host->serviceController();
        for (auto target : s->host->takeTweakedParameters()) {
            // Learned metadata is GUI-only; audio routes use immutable snapshots.
            auto& targets = s->saved.learnedParameters;
            auto found = std::find_if(targets.begin(), targets.end(), [&](const Vst3ParameterTarget& p) { return p.id == target.id; });
            if (found == targets.end()) targets.push_back(std::move(target)); else found->name = std::move(target.name);
        }
    }
}
std::vector<Vst3ModTarget> getTrackVst3ModTargets(int id) {
    serviceTrackVst3Controllers();
    std::vector<Vst3ModTarget> result;
    auto rack = racks.find(id); if (rack == racks.end()) return result;
    int effect = 0;
    for (const auto& s : rack->second.slots) {
        std::string prefix = s->saved.instrument ? "Instrument" : "FX " + std::to_string(++effect);
        for (const auto& p : s->saved.learnedParameters)
            result.push_back({s->saved.id, prefix + " / " + s->saved.name + " / " + p.name + (s->host ? "" : " (unavailable)"), p.id});
    }
    return result;
}
void prepareTrackVst3(double rate) {
    for (auto& entry : racks) { auto& rack = entry.second; for (auto& s : rack.slots) s->prepare(rate); rack.rate = rate; rack.reset(); }
}
std::string takeTrackVst3Error() { return errors.pop(); }
double takeTrackVst3RateRequest() { return requestedRate.exchange(0); }
int trackVst3FrameOffset(int id) noexcept { auto it = racks.find(id); return it == racks.end() || it->second.slots.empty() ? 0 : it->second.cursor; }
void renderTrackVst3(int id, bool useInstrument, double rate, double position, double tempo, bool reset,
                    bool gate, bool changed, const std::vector<StepNoteInfo>& ons, const std::vector<int>& present,
                    double& left, double& right, const std::vector<std::string>* order,
                    NativeFxProcess nativeProcess, void* nativeContext,
                    const std::vector<ModMatrixAssignment>* routes, const double* sources, size_t sourceCount, bool transportPlaying) noexcept {
    auto it = racks.find(id); if (it == racks.end() || it->second.slots.empty()) {
        if (useInstrument) left = right = 0;
        if (order && nativeProcess) for (const auto& key : *order) nativeProcess(nativeContext, key, 0, left, right);
        return;
    }
    auto& rack = it->second;
    if (rate != rack.rate) { requestedRate = rate; left = right = 0; return; }
    for (auto& ptr : rack.slots) {
        auto& s = *ptr;
        if (rack.cursor == 0) {
            s.context.projectTimeSamples = static_cast<int64>(position);
            s.context.projectTimeMusic = position * tempo / (60 * rate); s.context.tempo = tempo;
            s.context.state = (transportPlaying?ProcessContext::kPlaying:0) | ProcessContext::kTempoValid | ProcessContext::kTimeSigValid | ProcessContext::kProjectTimeMusicValid;
        }
        if (!s.saved.instrument || !useInstrument || s.failed) continue;
        try {
            for (int pitch = 0; pitch < 128; ++pitch) if (s.notes[pitch] && (reset || !gate ||
                (changed && std::find(present.begin(), present.end(), pitch) == present.end()))) s.note(pitch, 0, false, rack.cursor);
            if (gate && changed) for (const auto& info : ons) {
                const int pitch = std::clamp(info.midiNote, 0, 127);
                if (s.notes[pitch]) s.note(pitch, 0, false, rack.cursor);
                s.note(pitch, std::clamp(info.velocity, 0.f, 1.f), true, rack.cursor);
            }
        } catch (const std::exception& error) { s.failed = true; errors.push(id, s, error.what()); }
        catch (...) { s.failed = true; errors.push(id, s, "Unknown note processing error"); }
    }
    rack.input[0][rack.cursor] = useInstrument ? 0.f : static_cast<float>(left);
    rack.input[1][rack.cursor] = useInstrument ? 0.f : static_cast<float>(right);
    left = rack.output[0][rack.cursor]; right = rack.output[1][rack.cursor];
    if (++rack.cursor == blockSize) {
        Audio block = rack.input;
        auto processSlot = [&](Slot& s) {
            try {
                if (s.host) {
                    s.host->clearModulation();
                    if (routes && sources) for (const auto& route : *routes)
                        if (route.trackId == id && route.vstSlotId == s.saved.id && route.sourceIndex >= 0 && static_cast<size_t>(route.sourceIndex) < sourceCount)
                            s.host->addModulation(route.vstParameterId, std::clamp(double(route.normalizedAmount), -1.0, 1.0) * sources[route.sourceIndex]);
                }
                s.process(block);
            }
            catch (const std::exception& error) { s.failed = true; errors.push(id, s, error.what()); if (s.saved.instrument) block = {}; }
            catch (...) { s.failed = true; errors.push(id, s, "Unknown processing error"); if (s.saved.instrument) block = {}; }
        };
        for (auto& ptr : rack.slots) if (ptr->saved.instrument && useInstrument) processSlot(*ptr);
        if (order) {
            for (const auto& key : *order) {
                if (key.compare(0, 3, "kj:") == 0) {
                    if (nativeProcess) for (int i = 0; i < blockSize; ++i) {
                        double l = block[0][i], r = block[1][i];
                        nativeProcess(nativeContext, key, i, l, r);
                        block[0][i] = static_cast<float>(l); block[1][i] = static_cast<float>(r);
                    }
                } else for (auto& ptr : rack.slots) if (!ptr->saved.instrument && ptr->saved.id == key) { processSlot(*ptr); break; }
            }
        } else {
            for (auto& ptr : rack.slots) if (!ptr->saved.instrument) processSlot(*ptr);
        }
        rack.output = block; rack.cursor = 0;
    }
}
void stopTrackVst3Audio(int trackId) noexcept {
    for (auto& entry : racks) {
        if (trackId && entry.first != trackId) continue;
        for (auto& ptr : entry.second.slots) {
            auto& s = *ptr;
            if (s.host && s.processing) {
                try {
                    s.inputEvents.clear();
                    for (int pitch = 0; pitch < 128; ++pitch) if (s.notes[pitch]) s.note(pitch, 0, false, 0);
                    s.context.state &= ~ProcessContext::kPlaying;
                    Audio silence {}; if (!s.failed) s.process(silence);
                } catch (const std::exception& error) { s.failed = true; errors.push(entry.first, s, error.what()); }
                catch (...) { s.failed = true; errors.push(entry.first, s, "Unknown stop error"); }
                try { s.host->stopProcessing(); s.processing = false; }
                catch (const std::exception& error) { errors.push(entry.first, s, error.what()); }
                catch (...) { errors.push(entry.first, s, "Unknown setProcessing(false) error"); }
            }
            s.notes = {}; s.inputEvents.clear();
        }
        entry.second.reset();
    }
}
}
