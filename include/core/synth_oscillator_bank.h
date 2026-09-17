#pragma once

#include "core/tracks.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace kj
{
constexpr double kOscTwoPi = 6.28318530717958647692;

inline double oscillatorWave(SynthWaveType type, double phase)
{
    double t = phase / kOscTwoPi;
    switch (type)
    {
    case SynthWaveType::Sine: return std::sin(phase);
    case SynthWaveType::Square: return t < 0.5 ? 1.0 : -1.0;
    case SynthWaveType::Saw: return 2.0 * t - 1.0;
    case SynthWaveType::Triangle: return 2.0 * (1.0 - std::abs(2.0 * t - 1.0)) - 1.0;
    }
    return 0.0;
}

// Fixed tables are prepared at program startup, never allocated in the render loop.
inline const auto kOscWaveTables = [] {
    std::array<std::array<double, 2048>, 4> tables{};
    for (size_t wave = 0; wave < tables.size(); ++wave)
        for (size_t i = 0; i < tables[wave].size(); ++i)
            tables[wave][i] = oscillatorWave(static_cast<SynthWaveType>(wave),
                kOscTwoPi * static_cast<double>(i) / tables[wave].size());
    return tables;
}();

inline double wavetableWave(SynthWaveType wave, double phase)
{
    const size_t waveIndex = std::min(static_cast<size_t>(wave), kOscWaveTables.size() - 1);
    const auto& table = kOscWaveTables[waveIndex];
    double position = phase / kOscTwoPi * table.size();
    size_t index = static_cast<size_t>(position) % table.size();
    double fraction = position - std::floor(position);
    return table[index] + (table[(index + 1) % table.size()] - table[index]) * fraction;
}

// Position scans successive tables starting at the selected waveform.
inline double wavetableOutput(SynthWaveType wave, double phase, const SynthOscillatorSettings& settings)
{
    double position = std::clamp(static_cast<double>(settings.wavetablePosition), 0.0, 3.0);
    size_t offset = static_cast<size_t>(position);
    size_t first = (static_cast<size_t>(wave) + offset) % kOscWaveTables.size();
    size_t second = (first + 1) % kOscWaveTables.size();
    double a = wavetableWave(static_cast<SynthWaveType>(first), phase);
    double b = wavetableWave(static_cast<SynthWaveType>(second), phase);
    double table = a + (b - a) * (position - offset);
    double dry = oscillatorWave(wave, phase);
    return dry + (table - dry) * std::clamp(static_cast<double>(settings.wavetableMix), 0.0, 1.0);
}

struct SynthOscillatorState
{
    double phase = 0.0;
    double lastOutput = 0.0;
    double pitchEnvelope = 0.0;
    double frequencyNote = -1000.0;
    double frequencyRate = 0.0;
    double frequency = 0.0;
    double filterRate = -1.0;
    double filterFormant = -1.0;
    double filterResonance = -1.0;
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
    double z1 = 0.0, z2 = 0.0;

    double filter(double input, double sr, double formant, double resonance)
    {
        if (sr != filterRate || std::abs(formant - filterFormant) > 1e-4 ||
            std::abs(resonance - filterResonance) > 1e-4)
        {
            const double maxFrequency = std::max(200.0, std::min(sr * 0.45, 8000.0));
            const double frequency = std::min(sr * 0.45, 200.0 * std::pow(maxFrequency / 200.0, formant));
            const double w = kOscTwoPi * frequency / sr;
            const double cosine = std::cos(w);
            const double alpha = std::sin(w) / (2.0 * (0.5 + resonance * 11.5));
            const double inverseA0 = 1.0 / (1.0 + alpha);
            b0 = (1.0 - cosine) * 0.5 * inverseA0;
            b1 = (1.0 - cosine) * inverseA0;
            b2 = b0;
            a1 = -2.0 * cosine * inverseA0;
            a2 = (1.0 - alpha) * inverseA0;
            filterRate = sr;
            filterFormant = formant;
            filterResonance = resonance;
        }
        const double output = b0 * input + z1;
        z1 = b1 * input - a1 * output + z2;
        z2 = b2 * input - a2 * output;
        return output;
    }
};

struct SynthOscillatorBank
{
    std::array<SynthOscillatorState, kSynthOscillatorCount> oscillators{};

    void noteOn(bool phaseSync)
    {
        for (auto& oscillator : oscillators)
        {
            oscillator.pitchEnvelope = 1.0;
            if (phaseSync)
            {
                oscillator.phase = 0.0;
                oscillator.lastOutput = 0.0;
                oscillator.z1 = oscillator.z2 = 0.0;
            }
        }
    }

    double render(const std::array<SynthOscillatorSettings, kSynthOscillatorCount>& settings,
                  SynthWaveType wave, int note, double sampleRate, double stepPitch,
                  double pitchMod = 0.0, double rangeMod = 0.0, double feedbackMod = 0.0,
                  double formantMod = 0.0, double resonanceMod = 0.0)
    {
        const double sr = sampleRate > 0.0 && std::isfinite(sampleRate) ? sampleRate : 44100.0;
        double mixed = 0.0;
        for (size_t i = 0; i < oscillators.size(); ++i)
        {
            auto& oscillator = oscillators[i];
            const auto& params = settings[i];
            const double range = std::clamp(params.pitchRange + rangeMod - 1.0, 0.0, 23.0);
            const double pitchedNote = std::clamp(note + params.pitch + pitchMod + stepPitch +
                oscillator.pitchEnvelope * range, 0.0, 127.0);
            if (pitchedNote != oscillator.frequencyNote || sr != oscillator.frequencyRate) {
                oscillator.frequencyNote = pitchedNote;
                oscillator.frequencyRate = sr;
                oscillator.frequency = std::min(sr * 0.45, 440.0 * std::pow(2.0, (pitchedNote - 69.0) / 12.0));
            }
            const double frequency = oscillator.frequency;
            double output;
            if (params.wavetableEnabled)
            {
                output = wavetableOutput(wave, oscillator.phase, params);
            }
            else output = oscillatorWave(wave, oscillator.phase);

            const double feedback = std::clamp(params.feedback + feedbackMod, 0.0, 0.99);
            output = output * (1.0 - feedback) + oscillator.lastOutput * feedback;
            oscillator.lastOutput = output;
            const double formant = std::clamp(params.formant + formantMod, 0.0, 1.0);
            const double resonance = std::clamp(params.resonance + resonanceMod, 0.0, 1.0);
            const double filtered = oscillator.filter(output, sr, formant, resonance);
            mixed += filtered * (1.0 - formant) + output * formant;
            oscillator.phase += kOscTwoPi * frequency / sr;
            // Frequency is capped below Nyquist, so at most one wrap is needed.
            if (oscillator.phase >= kOscTwoPi) oscillator.phase -= kOscTwoPi;
            const double pitchTime = 0.04 + range / 23.0 * 0.26;
            oscillator.pitchEnvelope = std::max(0.0, oscillator.pitchEnvelope - 1.0 / (pitchTime * sr));
        }
        return mixed / static_cast<double>(oscillators.size());
    }
};
} // namespace kj
