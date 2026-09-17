#include "core/tracks.h"
#include "core/track_type_sample.h"
#include "core/tracks_internal.h"
#include "core/audio_engine.h"


#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <utility>
#include <vector>

namespace track_internal
{

std::vector<std::shared_ptr<TrackData>> gTracks;
std::shared_mutex gTrackMutex;

int clampTrackNote(const std::shared_ptr<TrackData>& track, int note)
{
    return track->type.load(std::memory_order_relaxed) == TrackType::Sample
        ? std::max(0, note) : clampMidiNote(note);
}

int gNextTrackId = 1;

std::shared_ptr<TrackData> makeTrackData(const std::string& name)
{
    Track baseTrack;
    baseTrack.id = gNextTrackId++;
    baseTrack.name = name.empty() ? "Track " + std::to_string(baseTrack.id) : name;
    baseTrack.type = TrackType::Synth;
    baseTrack.synthWaveType = SynthWaveType::Sine;
    baseTrack.volume = 1.0f;
    baseTrack.pan = 0.0f;
    baseTrack.lowGainDb = 0.0f;
    baseTrack.midGainDb = 0.0f;
    baseTrack.highGainDb = 0.0f;
    baseTrack.eqEnabled = false;
    baseTrack.delayEnabled = false;
    baseTrack.delayTimeMs = kDefaultDelayTimeMs;
    baseTrack.delayFeedback = kDefaultDelayFeedback;
    baseTrack.delayMix = kDefaultDelayMix;
    baseTrack.compressorEnabled = false;
    baseTrack.compressorThresholdDb = kDefaultCompressorThresholdDb;
    baseTrack.compressorRatio = kDefaultCompressorRatio;
    baseTrack.compressorAttack = kDefaultCompressorAttack;
    baseTrack.compressorRelease = kDefaultCompressorRelease;
    baseTrack.sidechainEnabled = false;
    baseTrack.sidechainSourceTrackId = kDefaultSidechainSourceTrack;
    baseTrack.sidechainAmount = kDefaultSidechainAmount;
    baseTrack.sidechainAttack = kDefaultSidechainAttack;
    baseTrack.sidechainRelease = kDefaultSidechainRelease;
    baseTrack.formant = kDefaultFormant;
    baseTrack.resonance = kDefaultResonance;
    baseTrack.feedback = kDefaultFeedback;
    baseTrack.pitch = kDefaultPitch;
    baseTrack.pitchRange = kDefaultPitchRange;
    baseTrack.synthAttack = kDefaultSynthAttack;
    baseTrack.synthDecay = kDefaultSynthDecay;
    baseTrack.synthSustain = kDefaultSynthSustain;
    baseTrack.synthRelease = kDefaultSynthRelease;
    baseTrack.synthThreeOscEnabled = false;
    for (auto& osc : baseTrack.synthOscillators)
    {
        osc.formant = kDefaultFormant;
        osc.resonance = kDefaultResonance;
        osc.feedback = kDefaultFeedback;
        osc.pitch = kDefaultPitch;
        osc.pitchRange = kDefaultPitchRange;
        osc.attack = kDefaultSynthAttack;
        osc.decay = kDefaultSynthDecay;
        osc.sustain = kDefaultSynthSustain;
        osc.release = kDefaultSynthRelease;
        osc.wavetableEnabled = kDefaultSynthWavetableEnabled;
    }
    baseTrack.sampleAttack = kDefaultSampleAttack;
    baseTrack.sampleRelease = kDefaultSampleRelease;
    baseTrack.midiChannel = kDefaultMidiChannel;
    baseTrack.midiPort = kDefaultMidiPort;
    baseTrack.midiPortName.clear();
    return std::make_shared<TrackData>(std::move(baseTrack));
}

std::shared_ptr<TrackData> findTrackData(int trackId)
{
    std::shared_lock<std::shared_mutex> lock(gTrackMutex);
    for (auto& track : gTracks)
    {
        if (track->track.id == trackId)
        {
            return track;
        }
    }
    return {};
}

} // namespace track_internal

using namespace track_internal;
#include "core/piano_pattern.h"

void initTracks()
{
    piano::reset();
    std::unique_lock<std::shared_mutex> lock(gTrackMutex);
    gTracks.clear();
    gNextTrackId = 1;
    gTracks.push_back(makeTrackData({}));
}

kj::Vst3RackState trackGetVst3State(int id) {
    std::shared_lock<std::shared_mutex> lock(track_internal::gTrackMutex);
    for (const auto& track : track_internal::gTracks) if (track->track.id == id) return track->vst3;
    return {};
}
std::vector<std::string> trackGetFxOrder(int id) {
    std::shared_lock<std::shared_mutex> lock(track_internal::gTrackMutex);
    for (const auto& track : track_internal::gTracks) if (track->track.id == id) return track->track.fxOrder;
    return {};
}
void trackSetFxOrder(int id, std::vector<std::string> order) {
    std::vector<std::string> unique;
    for (auto& key : order) if (!key.empty() && std::find(unique.begin(), unique.end(), key) == unique.end()) unique.push_back(std::move(key));
    std::unique_lock<std::shared_mutex> lock(track_internal::gTrackMutex);
    for (auto& track : track_internal::gTracks) if (track->track.id == id) { track->track.fxOrder = std::move(unique); return; }
}
void trackSetVst3State(int id, kj::Vst3RackState state) {
    std::unique_lock<std::shared_mutex> lock(track_internal::gTrackMutex);
    for (auto& track : track_internal::gTracks) if (track->track.id == id) {
        track->vst3 = std::move(state); return;
    }
}

