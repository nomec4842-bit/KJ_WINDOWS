#pragma once
#include "core/eq_processor.h"
#include <stdexcept>
#include <iostream>
// Measure samples after settling, using distinct stereo amplitudes and polarity.
template<class Render> double measureEqTone(double rate,double hz,Render render) {
    double inputPower=0,leftPower=0,rightPower=0;
    for(int i=0;i<int(rate*1.5);++i) {
        double input=.02*std::sin(6.283185307179586*hz*i/rate);
        double l=input,r=-input*.37;
        render(i,l,r);
        if(i>=int(rate*.5)) { inputPower+=input*input;leftPower+=l*l;rightPower+=r*r; }
    }
    if(std::abs(10*std::log10(rightPower/leftPower)-20*std::log10(.37))>.02)
        throw std::runtime_error("EQ stereo response differs");
    return 10*std::log10(leftPower/inputPower);
}
inline double measureEq(const Track& track,double rate,double hz) {
    eq::Processor processor;processor.configure(track,rate);
    return measureEqTone(rate,hz,[&](int,double& l,double& r){processor.process(l,r);});
}
inline void checkBassResponse(double hz,double gain,double measured) {
    const double magnitude=measured*(gain>0?1:-1);
    if((hz==40 && std::abs(measured-gain)>.1) ||
       (hz<=100 && magnitude<10) || (hz==200 && std::abs(magnitude-6)>.05) ||
       (hz==2000 && std::abs(measured)>.02))
        throw std::runtime_error("Measured bass shelf response outside requirements");
}
