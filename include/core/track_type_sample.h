#pragma once

#include "core/tracks.h"

#include <memory>
#include <vector>

// Drum lane IDs live above MIDI pitches so switching modes preserves both patterns.
constexpr int kSampleDrumNoteBase = 128;
constexpr int kSampleRootNote = 60; // C4 plays the original sample pitch.
using SampleBankBuffers = std::vector<std::shared_ptr<const SampleBuffer>>;
void sampleSetBankBuffers(std::shared_ptr<const SampleBankBuffers> buffers);
std::shared_ptr<const SampleBankBuffers> sampleGetBankBuffers();
double samplePlaybackIncrement(int note, bool drumMode, int sourceRate, double outputRate, double pitchOffset = 0.0);
bool trackGetSampleDrumMode(int trackId);
void trackSetSampleDrumMode(int trackId, bool drumMode);

float trackGetSampleAttack(int trackId);
void trackSetSampleAttack(int trackId, float value);

float trackGetSampleRelease(int trackId);
void trackSetSampleRelease(int trackId, float value);

std::shared_ptr<const SampleBuffer> trackGetSampleBuffer(int trackId);
void trackSetSampleBuffer(int trackId, std::shared_ptr<const SampleBuffer> buffer);


DrumSettings sampleDrumSettings(const Track& track,int lane);
DrumSettings trackGetDrumSettings(int trackId,int lane);
enum class DrumParameter { Attack, Release, Pitch, Pan, Volume };
void trackSetDrumParameter(int trackId,int lane,DrumParameter parameter,float value);
int trackGetSelectedDrum(int trackId);
void trackSetSelectedDrum(int trackId,int lane);
