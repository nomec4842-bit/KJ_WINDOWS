#include "hosting/TrackVST3.h"
#include "hosting/VST3Host.h"
#include "core/project_io.h"
#include "eq_audio_checks.h"
#include <windows.h>
#include <objbase.h>
#include <cmath>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <stdexcept>
void check(bool ok, const char* text) { if (!ok) throw std::runtime_error(text); }
void nativeTest(void*,const std::string& key,int,double& l,double& r) {
    if(key=="kj:eq"){l*=2;r*=2;}
    if(key=="kj:compressor"){l=std::clamp(l,-.25,.25);r=std::clamp(r,-.25,.25);}
}
double render(int track,const std::vector<std::string>* order=nullptr) {
    const std::vector<StepNoteInfo> ons; const std::vector<int> notes;
    double l = 0, r = 0;
    for (int i = 0; i < 128; ++i) {
        l = r = 0.2; kj::renderTrackVst3(track, false, 44100, i, 120, i == 0, false, false, ons, notes, l, r,order,nativeTest);
        if (i < 64) check(l == 0 && r == 0, "Rack delay is not 64 samples");
    }
    kj::stopTrackVst3Audio(); auto error = kj::takeTrackVst3Error(); if (!error.empty()) throw std::runtime_error(error); return l;
}
void nativeEq(void* context,const std::string& key,int,double& l,double& r) {
    if(key=="kj:eq") static_cast<eq::Processor*>(context)->process(l,r);
}
double measureRack(int track,const std::vector<std::string>& order,double hz) {
    eq::Processor processor;processor.configure(getTracks().front(),44100);
    const std::vector<StepNoteInfo> ons;const std::vector<int> notes;
    double result=measureEqTone(44100,hz,[&](int i,double& l,double& r){
        kj::renderTrackVst3(track,false,44100,i,120,i==0,false,false,ons,notes,l,r,&order,nativeEq,&processor);
    });
    kj::stopTrackVst3Audio();
    auto error=kj::takeTrackVst3Error();check(error.empty(),"Mixed EQ render failed");
    return result;
}
int main(int argc, char** argv) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED); int result = 0;
    try {
        check(argc == 3, "Expected fixture path and project path");
        const auto classes = kj::VST3Host::scan(argv[1]); check(classes.size() == 2, "Fixture scan failed");
        std::string gain, clip;
        for (const auto& c : classes) (c.name() == "KJ Test Gain" ? gain : clip) = c.ID().toString();
        initTracks(); int track = getTracks().front().id;
        auto a = kj::addTrackVst3(track, false, argv[1], gain, 44100);
        auto b = kj::addTrackVst3(track, false, argv[1], clip, 44100);
        check(std::abs(render(track) - 0.25) < 1e-6, "Gain > clip output wrong");
        kj::moveTrackVst3(track, b, -1);
        check(kj::getTrackVst3Slots(track)[0].id == b, "Slot ID changed on reorder");
        check(std::abs(render(track) - 0.4) < 1e-6, "Clip > gain output wrong");
        kj::bypassTrackVst3(track, a, true);
        check(std::abs(render(track) - 0.2) < 1e-6, "Bypass output wrong");
        check(saveProjectToFile(argv[2]), "Project save failed");
        kj::clearTrackVst3(); check(loadProjectFromFile(argv[2]), "Project load failed");
        track = getTracks().front().id;
        auto slots = kj::getTrackVst3Slots(track);
        check(slots.size() == 2 && slots[0].id == b && slots[1].id == a && slots[1].bypass, "Order/ID/bypass recall failed");
        check(std::abs(render(track) - 0.2) < 1e-6, "Recalled chain output wrong");
        // Save a GUI parameter edit before the processor has received any audio.
        kj::VST3Host host; host.load(argv[1], gain); host.setParameter(1, 0.75);
        auto edited = host.captureState(); host.unload();
        auto state = kj::captureTrackVst3(track); state[1].bypass = false; state[1].state = edited;
        kj::restoreTrackVst3(track, state, 44100);
        check(saveProjectToFile(argv[2]) && loadProjectFromFile(argv[2]), "Pre-play state round trip failed");
        track = getTracks().front().id;
        check(std::abs(render(track) - 0.6) < 1e-6, "Pre-play parameter edit lost");
        state = kj::captureTrackVst3(track); auto bytes = state[1].state.component;
        state[1].path = "Z:/missing/KJFixture.vst3";
        kj::restoreTrackVst3(track, state, 44100);
        check(!kj::getTrackVst3Slots(track)[1].error.empty(), "Missing plugin was not marked");
        check(std::abs(render(track) - 0.2) < 1e-6, "Missing effect did not pass through");
        check(saveProjectToFile(argv[2]) && loadProjectFromFile(argv[2]), "Missing plugin project failed");
        track = getTracks().front().id;
        check(kj::captureTrackVst3(track)[1].state.component == bytes, "Missing state bytes lost");
        kj::locateTrackVst3(track, a, argv[1], 44100);
        check(std::abs(render(track) - 0.6) < 1e-6, "Relink did not restore parameters");
        try { kj::addTrackVst3(track, false, "Z:/missing.vst3", gain, 44100); } catch (const std::exception&) {}
        check(kj::getTrackVst3Slots(track).size() == 2, "Failed add damaged rack");
        kj::removeTrackVst3(track, b); check(kj::getTrackVst3Slots(track)[0].id == a, "Removed wrong slot");
        kj::removeTrackVst3(track,a);
        b=kj::addTrackVst3(track,false,argv[1],clip,44100);
        std::vector<std::string> mixed{"kj:eq",b};
        check(std::abs(render(track,&mixed)-.25)<1e-6,"Native > plugin dispatch wrong");
        std::reverse(mixed.begin(),mixed.end());
        check(std::abs(render(track,&mixed)-.4)<1e-6,"Plugin > native dispatch wrong");
        trackSetFxOrder(track,mixed);
        check(saveProjectToFile(argv[2])&&loadProjectFromFile(argv[2]),"Mixed rack save/load failed");
        track=getTracks().front().id;
        check(trackGetFxOrder(track)==mixed&&std::abs(render(track,&mixed)-.4)<1e-6,"Mixed order recall failed");
        kj::clearTrackVst3();
        double l=.2,r=.2;const std::vector<StepNoteInfo> ons;const std::vector<int> notes;
        std::vector<std::string> nativeOnly{"kj:eq","kj:compressor"};
        kj::renderTrackVst3(track,false,44100,0,120,false,false,false,ons,notes,l,r,&nativeOnly,nativeTest);
        check(std::abs(l-.25)<1e-6,"Native-only chain order or zero-latency path failed");
        trackSetEqEnabled(track,true);trackSetEqShape(track,0,1);
        for(int mode=0;mode<3;++mode) {
            std::vector<std::string> order{"kj:eq"};
            if(mode) {
                auto slot=kj::addTrackVst3(track,false,argv[1],gain,44100);
                if(mode==1) order.push_back(slot);else order.insert(order.begin(),slot);
            }
            for(float amount:{-12.f,12.f}) {
                trackSetEqLowGain(track,amount);
                for(double hz:{40.,60.,100.,200.,2000.})
                    checkBassResponse(hz,amount,measureRack(track,order,hz)-(mode?20*std::log10(2.):0));
            }
            trackSetFxOrder(track,order);
            check(saveProjectToFile(argv[2])&&loadProjectFromFile(argv[2]),"Bass shelf project recall failed");
            track=getTracks().front().id;
            check(trackGetEqShape(track,0)==1,"Saved shelf shape lost");
            checkBassResponse(40,12,measureRack(track,order,40)-(mode?20*std::log10(2.):0));
            trackSetEqEnabled(track,false);
            check(std::abs(measureRack(track,order,40)-(mode?20*std::log10(2.):0))<.02,"Rack EQ bypass failed");
            trackSetEqEnabled(track,true);
            trackSetEqShape(track,0,0);
            check(saveProjectToFile(argv[2])&&loadProjectFromFile(argv[2]),"Bell recall failed");
            track=getTracks().front().id;
            check(trackGetEqShape(track,0)==0,"Explicit bell converted to shelf");
            check(std::abs(measureRack(track,order,40)-(mode?20*std::log10(2.):0)-measureEq(getTracks().front(),44100,40))<.02,"Recalled bell audio changed");
            trackSetEqShape(track,0,1);
            kj::clearTrackVst3();
        }
        std::cout << "PASS ordered DSP, single buffer, stable IDs, bypass, project recall, pre-play edits, missing state, relink, failed add, remove\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; result = 1; }
    kj::stopTrackVst3Audio(); kj::clearTrackVst3(); CoUninitialize(); return result;
}