Track addTrack(const std::string& name)
{
    std::shared_ptr<TrackData> trackData;
    {
        std::unique_lock<std::shared_mutex> lock(gTrackMutex);
        trackData = makeTrackData(name);
        gTracks.push_back(trackData);
    }
    if (!trackData)
    {
        return {};
    }

    Track result = trackData->track;
    result.type = trackData->type.load(std::memory_order_relaxed);
    result.synthWaveType = trackData->waveType.load(std::memory_order_relaxed);
    result.midiPort = trackData->midiPort.load(std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(trackData->midiPortMutex);
        result.midiPortName = trackData->midiPortName;
    }
    return result;
}

std::vector<Track> getTracks()
{
    std::shared_lock<std::shared_mutex> lock(gTrackMutex);
    std::vector<Track> result;
    result.reserve(gTracks.size());
    for (const auto& track : gTracks)
    {
        Track info = track->track;
        info.type = track->type.load(std::memory_order_relaxed);
        info.synthWaveType = track->waveType.load(std::memory_order_relaxed);
        info.volume = track->volume.load(std::memory_order_relaxed);
        info.pan = track->pan.load(std::memory_order_relaxed);
        info.lowGainDb = track->lowGainDb.load(std::memory_order_relaxed);
        info.midGainDb = track->midGainDb.load(std::memory_order_relaxed);
        info.highGainDb = track->highGainDb.load(std::memory_order_relaxed);
        info.eqEnabled = track->eqEnabled.load(std::memory_order_relaxed);
        for (size_t band = 0; band < 3; ++band) {
            info.eqShape[band] = track->eqShape[band].load(std::memory_order_relaxed);
            info.eqFrequency[band] = track->eqFrequency[band].load(std::memory_order_relaxed);
            info.eqQ[band] = track->eqQ[band].load(std::memory_order_relaxed);
        }
        info.delayEnabled = track->delayEnabled.load(std::memory_order_relaxed);
        info.delayTimeMs = track->delayTimeMs.load(std::memory_order_relaxed);
        info.delayFeedback = track->delayFeedback.load(std::memory_order_relaxed);
        info.delayMix = track->delayMix.load(std::memory_order_relaxed);
        info.compressorEnabled = track->compressorEnabled.load(std::memory_order_relaxed);
        info.compressorThresholdDb = track->compressorThresholdDb.load(std::memory_order_relaxed);
        info.compressorRatio = track->compressorRatio.load(std::memory_order_relaxed);
        info.compressorAttack = track->compressorAttack.load(std::memory_order_relaxed);
        info.compressorRelease = track->compressorRelease.load(std::memory_order_relaxed);
        info.sidechainEnabled = track->sidechainEnabled.load(std::memory_order_relaxed);
        info.sidechainSourceTrackId = track->sidechainSourceTrackId.load(std::memory_order_relaxed);
        info.sidechainAmount = track->sidechainAmount.load(std::memory_order_relaxed);
        info.sidechainAttack = track->sidechainAttack.load(std::memory_order_relaxed);
        info.sidechainRelease = track->sidechainRelease.load(std::memory_order_relaxed);
        info.formant = track->formant.load(std::memory_order_relaxed);
        info.resonance = track->resonance.load(std::memory_order_relaxed);
        info.feedback = track->feedback.load(std::memory_order_relaxed);
        info.pitch = track->pitch.load(std::memory_order_relaxed);
        info.pitchRange = track->pitchRange.load(std::memory_order_relaxed);
        info.synthAttack = track->synthAttack.load(std::memory_order_relaxed);
        info.synthDecay = track->synthDecay.load(std::memory_order_relaxed);
        info.synthSustain = track->synthSustain.load(std::memory_order_relaxed);
        info.synthRelease = track->synthRelease.load(std::memory_order_relaxed);
        info.synthPhaseSync = track->synthPhaseSync.load(std::memory_order_relaxed);
        info.synthThreeOscEnabled = track->synthThreeOscEnabled.load(std::memory_order_relaxed);
        for (size_t oscIndex = 0; oscIndex < info.synthOscillators.size(); ++oscIndex)
        {
            info.synthOscillators[oscIndex].formant = track->synthOscFormant[oscIndex].load(std::memory_order_relaxed);
            info.synthOscillators[oscIndex].resonance = track->synthOscResonance[oscIndex].load(std::memory_order_relaxed);
            info.synthOscillators[oscIndex].feedback = track->synthOscFeedback[oscIndex].load(std::memory_order_relaxed);
            info.synthOscillators[oscIndex].pitch = track->synthOscPitch[oscIndex].load(std::memory_order_relaxed);
            info.synthOscillators[oscIndex].pitchRange = track->synthOscPitchRange[oscIndex].load(std::memory_order_relaxed);
            info.synthOscillators[oscIndex].attack = track->synthOscAttack[oscIndex].load(std::memory_order_relaxed);
            info.synthOscillators[oscIndex].decay = track->synthOscDecay[oscIndex].load(std::memory_order_relaxed);
            info.synthOscillators[oscIndex].sustain = track->synthOscSustain[oscIndex].load(std::memory_order_relaxed);
            info.synthOscillators[oscIndex].release = track->synthOscRelease[oscIndex].load(std::memory_order_relaxed);
            info.synthOscillators[oscIndex].wavetableEnabled =
                track->synthOscWavetableEnabled[oscIndex].load(std::memory_order_relaxed);
            info.synthOscillators[oscIndex].wavetablePosition = track->synthOscWavetablePosition[oscIndex].load(std::memory_order_relaxed);
            info.synthOscillators[oscIndex].wavetableMix = track->synthOscWavetableMix[oscIndex].load(std::memory_order_relaxed);
        }
        info.sampleAttack = track->sampleAttack.load(std::memory_order_relaxed);
        info.sampleRelease = track->sampleRelease.load(std::memory_order_relaxed);
        for (size_t i = 0; i < info.lfoSettings.size(); ++i)
        {
            info.lfoSettings[i].rateHz = track->lfoRateHz[i].load(std::memory_order_relaxed);
            info.lfoSettings[i].shape = track->lfoShape[i].load(std::memory_order_relaxed);
            info.lfoSettings[i].deform = track->lfoDeform[i].load(std::memory_order_relaxed);
        }
        info.midiChannel = track->midiChannel.load(std::memory_order_relaxed);
        {
            std::lock_guard<std::mutex> lock(track->midiPortMutex);
            info.midiPortName = track->midiPortName;
            info.midiPort = track->midiPort.load(std::memory_order_relaxed);
        }
        result.push_back(std::move(info));
    }
    return result;
}

