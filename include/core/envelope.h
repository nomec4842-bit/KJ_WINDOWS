#pragma once

#include <algorithm>
#include <cmath>

enum class EnvelopeStage { Idle, Attack, Decay, Sustain, Release };

struct EnvelopeProgress
{
    EnvelopeStage stage = EnvelopeStage::Idle;
    double start = 0.0;
    double phase = 0.0;
};

// Times describe the entire ramp, including release from a partially open gate.
// One-shot AR goes directly from attack to release; gated ADSR waits for note-off.
inline double advanceEnvelope(EnvelopeStage& stage, double value, double attack,
                              double decay, double sustain, double release,
                              double sampleRate, EnvelopeProgress& progress,
                              bool oneShot = false)
{
    const double sr = std::isfinite(sampleRate) && sampleRate > 0.0 ? sampleRate : 44100.0;
    value = std::isfinite(value) ? std::clamp(value, 0.0, 1.0) : 0.0;
    sustain = std::isfinite(sustain) ? std::clamp(sustain, 0.0, 1.0) : 0.0;
    for (int transition = 0; transition < 4; ++transition)
    {
        if (stage == EnvelopeStage::Idle) { progress = {}; return 0.0; }
        if (stage == EnvelopeStage::Sustain) return sustain;
        if (progress.stage != stage)
        {
            progress.stage = stage;
            progress.start = value;
            progress.phase = 0.0;
        }
        double duration = attack;
        double target = 1.0;
        EnvelopeStage next = oneShot ? EnvelopeStage::Release : EnvelopeStage::Decay;
        if (stage == EnvelopeStage::Decay)
        {
            duration = decay;
            target = sustain;
            next = EnvelopeStage::Sustain;
        }
        else if (stage == EnvelopeStage::Release)
        {
            duration = release;
            target = 0.0;
            next = EnvelopeStage::Idle;
        }
        if (!std::isfinite(duration) || duration <= 0.0)
        {
            value = target;
            stage = next;
            continue;
        }
        progress.phase = std::min(1.0, progress.phase + 1.0 / std::max(1.0, duration * sr));
        value = progress.start + (target - progress.start) * progress.phase;
        if (progress.phase >= 1.0 - 1e-12)
        {
            value = target;
            stage = next;
        }
        return value;
    }
    return value;
}
