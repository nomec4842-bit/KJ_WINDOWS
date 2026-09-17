#pragma once

#include "core/sequencer.h"
#include "core/tracks.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <utility>
#include <vector>

namespace track_internal
{

inline constexpr float kMinVolume = 0.0f;
inline constexpr float kMaxVolume = 1.0f;
inline constexpr float kMinPan = -1.0f;
inline constexpr float kMaxPan = 1.0f;
inline constexpr float kMinEqGainDb = -12.0f;
inline constexpr float kMaxEqGainDb = 12.0f;
inline constexpr float kMinDelayTimeMs = 10.0f;
inline constexpr float kMaxDelayTimeMs = 2000.0f;
inline constexpr float kDefaultDelayTimeMs = 350.0f;
inline constexpr float kMinDelayFeedback = 0.0f;
inline constexpr float kMaxDelayFeedback = 0.95f;
inline constexpr float kDefaultDelayFeedback = 0.35f;
inline constexpr float kMinDelayMix = 0.0f;
inline constexpr float kMaxDelayMix = 1.0f;
inline constexpr float kDefaultDelayMix = 0.4f;
inline constexpr float kMinCompressorThresholdDb = -60.0f;
inline constexpr float kMaxCompressorThresholdDb = 0.0f;
inline constexpr float kDefaultCompressorThresholdDb = -12.0f;
inline constexpr float kMinCompressorRatio = 1.0f;
inline constexpr float kMaxCompressorRatio = 20.0f;
inline constexpr float kDefaultCompressorRatio = 4.0f;
inline constexpr float kMinCompressorAttack = 0.001f;
inline constexpr float kMaxCompressorAttack = 1.0f;
inline constexpr float kDefaultCompressorAttack = 0.01f;
inline constexpr float kMinCompressorRelease = 0.01f;
inline constexpr float kMaxCompressorRelease = 4.0f;
inline constexpr float kDefaultCompressorRelease = 0.2f;
inline constexpr float kMinSidechainAmount = 0.0f;
inline constexpr float kMaxSidechainAmount = 1.0f;
inline constexpr float kDefaultSidechainAmount = 1.0f;
inline constexpr float kDefaultSidechainAttack = 0.01f;
inline constexpr float kDefaultSidechainRelease = 0.3f;
inline constexpr int kDefaultSidechainSourceTrack = -1;
inline constexpr float kMinFormant = 0.0f;
inline constexpr float kMaxFormant = 1.0f;
inline constexpr float kDefaultFormant = 0.5f;
inline constexpr float kMinResonance = 0.0f;
inline constexpr float kMaxResonance = 1.0f;
inline constexpr float kDefaultResonance = 0.2f;
inline constexpr float kMinFeedback = 0.0f;
inline constexpr float kMaxFeedback = 1.0f;
inline constexpr float kDefaultFeedback = 0.0f;
inline constexpr float kMinPitch = -24.0f;
inline constexpr float kMaxPitch = 24.0f;
inline constexpr float kDefaultPitch = 0.0f;
inline constexpr float kMinPitchRange = 1.0f;
inline constexpr float kMaxPitchRange = 24.0f;
inline constexpr float kDefaultPitchRange = 12.0f;
inline constexpr float kMinSynthEnvelopeTime = 0.0f;
inline constexpr float kMaxSynthEnvelopeTime = 4.0f;
inline constexpr float kMinSynthSustain = 0.0f;
inline constexpr float kMaxSynthSustain = 1.0f;
inline constexpr float kDefaultSynthAttack = 0.01f;
inline constexpr float kDefaultSynthDecay = 0.2f;
inline constexpr float kDefaultSynthSustain = 0.8f;
inline constexpr float kDefaultSynthRelease = 0.3f;
inline constexpr bool kDefaultSynthWavetableEnabled = false;
inline constexpr float kMinSampleEnvelopeTime = 0.0f;
inline constexpr float kMaxSampleEnvelopeTime = 4.0f;
inline constexpr float kDefaultSampleAttack = 0.005f;
inline constexpr float kDefaultSampleRelease = 0.3f;
inline constexpr float kMinLfoRateHz = 0.05f;
inline constexpr float kMaxLfoRateHz = 20.0f;
inline constexpr float kDefaultLfoDeform = 0.0f;
inline constexpr auto kDefaultLfoRatesHz = [] {std::array<float,kMaxLfos> rates{};for(int i=0;i<kMaxLfos;++i)rates[i]=i==0?.5f:i==2?2.f:1.f;return rates;}();
inline constexpr std::array<LfoShape, kMaxLfos> kDefaultLfoShapes{};
inline constexpr int kMinMidiNote = 0;
inline constexpr int kMaxMidiNote = 127;
inline constexpr int kDefaultMidiNote = 69; // A4
inline constexpr int kMinMidiChannel = 1;
inline constexpr int kMaxMidiChannel = 16;
inline constexpr int kDefaultMidiChannel = 1;
inline constexpr int kDefaultMidiPort = -1;

inline int clampMidiNote(int note)
{
    return std::clamp(note, kMinMidiNote, kMaxMidiNote);
}

inline float clampLfoRate(float value)
{
    return std::clamp(value, kMinLfoRateHz, kMaxLfoRateHz);
}

struct TrackData
{
    explicit TrackData(Track baseTrack);