size_t getTrackCount()
{
    std::shared_lock<std::shared_mutex> lock(gTrackMutex);
    return gTracks.size();
}

void trackSetName(int trackId, const std::string& name)
{
    std::unique_lock<std::shared_mutex> lock(gTrackMutex);
    for (auto& track : gTracks)
    {
        if (track->track.id == trackId)
        {
            track->track.name = name;
            if (track->track.name.empty())
            {
                track->track.name = "Track " + std::to_string(track->track.id);
            }
            return;
        }
    }
}

bool trackGetStepState(int trackId, int stepIndex)
{
    if (stepIndex < 0 || stepIndex >= kMaxSequencerSteps)
        return false;

    auto track = findTrackData(trackId);
    if (!track)
        return false;

    int stepCount = track->stepCount.load(std::memory_order_relaxed);
    if (stepIndex >= stepCount)
        return false;

    return track->steps[stepIndex].load(std::memory_order_relaxed);
}

void trackSetStepState(int trackId, int stepIndex, bool enabled)
{
    if (stepIndex < 0 || stepIndex >= kMaxSequencerSteps)
        return;

    auto track = findTrackData(trackId);
    if (!track)
        return;

    int stepCount = track->stepCount.load(std::memory_order_relaxed);
    if (stepIndex >= stepCount)
        return;

    {
        std::lock_guard<std::mutex> lock(track->noteMutex);
        auto& notes = track->stepNotes[stepIndex];
        if (!enabled)
        {
            notes.clear();
        }
        else if (notes.empty())
        {
            int note = track->notes[stepIndex].load(std::memory_order_relaxed);
            TrackData::StepNoteEntry entry{};
            if (track->type.load(std::memory_order_relaxed) == TrackType::Sample)
            {
                bool drum = track->sampleDrumMode.load(std::memory_order_relaxed);
                entry.midiNote = drum ? std::max(kSampleDrumNoteBase, note)
                                     : (note < kSampleDrumNoteBase ? note : kSampleRootNote);
            }
            else
                entry.midiNote = clampMidiNote(note);
            entry.velocity = track->stepVelocity[stepIndex].load(std::memory_order_relaxed);
            entry.sustain = false;
            notes.push_back(entry);
        }
    }

    track->steps[stepIndex].store(enabled, std::memory_order_relaxed);
}

void trackToggleStepState(int trackId, int stepIndex)
{
    if (stepIndex < 0 || stepIndex >= kMaxSequencerSteps)
        return;

    auto track = findTrackData(trackId);
    if (!track)
        return;

    int stepCount = track->stepCount.load(std::memory_order_relaxed);
    if (stepIndex >= stepCount)
        return;

    bool current = track->steps[stepIndex].load(std::memory_order_relaxed);
    trackSetStepState(trackId, stepIndex, !current);
}

int trackGetStepNote(int trackId, int stepIndex)
{
    if (stepIndex < 0 || stepIndex >= kMaxSequencerSteps)
        return kDefaultMidiNote;

    auto track = findTrackData(trackId);
    if (!track)
        return kDefaultMidiNote;

    int stepCount = track->stepCount.load(std::memory_order_relaxed);
    if (stepIndex >= stepCount)
        return kDefaultMidiNote;

    int note = track->notes[stepIndex].load(std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(track->noteMutex);
        const auto& notes = track->stepNotes[stepIndex];
        if (!notes.empty())
        {
            note = notes.front().midiNote;
        }
    }
    return clampTrackNote(track, note);
}

void trackSetStepNote(int trackId, int stepIndex, int midiNote)
{
    if (stepIndex < 0 || stepIndex >= kMaxSequencerSteps)
        return;

    auto track = findTrackData(trackId);
    if (!track)
        return;

    int stepCount = track->stepCount.load(std::memory_order_relaxed);
    if (stepIndex >= stepCount)
        return;

    int clamped = clampTrackNote(track, midiNote);
    track->notes[stepIndex].store(clamped, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(track->noteMutex);
        auto& notes = track->stepNotes[stepIndex];
        notes.clear();
        TrackData::StepNoteEntry entry{};
        entry.midiNote = clamped;
        entry.velocity = track->stepVelocity[stepIndex].load(std::memory_order_relaxed);
        entry.sustain = false;
        notes.push_back(entry);
    }
    track->steps[stepIndex].store(true, std::memory_order_relaxed);
}

