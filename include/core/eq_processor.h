#pragma once
#include "core/parametric_eq.h"
#include "core/tracks.h"
#include <array>

namespace eq {
// Shared by realtime playback and offline rendering. No allocation or shared state.
class Processor {
    struct Band {
        Coefficients c;
        double z1[2]{}, z2[2]{};
        void reset() { z1[0]=z1[1]=z2[0]=z2[1]=0; }
        double process(double input, int channel) {
            const double output=c.b0*input+z1[channel];
            z1[channel]=c.b1*input-c.a1*output+z2[channel];
            z2[channel]=c.b2*input-c.a2*output;
            return output;
        }
    };
    std::array<Band,3> bands_{};
    std::array<float,3> frequency_{}, gain_{}, q_{};
    std::array<int,3> shape_{};
    double rate_=0;
    bool enabled_=false;
public:
    void configure(const Track& track, double rate) {
        if (!std::isfinite(rate) || rate<=0) rate=44100;
        const std::array<float,3> gain{{track.lowGainDb,track.midGainDb,track.highGainDb}};
        for (size_t i=0;i<bands_.size();++i) {
            if (rate_!=rate || shape_[i]!=track.eqShape[i] || enabled_!=track.eqEnabled)
                bands_[i].reset();
            if (rate_!=rate || frequency_[i]!=track.eqFrequency[i] ||
                gain_[i]!=gain[i] || q_[i]!=track.eqQ[i] || shape_[i]!=track.eqShape[i])
                bands_[i].c=coefficients(static_cast<Shape>(track.eqShape[i]),rate,
                    track.eqFrequency[i],gain[i],track.eqQ[i]);
        }
        // Preserve filter history during ordinary parameter edits, especially at bass frequencies.
        rate_=rate; frequency_=track.eqFrequency; gain_=gain;
        q_=track.eqQ; shape_=track.eqShape; enabled_=track.eqEnabled;
    }
    void process(double& left, double& right) {
        if (!enabled_) return;
        for (auto& band:bands_) {
            left=band.process(left,0);
            right=band.process(right,1);
        }
    }
};
}
