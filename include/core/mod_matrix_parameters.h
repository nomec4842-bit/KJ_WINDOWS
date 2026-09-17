#pragma once

#include "core/tracks.h"

#include <cstdint>

enum class ModMatrixParameter
{
    Volume,
    Pan,
    SynthPitch,
    SynthFormant,
    SynthResonance,
    SynthFeedback,
    SynthPitchRange,
    SynthAttack,
    SynthDecay,
    SynthSustain,
    SynthRelease,
    SynthOsc1Wavetable,
    SynthOsc1Formant,
    SynthOsc1Resonance,
    SynthOsc1Feedback,
    SynthOsc1Pitch,
    SynthOsc1PitchRange,
    SynthOsc1Attack,
    SynthOsc1Decay,
    SynthOsc1Sustain,
    SynthOsc1Release,
    SynthOsc2Wavetable,
    SynthOsc2Formant,
    SynthOsc2Resonance,
    SynthOsc2Feedback,
    SynthOsc2Pitch,
    SynthOsc2PitchRange,
    SynthOsc2Attack,
    SynthOsc2Decay,
    SynthOsc2Sustain,
    SynthOsc2Release,
    SynthOsc3Wavetable,
    SynthOsc3Formant,
    SynthOsc3Resonance,
    SynthOsc3Feedback,
    SynthOsc3Pitch,
    SynthOsc3PitchRange,
    SynthOsc3Attack,
    SynthOsc3Decay,
    SynthOsc3Sustain,
    SynthOsc3Release,
    SampleAttack,
    SampleRelease,
    DelayMix,
    CompressorThreshold,
    CompressorRatio,
    SynthOsc1WavetablePosition,
    SynthOsc1WavetableMix,
    SynthOsc2WavetablePosition,
    SynthOsc2WavetableMix,
    SynthOsc3WavetablePosition,
    SynthOsc3WavetableMix,
};

using ModParameterGetter = float (*)(int);
using ModParameterSetter = void (*)(int, float);

struct ModParameterInfo
{
    const wchar_t* label;
    ModParameterGetter getter;
    ModParameterSetter setter;
    float minValue;
    float maxValue;
    uint32_t trackTypeMask;
};

constexpr uint32_t modMatrixTrackTypeToMask(TrackType type)
{
    return 1u << static_cast<uint32_t>(type);
}

const ModParameterInfo* modMatrixGetParameterInfo(int index);
int modMatrixGetParameterCount();
int modMatrixGetParameterIndex(ModMatrixParameter parameter);
bool modMatrixParameterSupportsTrackType(const ModParameterInfo& info, TrackType trackType);
bool modMatrixParameterAvailableForTrack(int index, const Track& track);

float modMatrixClampNormalized(float normalized);
float modMatrixNormalizedToValue(float normalized, const ModParameterInfo& info);
float modMatrixValueToNormalized(float value, const ModParameterInfo& info);