std::vector<int> trackGetStepNotes(int trackId, int stepIndex)
{
    if (stepIndex < 0 || stepIndex >= kMaxSequencerSteps)
        return {};

    auto track = findTrackData(trackId);
    if (!track)
        return {};

    int stepCount = track->stepCount.load(std::memory_order_relaxed);
    if (stepIndex >= stepCount)
        return {};

    std::vector<int> result;
    {
        std::lock_guard<std::mutex> lock(track->noteMutex);
        const auto& entries = track->stepNotes[stepIndex];
        result.reserve(entries.size());
        for (const auto& entry : entries)
        {
            int clamped = clampTrackNote(track, entry.midiNote);
            if (clamped >= kMinMidiNote)
            {
                result.push_back(clamped);
            }
        }
    }
    result.erase(std::remove_if(result.begin(), result.end(), [](int value) {
        return value < kMinMidiNote;
    }), result.end());
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}

void trackToggleStepNote(int trackId, int stepIndex, int midiNote)
{
    if (stepIndex < 0 || stepIndex >= kMaxSequencerSteps)
        return;

    auto track = findTrackData(trackId);
    if (!track)
        return;

    int stepCount = track->stepCount.load(std::memory_order_relaxed);
    if (stepIndex >= stepCount)
        return;

    int clamped = clampTrackNote(track, midiNote);
    {
        std::lock_guard<std::mutex> lock(track->noteMutex);
        auto& notes = track->stepNotes[stepIndex];
        auto it = std::find_if(notes.begin(), notes.end(), [clamped](const TrackData::StepNoteEntry& entry) {
            return entry.midiNote == clamped;
        });
        if (it != notes.end())
        {
            notes.erase(it);
        }
        else
        {
            TrackData::StepNoteEntry entry{};
            entry.midiNote = clamped;
            entry.velocity = track->stepVelocity[stepIndex].load(std::memory_order_relaxed);
            entry.sustain = false;
            notes.push_back(entry);
            std::sort(notes.begin(), notes.end(), [](const TrackData::StepNoteEntry& a, const TrackData::StepNoteEntry& b) {
                return a.midiNote < b.midiNote;
            });
        }

        bool enabled = !notes.empty();
        if (enabled)
        {
            track->notes[stepIndex].store(notes.front().midiNote, std::memory_order_relaxed);
        }
        else
        {
            track->notes[stepIndex].store(kDefaultMidiNote, std::memory_order_relaxed);
        }
        track->steps[stepIndex].store(enabled, std::memory_order_relaxed);
    }
}

float trackGetStepVelocity(int trackId, int stepIndex)
{
    if (stepIndex < 0 || stepIndex >= kMaxSequencerSteps)
        return kTrackStepVelocityMax;

    auto track = findTrackData(trackId);
    if (!track)
        return kTrackStepVelocityMax;

    float value = track->stepVelocity[stepIndex].load(std::memory_order_relaxed);
    return std::clamp(value, kTrackStepVelocityMin, kTrackStepVelocityMax);
}

void trackSetStepVelocity(int trackId, int stepIndex, float value)
{
    if (stepIndex < 0 || stepIndex >= kMaxSequencerSteps)
        return;

    auto track = findTrackData(trackId);
    if (!track)
        return;

    float clamped = std::clamp(value, kTrackStepVelocityMin, kTrackStepVelocityMax);
    track->stepVelocity[stepIndex].store(clamped, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(track->noteMutex);
        auto& notes = track->stepNotes[stepIndex];
        for (auto& entry : notes)
        {
            entry.velocity = clamped;
        }
    }
}

