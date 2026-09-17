#include "core/envelope.h"
#include <iostream>
#include <stdexcept>

void check(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

int main()
{
    try
    {
        for (double sr : {44100.0, 48000.0, 96000.0})
        {
            EnvelopeStage stage = EnvelopeStage::Attack;
            EnvelopeProgress progress;
            double value = 0.0;
            auto tick = [&](bool oneShot = false) {
                value = advanceEnvelope(stage, value, 0.1, 0.2, 0.4, 0.25, sr, progress, oneShot);
                check(std::isfinite(value) && value >= 0.0 && value <= 1.0, "Invalid envelope output");
            };
            for (int i = 0; i < static_cast<int>(sr * 0.05); ++i) tick();
            check(std::abs(value - 0.5) < 1e-9, "Attack should be halfway after half its duration");
            for (int i = 0; i < static_cast<int>(sr * 0.05); ++i) tick();
            check(stage == EnvelopeStage::Decay && value == 1.0, "Attack duration is wrong");
            for (int i = 0; i < static_cast<int>(sr * 0.1); ++i) tick();
            check(std::abs(value - 0.7) < 1e-9, "Decay should interpolate toward sustain");
            for (int i = 0; i < static_cast<int>(sr * 0.1); ++i) tick();
            check(stage == EnvelopeStage::Sustain && value == 0.4, "Decay duration is wrong");
            for (int i = 0; i < 100; ++i) tick();
            check(value == 0.4, "Held note must stay at sustain");
            stage = EnvelopeStage::Release;
            for (int i = 0; i < static_cast<int>(sr * 0.25) - 1; ++i) tick();
            check(stage == EnvelopeStage::Release && value > 0.0, "Release ended early at low sustain");
            tick();
            check(stage == EnvelopeStage::Idle && value == 0.0, "Release did not finish on time");

            stage = EnvelopeStage::Attack;
            progress = {};
            for (int i = 0; i < static_cast<int>(sr * 0.1); ++i) tick(true);
            check(stage == EnvelopeStage::Release && value == 1.0, "Drum AR must release automatically");
            for (int i = 0; i < static_cast<int>(sr * 0.25); ++i) tick(true);
            check(stage == EnvelopeStage::Idle && value == 0.0, "Drum release must reach silence");

            stage = EnvelopeStage::Attack;
            progress = {};
            for (int i = 0; i < static_cast<int>(sr * 0.02); ++i) tick();
            double interruptedLevel = value;
            stage = EnvelopeStage::Release;
            tick();
            check(value < interruptedLevel && value > 0.0, "Early note-off should release from current level");
            for (int i = 1; i < static_cast<int>(sr * 0.25); ++i) tick();
            check(stage == EnvelopeStage::Idle, "Early note-off release duration is wrong");

            stage = EnvelopeStage::Attack;
            progress = {};
            value = advanceEnvelope(stage, 0.0, 0.0, 0.0, 0.6, 0.0, sr, progress);
            check(stage == EnvelopeStage::Sustain && value == 0.6, "Zero-time stages must not stall");
            stage = EnvelopeStage::Release;
            value = advanceEnvelope(stage, value, 0.0, 0.0, 0.6, 0.0, sr, progress);
            check(stage == EnvelopeStage::Idle && value == 0.0, "Zero release must finish immediately");
        }
        std::cout << "Envelope timing tests passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
