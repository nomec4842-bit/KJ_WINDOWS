#pragma once
#include "core/lfo_config.h"
#include "core/vst3_state.h"

#include <array>
#include <cstddef>
#include <string>
#include <vector>
#include <map>

struct SampleBuffer;

namespace kj
{
}

enum class TrackType
{
    Synth,
    Sample,
    MidiOut,
    Vst3,
    AudioIn,
};

enum class SynthWaveType
{
    Sine,
    Square,
    Saw,
    Triangle,
};

enum class LfoShape
{
    Sine,
    Triangle,
    Saw,
    Square,
};

constexpr size_t kSynthOscillatorCount = 3;

struct LfoSettings
{
    float rateHz = 1.0f;
    LfoShape shape = LfoShape::Sine;
    float deform = 0.0f;
};

struct SynthOscillatorSettings
{
    float formant = 0.5f;
    float resonance = 0.2f;
    float feedback = 0.0f;
    float pitch = 0.0f;
    float pitchRange = 12.0f;
    float attack = 0.01f;
    float decay = 0.2f;
    float sustain = 0.8f;
    float release = 0.3f;
    bool wavetableEnabled = false;
    float wavetablePosition = 0.0f;
    float wavetableMix = 1.0f;
};

struct DrumSettings { float attack=.005f, release=.3f, pitch=0, pan=0, volume=1; };

struct Track
{
    std::map<int,DrumSettings> drums; // Zero-based sample bank lane; omitted lanes inherit track AR.
    int selectedDrum=0;
    bool mute=false, solo=false;
    std::string inputDeviceId, inputDeviceName;
    int inputChannel=0;
    bool inputStereo=false, inputMonitor=false;

    // Native effects retain their existing per-track controls and DSP state.
    // Plugin entries use their stable slot IDs in this shared signal order.
    std::vector<std::string> fxOrder;
    int id;
    std::string name;
    TrackType type = TrackType::Synth;
    SynthWaveType synthWaveType = SynthWaveType::Sine;
    float volume = 1.0f;
    float pan = 0.0f;
    float lowGainDb = 0.0f;
    float midGainDb = 0.0f;
    float highGainDb = 0.0f;
    bool eqEnabled = false;
    std::array<float, 3> eqFrequency{{200.0f, 1000.0f, 5000.0f}};
    std::array<int, 3> eqShape{{1, 0, 0}}; // New EQs use a broad bass shelf; project loading preserves saved shapes.
    std::array<float, 3> eqQ{{0.707f, 0.707f, 0.707f}};
    bool delayEnabled = false;
    float delayTimeMs = 350.0f;
    float delayFeedback = 0.35f;
    float delayMix = 0.4f;
    bool compressorEnabled = false;
    float compressorThresholdDb = -12.0f;
    float compressorRatio = 4.0f;
    float compressorAttack = 0.01f;
    float compressorRelease = 0.2f;
    bool sidechainEnabled = false;
    int sidechainSourceTrackId = -1;
    float sidechainAmount = 1.0f;
    float sidechainAttack = 0.01f;
    float sidechainRelease = 0.3f;
    float formant = 0.5f;
    float resonance = 0.2f;
    float feedback = 0.0f;
    float pitch = 0.0f;
    float pitchRange = 12.0f;
    float synthAttack = 0.01f;
    float synthDecay = 0.2f;
    float synthSustain = 0.8f;
    float synthRelease = 0.3f;
    bool synthPhaseSync = false;
    bool synthThreeOscEnabled = false;
    std::array<SynthOscillatorSettings, kSynthOscillatorCount> synthOscillators{};
    float sampleAttack = 0.005f;
    float sampleRelease = 0.3f;
    std::array<LfoSettings, kMaxLfos> lfoSettings{};
    int midiChannel = 1;
    int midiPort = -1;
    std::wstring midiPortName;
};

constexpr float kTrackStepVelocityMin = 0.0f;
constexpr float kTrackStepVelocityMax = 1.0f;
constexpr float kTrackStepPanMin = -1.0f;
constexpr float kTrackStepPanMax = 1.0f;
constexpr float kTrackStepPitchMin = -12.0f;
constexpr float kTrackStepPitchMax = 12.0f;

struct StepNoteInfo
{
    int midiNote = 0;
    float velocity = kTrackStepVelocityMax;
    bool sustain = false;
};

void initTracks();
std::vector<std::string> trackGetFxOrder(int trackId);
void trackSetFxOrder(int trackId, std::vector<std::string> order);
kj::Vst3RackState trackGetVst3State(int trackId);
void trackSetVst3State(int trackId, kj::Vst3RackState state);
Track addTrack(const std::string& name = {});
std::vector<Track> getTracks();
size_t getTrackCount();