    Track track;
    kj::Vst3RackState vst3;
    std::atomic<TrackType> type{TrackType::Synth};
    std::atomic<SynthWaveType> waveType{SynthWaveType::Sine};
    std::atomic<float> volume{1.0f};
    std::atomic<float> pan{0.0f};
    std::atomic<float> lowGainDb{0.0f};
    std::atomic<float> midGainDb{0.0f};
    std::atomic<float> highGainDb{0.0f};
    std::atomic<bool> eqEnabled{false};
    std::array<std::atomic<float>, 3> eqFrequency{{200.0f, 1000.0f, 5000.0f}};
    std::array<std::atomic<int>, 3> eqShape{{1, 0, 0}};
    std::array<std::atomic<float>, 3> eqQ{{0.707f, 0.707f, 0.707f}};
    std::atomic<bool> delayEnabled{false};
    std::atomic<float> delayTimeMs{kDefaultDelayTimeMs};
    std::atomic<float> delayFeedback{kDefaultDelayFeedback};
    std::atomic<float> delayMix{kDefaultDelayMix};
    std::atomic<bool> compressorEnabled{false};
    std::atomic<float> compressorThresholdDb{kDefaultCompressorThresholdDb};
    std::atomic<float> compressorRatio{kDefaultCompressorRatio};
    std::atomic<float> compressorAttack{kDefaultCompressorAttack};
    std::atomic<float> compressorRelease{kDefaultCompressorRelease};
    std::atomic<bool> sidechainEnabled{false};
    std::atomic<int> sidechainSourceTrackId{kDefaultSidechainSourceTrack};
    std::atomic<float> sidechainAmount{kDefaultSidechainAmount};
    std::atomic<float> sidechainAttack{kDefaultSidechainAttack};
    std::atomic<float> sidechainRelease{kDefaultSidechainRelease};
    std::atomic<float> formant{kDefaultFormant};
    std::atomic<float> resonance{kDefaultResonance};
    std::atomic<float> feedback{kDefaultFeedback};
    std::atomic<float> pitch{kDefaultPitch};
    std::atomic<float> pitchRange{kDefaultPitchRange};
    std::atomic<float> synthAttack{kDefaultSynthAttack};
    std::atomic<float> synthDecay{kDefaultSynthDecay};
    std::atomic<float> synthSustain{kDefaultSynthSustain};
    std::atomic<float> synthRelease{kDefaultSynthRelease};
    std::atomic<bool> synthPhaseSync{false};
    std::atomic<bool> synthThreeOscEnabled{false};
    std::array<std::atomic<float>, kSynthOscillatorCount> synthOscFormant;
    std::array<std::atomic<float>, kSynthOscillatorCount> synthOscResonance;
    std::array<std::atomic<float>, kSynthOscillatorCount> synthOscFeedback;
    std::array<std::atomic<float>, kSynthOscillatorCount> synthOscPitch;
    std::array<std::atomic<float>, kSynthOscillatorCount> synthOscPitchRange;
    std::array<std::atomic<float>, kSynthOscillatorCount> synthOscAttack;
    std::array<std::atomic<float>, kSynthOscillatorCount> synthOscDecay;
    std::array<std::atomic<float>, kSynthOscillatorCount> synthOscSustain;
    std::array<std::atomic<float>, kSynthOscillatorCount> synthOscRelease;
    std::array<std::atomic<bool>, kSynthOscillatorCount> synthOscWavetableEnabled;
    std::array<std::atomic<float>, kSynthOscillatorCount> synthOscWavetablePosition;
    std::array<std::atomic<float>, kSynthOscillatorCount> synthOscWavetableMix;
    std::atomic<float> sampleAttack{kDefaultSampleAttack};
    std::atomic<float> sampleRelease{kDefaultSampleRelease};
    std::array<std::atomic<float>, kDefaultLfoRatesHz.size()> lfoRateHz;
    std::array<std::atomic<LfoShape>, kDefaultLfoShapes.size()> lfoShape;
    std::array<std::atomic<float>, kDefaultLfoRatesHz.size()> lfoDeform;
    std::array<std::atomic<bool>, kMaxSequencerSteps> steps{};
    std::array<std::atomic<int>, kMaxSequencerSteps> notes{};
    struct StepNoteEntry
    {
        int midiNote = kDefaultMidiNote;
        float velocity = kTrackStepVelocityMax;
        bool sustain = false;
    };
    std::array<std::vector<StepNoteEntry>, kMaxSequencerSteps> stepNotes{};
    std::array<std::atomic<float>, kMaxSequencerSteps> stepVelocity{};
    std::array<std::atomic<float>, kMaxSequencerSteps> stepPan{};
    std::array<std::atomic<float>, kMaxSequencerSteps> stepPitch{};
    std::atomic<int> stepCount{1};
    std::atomic<int> maxInitializedStepCount{kSequencerStepsPerPage};
    std::shared_ptr<const SampleBuffer> sampleBuffer;
    std::atomic<bool> sampleDrumMode{false};
    std::mutex noteMutex;
    std::atomic<int> midiChannel{kDefaultMidiChannel};
    std::atomic<int> midiPort{kDefaultMidiPort};
    std::wstring midiPortName;
    std::mutex midiPortMutex;
};

inline TrackData::TrackData(Track baseTrack)
    : track(std::move(baseTrack))
{
    if (track.name.empty())
    {
        track.name = "Track " + std::to_string(track.id);
    }

    track.type = TrackType::Synth;
    track.synthWaveType = SynthWaveType::Sine;
    track.volume = 1.0f;
    track.pan = 0.0f;
    track.lowGainDb = 0.0f;
    track.midGainDb = 0.0f;
    track.highGainDb = 0.0f;
    track.eqEnabled = false;
    track.delayEnabled = false;
    track.delayTimeMs = kDefaultDelayTimeMs;
    track.delayFeedback = kDefaultDelayFeedback;
    track.delayMix = kDefaultDelayMix;
    track.compressorEnabled = false;
    track.compressorThresholdDb = kDefaultCompressorThresholdDb;
    track.compressorRatio = kDefaultCompressorRatio;
    track.compressorAttack = kDefaultCompressorAttack;
    track.compressorRelease = kDefaultCompressorRelease;
    track.sidechainEnabled = false;
    track.sidechainSourceTrackId = kDefaultSidechainSourceTrack;
    track.sidechainAmount = kDefaultSidechainAmount;
    track.sidechainAttack = kDefaultSidechainAttack;
    track.sidechainRelease = kDefaultSidechainRelease;
    track.formant = kDefaultFormant;
    track.resonance = kDefaultResonance;
    track.feedback = kDefaultFeedback;
    track.pitch = kDefaultPitch;
    track.pitchRange = kDefaultPitchRange;
    track.synthAttack = kDefaultSynthAttack;
    track.synthDecay = kDefaultSynthDecay;
    track.synthSustain = kDefaultSynthSustain;
    track.synthRelease = kDefaultSynthRelease;
    track.synthPhaseSync = false;
    track.synthThreeOscEnabled = false;
    for (auto& osc : track.synthOscillators)
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
    track.sampleAttack = kDefaultSampleAttack;
    track.sampleRelease = kDefaultSampleRelease;
    for (size_t i = 0; i < track.lfoSettings.size(); ++i)
    {
        track.lfoSettings[i].rateHz = kDefaultLfoRatesHz[i];
        track.lfoSettings[i].shape = kDefaultLfoShapes[i];
        track.lfoSettings[i].deform = kDefaultLfoDeform;
    }
    track.midiChannel = kDefaultMidiChannel;
    track.midiPort = kDefaultMidiPort;
    track.midiPortName.clear();

    stepCount.store(kSequencerStepsPerPage, std::memory_order_relaxed);
    for (size_t i = 0; i < lfoRateHz.size(); ++i)
    {
        lfoRateHz[i].store(kDefaultLfoRatesHz[i], std::memory_order_relaxed);
        lfoShape[i].store(kDefaultLfoShapes[i], std::memory_order_relaxed);
        lfoDeform[i].store(kDefaultLfoDeform, std::memory_order_relaxed);
    }
    synthThreeOscEnabled.store(false, std::memory_order_relaxed);
    for (size_t i = 0; i < kSynthOscillatorCount; ++i)
    {
        synthOscFormant[i].store(kDefaultFormant, std::memory_order_relaxed);
        synthOscResonance[i].store(kDefaultResonance, std::memory_order_relaxed);
        synthOscFeedback[i].store(kDefaultFeedback, std::memory_order_relaxed);
        synthOscPitch[i].store(kDefaultPitch, std::memory_order_relaxed);
        synthOscPitchRange[i].store(kDefaultPitchRange, std::memory_order_relaxed);
        synthOscAttack[i].store(kDefaultSynthAttack, std::memory_order_relaxed);
        synthOscDecay[i].store(kDefaultSynthDecay, std::memory_order_relaxed);
        synthOscSustain[i].store(kDefaultSynthSustain, std::memory_order_relaxed);
        synthOscRelease[i].store(kDefaultSynthRelease, std::memory_order_relaxed);
        synthOscWavetableEnabled[i].store(kDefaultSynthWavetableEnabled, std::memory_order_relaxed);
        synthOscWavetablePosition[i].store(0.0f, std::memory_order_relaxed);
        synthOscWavetableMix[i].store(1.0f, std::memory_order_relaxed);
    }
    for (int i = 0; i < kMaxSequencerSteps; ++i)
    {
        steps[i].store(false, std::memory_order_relaxed);
        notes[i].store(kDefaultMidiNote, std::memory_order_relaxed);
        stepVelocity[i].store(kTrackStepVelocityMax, std::memory_order_relaxed);
        stepPan[i].store(0.0f, std::memory_order_relaxed);
        stepPitch[i].store(0.0f, std::memory_order_relaxed);
    }
}

std::shared_ptr<TrackData> makeTrackData(const std::string& name);
std::shared_ptr<TrackData> findTrackData(int trackId);

extern std::vector<std::shared_ptr<TrackData>> gTracks;
extern std::shared_mutex gTrackMutex;
extern int gNextTrackId;

} // namespace track_internal
