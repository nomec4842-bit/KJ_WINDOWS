#include "core/track_type_sample.h"
#include "core/track_type_synth.h"
#include "core/mod_matrix_parameters.h"
#include "core/mod_matrix.h"
#include "core/sample_loader.h"
#include "core/project_io.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <limits>

void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

int main(int argc, char** argv) {
    try {
        initTracks();
        int track = getTracks().front().id;
        trackSetType(track, TrackType::Sample);
        check(!trackGetSampleDrumMode(track), "Pitch must be the default");
        trackSetSampleDrumMode(track, true);
        auto bank = std::make_shared<SampleBankBuffers>();
        for (int i = 0; i < 200; ++i) {
            auto buffer = std::make_shared<SampleBuffer>();
            buffer->samples = {static_cast<float>(i)};
            buffer->channels = 1;
            buffer->sampleRate = 48000;
            bank->push_back(buffer);
        }
        sampleSetBankBuffers(bank);
        auto snapshot = sampleGetBankBuffers();
        check(snapshot->size() == 200, "Bank must exceed MIDI's 128-note range");
        int lane = kSampleDrumNoteBase + 199;
        trackSetStepNote(track, 0, kSampleDrumNoteBase);
        trackToggleStepNote(track, 0, lane);
        trackSetStepNoteVelocity(track, 0, lane, 0.4f);
        trackSetStepNoteSustain(track, 0, lane, true);
        auto notes = trackGetStepNoteInfo(track, 0);
        check(notes.size() == 2 && notes.back().midiNote == lane, "Distinct drum lanes must survive storage");
        check(std::abs(notes.back().velocity - 0.4f) < 0.001f && notes.back().sustain, "Drum note parameters must survive");
        trackSetSampleBuffer(track, (*snapshot)[5]);
        trackSetSampleDrumMode(track, false);
        trackToggleStepNote(track, 0, 60);
        check(trackGetStepNotes(track, 0).size() == 3, "Mode changes must preserve both patterns");
        check(trackGetSampleBuffer(track) == (*snapshot)[5], "Pitch selection must survive a mode change");
        check(std::abs(samplePlaybackIncrement(60, false, 48000, 48000) - 1.0) < 1e-9, "C4 must play at original pitch");
        check(std::abs(samplePlaybackIncrement(72, false, 48000, 48000) - 2.0) < 1e-9, "Octave up must double playback rate");
        check(std::abs(samplePlaybackIncrement(48, false, 48000, 48000) - 0.5) < 1e-9, "Octave down must halve playback rate");
        check(std::abs(samplePlaybackIncrement(lane, true, 24000, 48000, 12.0) - 1.0) < 1e-9, "Drum tuning must transpose relative to original sample and respect sample rate");
        trackSetSampleDrumMode(track, true);
        trackSetSampleAttack(track,.04f);trackSetSampleRelease(track,.6f);
        auto inherited=trackGetDrumSettings(track,199);
        check(inherited.attack==.04f&&inherited.release==.6f&&inherited.pitch==0&&inherited.pan==0&&inherited.volume==1,"Legacy drum defaults changed");
        trackSetDrumParameter(track,199,DrumParameter::Attack,.12f);
        trackSetDrumParameter(track,199,DrumParameter::Release,.8f);
        trackSetDrumParameter(track,199,DrumParameter::Pitch,-12);
        trackSetDrumParameter(track,199,DrumParameter::Pan,-.7f);
        trackSetDrumParameter(track,199,DrumParameter::Volume,.35f);
        trackSetDrumParameter(track,0,DrumParameter::Pitch,12);
        trackSetDrumParameter(track,0,DrumParameter::Pan,1);
        trackSetDrumParameter(track,0,DrumParameter::Volume,0);
        trackSetSelectedDrum(track,199);
        trackSetDrumParameter(track,199,DrumParameter::Pitch,std::numeric_limits<float>::quiet_NaN());
        auto drum=sampleDrumSettings(getTracks().front(),199);
        check(drum.attack==.12f&&drum.release==.8f&&drum.pitch==-12&&drum.pan==-.7f&&drum.volume==.35f,"Per-drum snapshot or finite validation failed");
        check(trackGetDrumSettings(track,0).pitch==12&&trackGetDrumSettings(track,0).volume==0,"Drum settings leaked across lanes");
        check(trackGetDrumSettings(track,1).attack==.04f,"Unedited drum lost inherited AR");
        auto other=addTrack();check(trackGetDrumSettings(other.id,199).volume==1,"Drum settings leaked across tracks");
        check(argc > 1 && saveProjectToFile(argv[1]), "Project save failed");
        for (int band = 0; band < 3; ++band) {
            trackSetEqFrequency(track, band, 300.0f * (band + 1));
            trackSetEqQ(track, band, 1.5f + band);
        }
        for (int osc = 0; osc < 3; ++osc) {
            trackSetSynthOscWavetablePosition(track, osc, 0.5f + osc);
            trackSetSynthOscWavetableMix(track, osc, 0.25f * osc);
        }
        check(saveProjectToFile(argv[1]), "Wavetable project save failed");
        check(loadProjectFromFile(argv[1]), "Project load failed");
        track = getTracks().front().id;
        auto recalled=trackGetDrumSettings(track,199);
        check(recalled.attack==.12f&&recalled.release==.8f&&recalled.pitch==-12&&recalled.pan==-.7f&&recalled.volume==.35f,"Drum AR/pitch/pan/volume recall failed");
        check(trackGetSelectedDrum(track)==199,"Selected drum recall failed");

        for (int band = 0; band < 3; ++band) {
            const auto restoredEq = getTracks().front();
            check(restoredEq.eqFrequency[band] == 300.0f * (band + 1), "EQ frequency must survive save/load");
            check(restoredEq.eqQ[band] == 1.5f + band, "EQ Q must survive save/load");
        }
        for (int osc = 0; osc < 3; ++osc) {
            const auto settings = getTracks().front().synthOscillators[osc];
            check(std::abs(settings.wavetablePosition - (0.5f + osc)) < 1e-6f,
                  "Wavetable positions must round-trip into playback snapshots");
            check(std::abs(settings.wavetableMix - 0.25f * osc) < 1e-6f,
                  "Wavetable mixes must round-trip into playback snapshots");
        }
        check(trackGetSampleDrumMode(track), "Sampler mode must round-trip through project files");
        auto restored = trackGetStepNotes(track, 0);
        check(restored.size() == 3 && restored.back() == lane, "Extended drum lane IDs must round-trip");
        trackSetType(track, TrackType::Synth);
        int positionTarget = modMatrixGetParameterIndex(ModMatrixParameter::SynthOsc1WavetablePosition);
        int osc3MixTarget = modMatrixGetParameterIndex(ModMatrixParameter::SynthOsc3WavetableMix);
        check(positionTarget == 46 && osc3MixTarget == 51, "Existing saved parameter indices must remain stable");
        auto synth = getTracks().front();
        synth.synthOscillators[0].wavetableEnabled = false;
        check(!modMatrixParameterAvailableForTrack(positionTarget, synth), "WT targets must hide when disabled");
        synth.synthOscillators[0].wavetableEnabled = true;
        synth.synthThreeOscEnabled = false;
        check(modMatrixParameterAvailableForTrack(positionTarget, synth), "Single oscillator WT target must be available");
        check(!modMatrixParameterAvailableForTrack(osc3MixTarget, synth), "Osc 3 target requires 3 Osc");
        synth.synthThreeOscEnabled = true;
        check(modMatrixParameterAvailableForTrack(osc3MixTarget, synth), "3 Osc WT targets must be available");
        const auto* info = modMatrixGetParameterInfo(osc3MixTarget);
        info->setter(track, 0.65f);
        check(std::abs(info->getter(track) - 0.65f) < 1e-6f, "WT target must address the right parameter");
        auto route = modMatrixCreateAssignment();
        route.trackId = track;
        route.parameterIndex = osc3MixTarget;
        route.normalizedAmount = -0.5f;
        check(modMatrixUpdateAssignment(route), "WT route update failed");
        check(saveProjectToFile(argv[1]) && loadProjectFromFile(argv[1]), "WT route round-trip failed");
        auto routes = modMatrixGetAssignments();
        check(!routes.empty() && routes.back().parameterIndex == osc3MixTarget && routes.back().normalizedAmount == -0.5f,
              "WT route target and amount must survive save/load");
        track = getTracks().front().id;
        trackSetStepNote(track, 1, 999);
        check(trackGetStepNote(track, 1) == 127, "Synth MIDI validation must remain intact");
        std::cout << "Sampler mode regression tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
