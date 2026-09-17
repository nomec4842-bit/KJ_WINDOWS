#pragma once

#include "core/tracks.h"

#include <string>

SynthWaveType trackGetSynthWaveType(int trackId);
void trackSetSynthWaveType(int trackId, SynthWaveType type);

float trackGetSynthFormant(int trackId);
void trackSetSynthFormant(int trackId, float value);

float trackGetSynthResonance(int trackId);
void trackSetSynthResonance(int trackId, float value);

float trackGetSynthFeedback(int trackId);
void trackSetSynthFeedback(int trackId, float value);

float trackGetSynthPitch(int trackId);
void trackSetSynthPitch(int trackId, float value);

float trackGetSynthPitchRange(int trackId);
void trackSetSynthPitchRange(int trackId, float value);

float trackGetSynthAttack(int trackId);
void trackSetSynthAttack(int trackId, float value);

float trackGetSynthDecay(int trackId);
void trackSetSynthDecay(int trackId, float value);

float trackGetSynthSustain(int trackId);
void trackSetSynthSustain(int trackId, float value);

float trackGetSynthRelease(int trackId);
void trackSetSynthRelease(int trackId, float value);

bool trackGetSynthPhaseSync(int trackId);
void trackSetSynthPhaseSync(int trackId, bool enabled);

bool trackGetSynthThreeOscEnabled(int trackId);
void trackSetSynthThreeOscEnabled(int trackId, bool enabled);

float trackGetSynthOscFormant(int trackId, int oscIndex);
void trackSetSynthOscFormant(int trackId, int oscIndex, float value);

float trackGetSynthOscResonance(int trackId, int oscIndex);
void trackSetSynthOscResonance(int trackId, int oscIndex, float value);

float trackGetSynthOscFeedback(int trackId, int oscIndex);
void trackSetSynthOscFeedback(int trackId, int oscIndex, float value);

float trackGetSynthOscPitch(int trackId, int oscIndex);
void trackSetSynthOscPitch(int trackId, int oscIndex, float value);

float trackGetSynthOscPitchRange(int trackId, int oscIndex);
void trackSetSynthOscPitchRange(int trackId, int oscIndex, float value);

float trackGetSynthOscAttack(int trackId, int oscIndex);
void trackSetSynthOscAttack(int trackId, int oscIndex, float value);

float trackGetSynthOscDecay(int trackId, int oscIndex);
void trackSetSynthOscDecay(int trackId, int oscIndex, float value);

float trackGetSynthOscSustain(int trackId, int oscIndex);
void trackSetSynthOscSustain(int trackId, int oscIndex, float value);

float trackGetSynthOscRelease(int trackId, int oscIndex);
void trackSetSynthOscRelease(int trackId, int oscIndex, float value);

float trackGetSynthOscWavetablePosition(int trackId, int oscIndex);
void trackSetSynthOscWavetablePosition(int trackId, int oscIndex, float value);
float trackGetSynthOscWavetableMix(int trackId, int oscIndex);
void trackSetSynthOscWavetableMix(int trackId, int oscIndex, float value);
bool trackGetSynthOscWavetableEnabled(int trackId, int oscIndex);
void trackSetSynthOscWavetableEnabled(int trackId, int oscIndex, bool enabled);

float trackGetLfoRate(int trackId, int index);
void trackSetLfoRate(int trackId, int index, float value);

LfoShape trackGetLfoShape(int trackId, int index);
void trackSetLfoShape(int trackId, int index, LfoShape shape);

float trackGetLfoDeform(int trackId, int index);
void trackSetLfoDeform(int trackId, int index, float value);

const char* lfoShapeToString(LfoShape shape);
LfoShape lfoShapeFromString(const std::string& text);