void trackSetName(int trackId, const std::string& name);

TrackType trackGetType(int trackId);
void trackSetType(int trackId, TrackType type);

float trackGetVolume(int trackId);
void trackSetVolume(int trackId, float volume);

float trackGetPan(int trackId);
void trackSetPan(int trackId, float pan);

float trackGetEqLowGain(int trackId);
float trackGetEqFrequency(int trackId, int band);
void trackSetEqFrequency(int trackId, int band, float value);
int trackGetEqShape(int trackId, int band);
void trackSetEqShape(int trackId, int band, int shape);
float trackGetEqQ(int trackId, int band);
void trackSetEqQ(int trackId, int band, float value);
float trackGetEqMidGain(int trackId);
float trackGetEqHighGain(int trackId);

bool trackGetEqEnabled(int trackId);

void trackSetEqLowGain(int trackId, float gainDb);
void trackSetEqMidGain(int trackId, float gainDb);
void trackSetEqHighGain(int trackId, float gainDb);
void trackSetEqEnabled(int trackId, bool enabled);

bool trackGetDelayEnabled(int trackId);
void trackSetDelayEnabled(int trackId, bool enabled);

float trackGetDelayTimeMs(int trackId);
void trackSetDelayTimeMs(int trackId, float value);

float trackGetDelayFeedback(int trackId);
void trackSetDelayFeedback(int trackId, float value);

float trackGetDelayMix(int trackId);
void trackSetDelayMix(int trackId, float value);

bool trackGetCompressorEnabled(int trackId);
void trackSetCompressorEnabled(int trackId, bool enabled);

float trackGetCompressorThresholdDb(int trackId);
void trackSetCompressorThresholdDb(int trackId, float value);

float trackGetCompressorRatio(int trackId);
void trackSetCompressorRatio(int trackId, float value);

float trackGetCompressorAttack(int trackId);
void trackSetCompressorAttack(int trackId, float value);

float trackGetCompressorRelease(int trackId);
void trackSetCompressorRelease(int trackId, float value);

bool trackGetSidechainEnabled(int trackId);
void trackSetSidechainEnabled(int trackId, bool enabled);

int trackGetSidechainSourceTrack(int trackId);
void trackSetSidechainSourceTrack(int trackId, int sourceTrackId);

float trackGetSidechainAmount(int trackId);
void trackSetSidechainAmount(int trackId, float value);

float trackGetSidechainAttack(int trackId);
void trackSetSidechainAttack(int trackId, float value);

float trackGetSidechainRelease(int trackId);
void trackSetSidechainRelease(int trackId, float value);

bool trackGetStepState(int trackId, int stepIndex);
void trackSetStepState(int trackId, int stepIndex, bool enabled);
void trackToggleStepState(int trackId, int stepIndex);
int trackGetStepCount(int trackId);
void trackSetStepCount(int trackId, int count);

int trackGetStepNote(int trackId, int stepIndex);
void trackSetStepNote(int trackId, int stepIndex, int midiNote);
std::vector<int> trackGetStepNotes(int trackId, int stepIndex);
void trackToggleStepNote(int trackId, int stepIndex, int midiNote);

bool trackGetStepNoteSustain(int trackId, int stepIndex, int midiNote);
void trackSetStepNoteSustain(int trackId, int stepIndex, int midiNote, bool sustain);

float trackGetStepVelocity(int trackId, int stepIndex);
void trackSetStepVelocity(int trackId, int stepIndex, float value);
float trackGetStepNoteVelocity(int trackId, int stepIndex, int midiNote);
std::vector<StepNoteInfo> trackGetStepNoteInfo(int trackId, int stepIndex);
void trackSetStepNoteVelocity(int trackId, int stepIndex, int midiNote, float value);

float trackGetStepPan(int trackId, int stepIndex);
void trackSetStepPan(int trackId, int stepIndex, float value);

float trackGetStepPitchOffset(int trackId, int stepIndex);
void trackSetStepPitchOffset(int trackId, int stepIndex, float value);

void trackSetMute(int trackId,bool value);
void trackSetSolo(int trackId,bool value);
void trackSetInputDevice(int trackId,std::string id,std::string name);
void trackSetInputChannels(int trackId,int first,bool stereo);
void trackSetInputMonitor(int trackId,bool enabled);
inline bool trackAudible(const Track& track,bool anySolo){return !track.mute && (!anySolo || track.solo);}
