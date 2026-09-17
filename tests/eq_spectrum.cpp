#include "core/eq_spectrum.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
void check(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
int main(){try{
    auto a=spectrum::watch(7),b=spectrum::watch(8);
    std::array<float,spectrum::size/2+1> db{};
    check(!a->read(db),"Analyzer returned stale samples on creation");
    for(int i=0;i<spectrum::size;++i){float sample=static_cast<float>(.5*std::sin(6.283185307179586*64*i/spectrum::size));spectrum::capture(7,sample,-sample);}
    check(a->read(db),"No spectrum after a complete frame");
    check(std::max_element(db.begin(),db.end())-db.begin()==64,"Wrong frequency peak");
    check(std::abs(db[64]+6.0206)<.1,"Stereo cancellation or incorrect level calibration");
    check(!b->read(db),"Audio leaked between tracks");check(!a->read(db),"Stale spectrum reported as fresh");
    for(int i=0;i<spectrum::size;++i)spectrum::capture(7,0,0);
    check(a->read(db)&&*std::max_element(db.begin(),db.end())<=-119,"Silence did not clear spectrum");
    for(double rate:{44100.,48000.,96000.})for(double hz:{30.,40.,60.,100.}){
        for(int i=0;i<spectrum::size;++i){float sample=static_cast<float>(.5*std::sin(6.283185307179586*hz*i/rate));spectrum::capture(7,sample,sample);}
        check(a->read(db),"Missing bass spectrum");
        auto bin=std::max_element(db.begin(),db.end())-db.begin();
        check(std::abs(bin*rate/spectrum::size-hz)<=rate/spectrum::size,"Bass peak outside frequency resolution");
        check(db[bin]>-8&&db[bin]<-4,"Bass level substantially misreported");
    }
    for(float factor:{.25f,1.f,4.f}) {
        for(int i=0;i<spectrum::size;++i){float input=.1f*std::sin(6.283185307179586*10*i/spectrum::size);spectrum::capture(7,input*factor,-input*factor,input,-input);}
        check(a->read(db)&&a->hasInput,"Missing EQ level telemetry");
        check(std::abs(a->outputDb-a->inputDb-20*std::log10(factor))<.001,"Wrong measured EQ level change");
    }
    for(int i=0;i<spectrum::size;++i)spectrum::capture(7,0,0,0,0);
    check(a->read(db)&&!a->hasInput,"Silence retained EQ level change");
    a.reset();auto next=spectrum::watch(9);check(!next->read(db),"Reused analyzer showed previous track");
    std::cout<<"PASS spectrum frequency, calibrated level, stereo phase, track isolation and silence\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}return 0;}