float trackGetStepNoteVelocity(int trackId, int stepIndex, int midiNote)
{
    if (stepIndex < 0 || stepIndex >= kMaxSequencerSteps)
        return kTrackStepVelocityMax;

    auto track = findTrackData(trackId);
    if (!track)
        return kTrackStepVelocityMax;

    int stepCount = track->stepCount.load(std::memory_order_relaxed);
    if (stepIndex >= stepCount)
        return kTrackStepVelocityMax;

    int clampedNote = clampTrackNote(track, midiNote);
    float fallback = track->stepVelocity[stepIndex].load(std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(track->noteMutex);
    const auto& notes = track->stepNotes[stepIndex];
    auto it = std::find_if(notes.begin(), notes.end(), [clampedNote](const TrackData::StepNoteEntry& entry) {
        return entry.midiNote == clampedNote;
    });
    if (it != notes.end())
    {
        return std::clamp(it->velocity, kTrackStepVelocityMin, kTrackStepVelocityMax);
    }
    return std::clamp(fallback, kTrackStepVelocityMin, kTrackStepVelocityMax);
}

std::vector<StepNoteInfo> trackGetStepNoteInfo(int trackId, int stepIndex)
{
    if (stepIndex < 0 || stepIndex >= kMaxSequencerSteps)
        return {};

    auto track = findTrackData(trackId);
    if (!track)
        return {};

    int stepCount = track->stepCount.load(std::memory_order_relaxed);
    if (stepIndex >= stepCount)
        return {};

    std::vector<StepNoteInfo> result;
    std::lock_guard<std::mutex> lock(track->noteMutex);
    const auto& notes = track->stepNotes[stepIndex];
    result.reserve(notes.size());
    for (const auto& entry : notes)
    {
        StepNoteInfo info{};
        info.midiNote = clampTrackNote(track, entry.midiNote);
        info.velocity = std::clamp(entry.velocity, kTrackStepVelocityMin, kTrackStepVelocityMax);
        info.sustain = entry.sustain;
        if (info.midiNote >= kMinMidiNote)
        {
            result.push_back(info);
        }
    }
    std::sort(result.begin(), result.end(), [](const StepNoteInfo& a, const StepNoteInfo& b) {
        if (a.midiNote != b.midiNote)
            return a.midiNote < b.midiNote;
        if (a.sustain != b.sustain)
            return a.sustain < b.sustain;
        return a.velocity < b.velocity;
    });
    result.erase(std::unique(result.begin(), result.end(), [](const StepNoteInfo& a, const StepNoteInfo& b) {
        return a.midiNote == b.midiNote;
    }), result.end());
    return result;
}

bool trackGetStepNoteSustain(int trackId, int stepIndex, int midiNote)
{
    if (stepIndex < 0 || stepIndex >= kMaxSequencerSteps)
        return false;

    auto track = findTrackData(trackId);
    if (!track)
        return false;

    int stepCount = track->stepCount.load(std::memory_order_relaxed);
    if (stepIndex >= stepCount)
        return false;

    int clampedNote = clampTrackNote(track, midiNote);
    std::lock_guard<std::mutex> lock(track->noteMutex);
    const auto& notes = track->stepNotes[stepIndex];
    auto it = std::find_if(notes.begin(), notes.end(), [clampedNote](const TrackData::StepNoteEntry& entry) {
        return entry.midiNote == clampedNote;
    });
    if (it != notes.end())
        return it->sustain;
    return false;
}

void trackSetStepNoteSustain(int trackId, int stepIndex, int midiNote, bool sustain)
{
    if (stepIndex < 0 || stepIndex >= kMaxSequencerSteps)
        return;

    auto track = findTrackData(trackId);
    if (!track)
        return;

    int stepCount = track->stepCount.load(std::memory_order_relaxed);
    if (stepIndex >= stepCount)
        return;

    int clampedNote = clampTrackNote(track, midiNote);
    std::lock_guard<std::mutex> lock(track->noteMutex);
    auto& notes = track->stepNotes[stepIndex];
    auto it = std::find_if(notes.begin(), notes.end(), [clampedNote](const TrackData::StepNoteEntry& entry) {
        return entry.midiNote == clampedNote;
    });
    if (it != notes.end())
    {
        it->sustain = sustain;
    }
}

void trackSetStepNoteVelocity(int trackId, int stepIndex, int midiNote, float value)
{
    if (stepIndex < 0 || stepIndex >= kMaxSequencerSteps)
        return;

    auto track = findTrackData(trackId);
    if (!track)
        return;

    int stepCount = track->stepCount.load(std::memory_order_relaxed);
    if (stepIndex >= stepCount)
        return;

    int clampedNote = clampTrackNote(track, midiNote);
    float clampedValue = std::clamp(value, kTrackStepVelocityMin, kTrackStepVelocityMax);
    std::lock_guard<std::mutex> lock(track->noteMutex);
    auto& notes = track->stepNotes[stepIndex];
    auto it = std::find_if(notes.begin(), notes.end(), [clampedNote](const TrackData::StepNoteEntry& entry) {
        return entry.midiNote == clampedNote;
    });
    if (it != notes.end())
    {
        it->velocity = clampedValue;
        if (notes.size() == 1)
        {
            track->stepVelocity[stepIndex].store(clampedValue, std::memory_order_relaxed);
        }
    }
}

float trackGetStepPan(int trackId, int stepIndex)
{
    if (stepIndex < 0 || stepIndex >= kMaxSequencerSteps)
        return 0.0f;

    auto track = findTrackData(trackId);
    if (!track)
        return 0.0f;

    float value = track->stepPan[stepIndex].load(std::memory_order_relaxed);
    return std::clamp(value, kTrackStepPanMin, kTrackStepPanMax);
}

void trackSetStepPan(int trackId, int stepIndex, float value)
{
    if (stepIndex < 0 || stepIndex >= kMaxSequencerSteps)
        return;

    auto track = findTrackData(trackId);
    if (!track)
        return;

    float clamped = std::clamp(value, kTrackStepPanMin, kTrackStepPanMax);
    track->stepPan[stepIndex].store(clamped, std::memory_order_relaxed);
}

float trackGetStepPitchOffset(int trackId, int stepIndex)
{
    if (stepIndex < 0 || stepIndex >= kMaxSequencerSteps)
        return 0.0f;

    auto track = findTrackData(trackId);
    if (!track)
        return 0.0f;

    float value = track->stepPitch[stepIndex].load(std::memory_order_relaxed);
    return std::clamp(value, kTrackStepPitchMin, kTrackStepPitchMax);
}

void trackSetStepPitchOffset(int trackId, int stepIndex, float value)
{
    if (stepIndex < 0 || stepIndex >= kMaxSequencerSteps)
        return;

    auto track = findTrackData(trackId);
    if (!track)
        return;

    float clamped = std::clamp(value, kTrackStepPitchMin, kTrackStepPitchMax);
    track->stepPitch[stepIndex].store(clamped, std::memory_order_relaxed);
}

int trackGetStepCount(int trackId)
{
    auto track = findTrackData(trackId);
    if (!track)
        return 0;

    int count = track->stepCount.load(std::memory_order_relaxed);
    return std::clamp(count, 1, kMaxSequencerSteps);
}

void trackSetStepCount(int trackId, int count)
{
    auto track = findTrackData(trackId);
    if (!track)
        return;

    int clamped = std::clamp(count, 1, kMaxSequencerSteps);
    int previous = track->stepCount.load(std::memory_order_relaxed);
    if (clamped == previous)
        return;

    if (clamped > previous)
    {
        int maxInitialized = track->maxInitializedStepCount.load(std::memory_order_relaxed);
        if (clamped > maxInitialized)
        {
            std::lock_guard<std::mutex> lock(track->noteMutex);
            for (int i = maxInitialized; i < clamped; ++i)
            {
                track->steps[i].store(false, std::memory_order_relaxed);
                track->notes[i].store(kDefaultMidiNote, std::memory_order_relaxed);
                track->stepNotes[i].clear();
                track->stepVelocity[i].store(kTrackStepVelocityMax, std::memory_order_relaxed);
                track->stepPan[i].store(0.0f, std::memory_order_relaxed);
                track->stepPitch[i].store(0.0f, std::memory_order_relaxed);
            }
            track->maxInitializedStepCount.store(clamped, std::memory_order_relaxed);
        }
    }

    track->stepCount.store(clamped, std::memory_order_relaxed);
}

TrackType trackGetType(int trackId)
{
    auto track = findTrackData(trackId);
    if (!track)
        return TrackType::Synth;

    return track->type.load(std::memory_order_relaxed);
}

void trackSetType(int trackId, TrackType type)
{
    std::unique_lock<std::shared_mutex> lock(gTrackMutex);
    for (auto& track : gTracks)
    {
        if (track->track.id == trackId)
        {
            if(track->track.type!=type)track->track.inputMonitor=false;
            track->type.store(type, std::memory_order_relaxed);
            track->track.type = type;

            return;
        }
    }
}

float trackGetVolume(int trackId)
{
    auto track = findTrackData(trackId);
    if (!track)
        return 1.0f;

    float volume = track->volume.load(std::memory_order_relaxed);
    return std::clamp(volume, kMinVolume, kMaxVolume);
}

void trackSetVolume(int trackId, float volume)
{
    auto track = findTrackData(trackId);
    if (!track)
        return;

    float clamped = std::clamp(volume, kMinVolume, kMaxVolume);
    track->volume.store(clamped, std::memory_order_relaxed);
}

float trackGetPan(int trackId)
{
    auto track = findTrackData(trackId);
    if (!track)
        return 0.0f;

    float pan = track->pan.load(std::memory_order_relaxed);
    return std::clamp(pan, kMinPan, kMaxPan);
}

void trackSetPan(int trackId, float pan)
{
    auto track = findTrackData(trackId);
    if (!track)
        return;

    float clamped = std::clamp(pan, kMinPan, kMaxPan);
    track->pan.store(clamped, std::memory_order_relaxed);
}

float trackGetEqFrequency(int trackId, int band) {
    auto track = findTrackData(trackId);
    if (!track || band < 0 || band >= 3) return 1000.0f;
    return track->eqFrequency[band].load(std::memory_order_relaxed);
}
void trackSetEqFrequency(int trackId, int band, float value) {
    auto track = findTrackData(trackId);
    if (!track || band < 0 || band >= 3 || !std::isfinite(value)) return;
    track->eqFrequency[band].store(std::clamp(value, 20.0f, 20000.0f), std::memory_order_relaxed);
}
int trackGetEqShape(int trackId,int band) {
    auto track=findTrackData(trackId);if(!track||band<0||band>=3)return 0;
    return track->eqShape[band].load(std::memory_order_relaxed);
}
void trackSetEqShape(int trackId,int band,int shape) {
    auto track=findTrackData(trackId);if(!track||band<0||band>=3)return;
    track->eqShape[band].store(std::clamp(shape,0,6),std::memory_order_relaxed);
}
float trackGetEqQ(int trackId, int band) {
    auto track = findTrackData(trackId);
    if (!track || band < 0 || band >= 3) return 0.707f;
    return track->eqQ[band].load(std::memory_order_relaxed);
}
void trackSetEqQ(int trackId, int band, float value) {
    auto track = findTrackData(trackId);
    if (!track || band < 0 || band >= 3 || !std::isfinite(value)) return;
    track->eqQ[band].store(std::clamp(value, 0.1f, 10.0f), std::memory_order_relaxed);
}

float trackGetEqLowGain(int trackId)
{
    auto track = findTrackData(trackId);
    if (!track)
        return 0.0f;

    float gain = track->lowGainDb.load(std::memory_order_relaxed);
    return std::clamp(gain, kMinEqGainDb, kMaxEqGainDb);
}

float trackGetEqMidGain(int trackId)
{
    auto track = findTrackData(trackId);
    if (!track)
        return 0.0f;

    float gain = track->midGainDb.load(std::memory_order_relaxed);
    return std::clamp(gain, kMinEqGainDb, kMaxEqGainDb);
}

float trackGetEqHighGain(int trackId)
{
    auto track = findTrackData(trackId);
    if (!track)
        return 0.0f;

    float gain = track->highGainDb.load(std::memory_order_relaxed);
    return std::clamp(gain, kMinEqGainDb, kMaxEqGainDb);
}

bool trackGetEqEnabled(int trackId)
{
    auto track = findTrackData(trackId);
    if (!track)
        return true;

    return track->eqEnabled.load(std::memory_order_relaxed);
}

void trackSetEqLowGain(int trackId, float gainDb)
{
    auto track = findTrackData(trackId);
    if (!track || !std::isfinite(gainDb))
        return;

    float clamped = std::clamp(gainDb, kMinEqGainDb, kMaxEqGainDb);
    track->lowGainDb.store(clamped, std::memory_order_relaxed);
}

void trackSetEqMidGain(int trackId, float gainDb)
{
    auto track = findTrackData(trackId);
    if (!track || !std::isfinite(gainDb))
        return;

    float clamped = std::clamp(gainDb, kMinEqGainDb, kMaxEqGainDb);
    track->midGainDb.store(clamped, std::memory_order_relaxed);
}

void trackSetEqHighGain(int trackId, float gainDb)
{
    auto track = findTrackData(trackId);
    if (!track || !std::isfinite(gainDb))
        return;

    float clamped = std::clamp(gainDb, kMinEqGainDb, kMaxEqGainDb);
    track->highGainDb.store(clamped, std::memory_order_relaxed);
}

void trackSetEqEnabled(int trackId, bool enabled)
{
    auto track = findTrackData(trackId);
    if (!track)
        return;

    track->eqEnabled.store(enabled, std::memory_order_relaxed);
    if (enabled) {
        std::unique_lock<std::shared_mutex> lock(gTrackMutex);
        auto& order = track->track.fxOrder;
        if (std::find(order.begin(), order.end(), "kj:eq") == order.end()) order.push_back("kj:eq");
    }
}

bool trackGetDelayEnabled(int trackId)
{
    auto track = findTrackData(trackId);
    if (!track)
        return false;

    return track->delayEnabled.load(std::memory_order_relaxed);
}

void trackSetDelayEnabled(int trackId, bool enabled)
{
    auto track = findTrackData(trackId);
    if (!track)
        return;

    track->delayEnabled.store(enabled, std::memory_order_relaxed);
    if (enabled) {
        std::unique_lock<std::shared_mutex> lock(gTrackMutex);
        auto& order = track->track.fxOrder;
        if (std::find(order.begin(), order.end(), "kj:delay") == order.end()) order.push_back("kj:delay");
    }
}

float trackGetDelayTimeMs(int trackId)
{
    auto track = findTrackData(trackId);
    if (!track)
        return kDefaultDelayTimeMs;

    float value = track->delayTimeMs.load(std::memory_order_relaxed);
    return std::clamp(value, kMinDelayTimeMs, kMaxDelayTimeMs);
}

void trackSetDelayTimeMs(int trackId, float value)
{
    auto track = findTrackData(trackId);
    if (!track)
        return;

    float clamped = std::clamp(value, kMinDelayTimeMs, kMaxDelayTimeMs);
    track->delayTimeMs.store(clamped, std::memory_order_relaxed);
}

float trackGetDelayFeedback(int trackId)
{
    auto track = findTrackData(trackId);
    if (!track)
        return kDefaultDelayFeedback;

    float value = track->delayFeedback.load(std::memory_order_relaxed);
    return std::clamp(value, kMinDelayFeedback, kMaxDelayFeedback);
}

void trackSetDelayFeedback(int trackId, float value)
{
    auto track = findTrackData(trackId);
    if (!track)
        return;

    float clamped = std::clamp(value, kMinDelayFeedback, kMaxDelayFeedback);
    track->delayFeedback.store(clamped, std::memory_order_relaxed);
}

float trackGetDelayMix(int trackId)
{
    auto track = findTrackData(trackId);
    if (!track)
        return kDefaultDelayMix;

    float value = track->delayMix.load(std::memory_order_relaxed);
    return std::clamp(value, kMinDelayMix, kMaxDelayMix);
}

void trackSetDelayMix(int trackId, float value)
{
    auto track = findTrackData(trackId);
    if (!track)
        return;

    float clamped = std::clamp(value, kMinDelayMix, kMaxDelayMix);
    track->delayMix.store(clamped, std::memory_order_relaxed);
}

bool trackGetCompressorEnabled(int trackId)
{
    auto track = findTrackData(trackId);
    if (!track)
        return false;

    return track->compressorEnabled.load(std::memory_order_relaxed);
}

void trackSetCompressorEnabled(int trackId, bool enabled)
{
    auto track = findTrackData(trackId);
    if (!track)
        return;

    track->compressorEnabled.store(enabled, std::memory_order_relaxed);
    if (enabled) {
        std::unique_lock<std::shared_mutex> lock(gTrackMutex);
        auto& order = track->track.fxOrder;
        if (std::find(order.begin(), order.end(), "kj:compressor") == order.end()) order.push_back("kj:compressor");
    }
}

float trackGetCompressorThresholdDb(int trackId)
{
    auto track = findTrackData(trackId);
    if (!track)
        return kDefaultCompressorThresholdDb;

    float value = track->compressorThresholdDb.load(std::memory_order_relaxed);
    return std::clamp(value, kMinCompressorThresholdDb, kMaxCompressorThresholdDb);
}

void trackSetCompressorThresholdDb(int trackId, float value)
{
    auto track = findTrackData(trackId);
    if (!track)
        return;

    float clamped = std::clamp(value, kMinCompressorThresholdDb, kMaxCompressorThresholdDb);
    track->compressorThresholdDb.store(clamped, std::memory_order_relaxed);
}

float trackGetCompressorRatio(int trackId)
{
    auto track = findTrackData(trackId);
    if (!track)
        return kDefaultCompressorRatio;

    float value = track->compressorRatio.load(std::memory_order_relaxed);
    return std::clamp(value, kMinCompressorRatio, kMaxCompressorRatio);
}

void trackSetCompressorRatio(int trackId, float value)
{
    auto track = findTrackData(trackId);
    if (!track)
        return;

    float clamped = std::clamp(value, kMinCompressorRatio, kMaxCompressorRatio);
    track->compressorRatio.store(clamped, std::memory_order_relaxed);
}

float trackGetCompressorAttack(int trackId)
{
    auto track = findTrackData(trackId);
    if (!track)
        return kDefaultCompressorAttack;

    float value = track->compressorAttack.load(std::memory_order_relaxed);
    return std::clamp(value, kMinCompressorAttack, kMaxCompressorAttack);
}

void trackSetCompressorAttack(int trackId, float value)
{
    auto track = findTrackData(trackId);
    if (!track)
        return;

    float clamped = std::clamp(value, kMinCompressorAttack, kMaxCompressorAttack);
    track->compressorAttack.store(clamped, std::memory_order_relaxed);
}

float trackGetCompressorRelease(int trackId)
{
    auto track = findTrackData(trackId);
    if (!track)
        return kDefaultCompressorRelease;

    float value = track->compressorRelease.load(std::memory_order_relaxed);
    return std::clamp(value, kMinCompressorRelease, kMaxCompressorRelease);
}

void trackSetCompressorRelease(int trackId, float value)
{
    auto track = findTrackData(trackId);
    if (!track)
        return;

    float clamped = std::clamp(value, kMinCompressorRelease, kMaxCompressorRelease);
    track->compressorRelease.store(clamped, std::memory_order_relaxed);
}

bool trackGetSidechainEnabled(int trackId)
{
    auto track = findTrackData(trackId);
    if (!track)
        return false;

    return track->sidechainEnabled.load(std::memory_order_relaxed);
}

void trackSetSidechainEnabled(int trackId, bool enabled)
{
    auto track = findTrackData(trackId);
    if (!track)
        return;

    track->sidechainEnabled.store(enabled, std::memory_order_relaxed);
    if (enabled) {
        std::unique_lock<std::shared_mutex> lock(gTrackMutex);
        auto& order = track->track.fxOrder;
        if (std::find(order.begin(), order.end(), "kj:sidechain") == order.end()) order.push_back("kj:sidechain");
    }
}

int trackGetSidechainSourceTrack(int trackId)
{
    auto track = findTrackData(trackId);
    if (!track)
        return kDefaultSidechainSourceTrack;

    return track->sidechainSourceTrackId.load(std::memory_order_relaxed);
}

void trackSetSidechainSourceTrack(int trackId, int sourceTrackId)
{
    auto track = findTrackData(trackId);
    if (!track)
        return;

    track->sidechainSourceTrackId.store(sourceTrackId, std::memory_order_relaxed);
}

float trackGetSidechainAmount(int trackId)
{
    auto track = findTrackData(trackId);
    if (!track)
        return kDefaultSidechainAmount;

    float value = track->sidechainAmount.load(std::memory_order_relaxed);
    return std::clamp(value, kMinSidechainAmount, kMaxSidechainAmount);
}

void trackSetSidechainAmount(int trackId, float value)
{
    auto track = findTrackData(trackId);
    if (!track)
        return;

    float clamped = std::clamp(value, kMinSidechainAmount, kMaxSidechainAmount);
    track->sidechainAmount.store(clamped, std::memory_order_relaxed);
}

float trackGetSidechainAttack(int trackId)
{
    auto track = findTrackData(trackId);
    if (!track)
        return kDefaultSidechainAttack;

    float value = track->sidechainAttack.load(std::memory_order_relaxed);
    return std::clamp(value, kMinSynthEnvelopeTime, kMaxSynthEnvelopeTime);
}

void trackSetSidechainAttack(int trackId, float value)
{
    auto track = findTrackData(trackId);
    if (!track)
        return;

    float clamped = std::clamp(value, kMinSynthEnvelopeTime, kMaxSynthEnvelopeTime);
    track->sidechainAttack.store(clamped, std::memory_order_relaxed);
}

float trackGetSidechainRelease(int trackId)
{
    auto track = findTrackData(trackId);
    if (!track)
        return kDefaultSidechainRelease;

    float value = track->sidechainRelease.load(std::memory_order_relaxed);
    return std::clamp(value, kMinSynthEnvelopeTime, kMaxSynthEnvelopeTime);
}

void trackSetSidechainRelease(int trackId, float value)
{
    auto track = findTrackData(trackId);
    if (!track)
        return;

    float clamped = std::clamp(value, kMinSynthEnvelopeTime, kMaxSynthEnvelopeTime);
    track->sidechainRelease.store(clamped, std::memory_order_relaxed);
}

void trackSetMute(int id,bool value){std::unique_lock<std::shared_mutex> lock(gTrackMutex);for(auto& t:gTracks)if(t->track.id==id)t->track.mute=value;}
void trackSetSolo(int id,bool value){std::unique_lock<std::shared_mutex> lock(gTrackMutex);for(auto& t:gTracks)if(t->track.id==id)t->track.solo=value;}
void trackSetInputDevice(int id,std::string device,std::string name){std::unique_lock<std::shared_mutex> lock(gTrackMutex);for(auto& t:gTracks)if(t->track.id==id){t->track.inputDeviceId=std::move(device);t->track.inputDeviceName=std::move(name);t->track.inputMonitor=false;t->track.inputChannel=0;t->track.inputStereo=false;return;}}
void trackSetInputChannels(int id,int first,bool stereo){if(first<0||first>=64)return;std::unique_lock<std::shared_mutex> lock(gTrackMutex);for(auto& t:gTracks)if(t->track.id==id){t->track.inputChannel=first;t->track.inputStereo=stereo;}}
void trackSetInputMonitor(int id,bool value){std::unique_lock<std::shared_mutex> lock(gTrackMutex);for(auto& t:gTracks)if(t->track.id==id)t->track.inputMonitor=value&&!t->track.inputDeviceId.empty();}
