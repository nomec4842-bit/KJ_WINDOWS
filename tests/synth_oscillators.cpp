#include "core/synth_oscillator_bank.h"
#include <iostream>
#include <stdexcept>
#include <chrono>

void check(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

int main()
{
    try
    {
        std::array<SynthOscillatorSettings, kSynthOscillatorCount> settings{};
        for (auto& osc : settings)
        {
            osc.pitchRange = 1.0f;
            osc.formant = 1.0f;
            osc.resonance = 0.0f;
            osc.feedback = 0.0f;
        }
        for (double sr : {44100.0, 48000.0, 96000.0})
        {
            kj::SynthOscillatorBank bank;
            bank.noteOn(true);
            settings[0].pitch = 0;
            settings[1].pitch = 12;
            settings[2].pitch = -12;
            const double frequencies[] = {440.0, 880.0, 220.0};
            double amplitudes[3]{};
            for (int frame = 0; frame < static_cast<int>(sr); ++frame)
            {
                double sample = bank.render(settings, SynthWaveType::Sine, 69, sr, 0.0);
                check(std::isfinite(sample) && std::abs(sample) <= 1.000001, "Invalid oscillator mix");
                for (int i = 0; i < 3; ++i)
                    amplitudes[i] += 2.0 * sample * std::sin(kj::kOscTwoPi * frequencies[i] * frame / sr) / sr;
            }
            for (double amplitude : amplitudes)
                check(std::abs(amplitude - 1.0 / 3.0) < 1e-6, "One of the three oscillator pitches is missing");
        }

        settings[0].pitch = settings[1].pitch = settings[2].pitch = 0;
        kj::SynthOscillatorBank bank;
        bank.noteOn(true);
        for (int i = 0; i < 1000; ++i)
        {
            double sample = bank.render(settings, SynthWaveType::Sine, 69, 48000, 0);
            check(std::abs(sample - std::sin(kj::kOscTwoPi * 440.0 * i / 48000.0)) < 1e-9,
                  "Identical oscillators should retain normal gain");
        }
        double previousPhase = bank.oscillators[1].phase;
        bank.noteOn(false);
        check(bank.oscillators[1].phase == previousPhase, "Free-running phase should survive retrigger");
        bank.noteOn(true);
        for (const auto& oscillator : bank.oscillators)
            check(oscillator.phase == 0.0 && oscillator.pitchEnvelope == 1.0, "Phase sync must reset all oscillators");

        auto tableSettings = settings;
        for (auto& osc : tableSettings) osc.wavetableEnabled = true;
        kj::SynthOscillatorBank analytic, table;
        for (int i = 0; i < 2000; ++i)
        {
            double a = analytic.render(settings, SynthWaveType::Sine, 69, 48000, 0);
            double b = table.render(tableSettings, SynthWaveType::Sine, 69, 48000, 0);
            check(std::abs(a - b) < 2e-6, "Wavetable playback must preserve waveform and tuning");
        }

        for (int parameter = 0; parameter < 3; ++parameter)
        {
            auto changed = tableSettings;
            changed[parameter].wavetablePosition = 1.5f;
            kj::SynthOscillatorBank baseline, edited;
            double difference = 0.0;
            for (int i = 0; i < 2000; ++i)
            {
                double a = baseline.render(tableSettings, SynthWaveType::Sine, 69, 48000, 0);
                double b = edited.render(changed, SynthWaveType::Sine, 69, 48000, 0);
                difference += std::abs(a - b);
                for (int other = 0; other < 3; ++other)
                    if (other != parameter)
                        check(baseline.oscillators[other].lastOutput == edited.oscillators[other].lastOutput,
                              "Wavetable settings must be independent per oscillator");
            }
            check(difference > 1.0, "Wavetable position must change each oscillator's sound");
        }
        SynthOscillatorSettings morph;
        morph.wavetablePosition = 1.5f;
        for (int i = 0; i < 2000; ++i)
        {
            double phase = kj::kOscTwoPi * i / 2000.0;
            double expected = (kj::wavetableWave(SynthWaveType::Square, phase) +
                               kj::wavetableWave(SynthWaveType::Saw, phase)) * 0.5;
            check(std::abs(kj::wavetableOutput(SynthWaveType::Sine, phase, morph) - expected) < 1e-12,
                  "Position must interpolate between adjacent tables");
            morph.wavetableMix = 0.0f;
            check(std::abs(kj::wavetableOutput(SynthWaveType::Sine, phase, morph) - std::sin(phase)) < 1e-12,
                  "Zero wavetable mix must restore the original waveform");
            morph.wavetableMix = 1.0f;
        }

        for (int parameter = 0; parameter < 3; ++parameter)
        {
            auto changed = settings;
            if (parameter == 0) changed[1].feedback = 0.9f;
            if (parameter == 1) changed[1].formant = 0.0f;
            if (parameter == 2) { changed[1].formant = 0.2f; changed[1].resonance = 0.8f; }
            kj::SynthOscillatorBank baseline, edited;
            double difference = 0.0;
            for (int i = 0; i < 4000; ++i)
            {
                double a = baseline.render(settings, SynthWaveType::Saw, 69, 48000, 0);
                double b = edited.render(changed, SynthWaveType::Saw, 69, 48000, 0);
                check(std::isfinite(b), "Oscillator filter is unstable");
                difference += std::abs(a - b);
                check(baseline.oscillators[0].lastOutput == edited.oscillators[0].lastOutput,
                      "Editing Osc 2 must not change Osc 1");
            }
            check(difference > 1.0, "An oscillator control has no audible effect");
        }
        // Two eight-note chords: 16 voices, each with three oscillators.
        std::array<kj::SynthOscillatorBank,16> chordVoices;
        for(auto& osc:settings){osc.pitch=0;osc.pitchRange=1;osc.formant=.5f;osc.resonance=.2f;osc.feedback=0;osc.wavetableEnabled=false;}
        const auto started=std::chrono::steady_clock::now();
        double energy=0;
        for(int frame=0;frame<48000;++frame) {
            for(int voice=0;voice<16;++voice) {
                double sample=chordVoices[voice].render(settings,SynthWaveType::Saw,48+voice,48000,0);
                check(std::isfinite(sample),"Concurrent chord rendering became non-finite");
                energy+=sample*sample;
            }
        }
        check(energy>0,"Concurrent chord rendering was silent");
        auto& changedVoice=chordVoices[0];
        changedVoice.render(settings,SynthWaveType::Sine,72,96000,12);
        check(std::abs(changedVoice.oscillators[0].frequency-1046.5022612)<.0001,"Cached frequency ignored changed pitch or sample rate");
        std::cout << "16 three-oscillator voices, one second: "
                  << std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count()
                  << " ms (offline DSP only)\n";
        std::cout << "Three oscillator render tests passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
