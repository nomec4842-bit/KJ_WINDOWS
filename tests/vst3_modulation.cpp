#include "hosting/VST3Host.h"
#include "hosting/TrackVST3.h"
#include "core/project_io.h"
#include <windows.h>
#include <objbase.h>
#include <cmath>
#include <iostream>
#include <stdexcept>

void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
double render(int track, const std::vector<ModMatrixAssignment>& routes) {
    std::vector<StepNoteInfo> ons; std::vector<int> notes;
    const double sources[] = {1.0};
    double l=0, r=0;
    for (int i=0;i<128;++i) {
        l=r=.1;
        kj::renderTrackVst3(track,false,44100,i,120,false,false,false,ons,notes,l,r,nullptr,nullptr,nullptr,&routes,sources,1);
    }
    check(kj::takeTrackVst3Error().empty(), "Plugin processing failed");
    return l;
}
int main(int argc, char** argv) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    HMODULE module=nullptr; int result=0;
    try {
        check(argc==3,"Expected fixture and scratch project paths");
        module=LoadLibraryA(argv[1]); check(module!=nullptr,"Cannot load fixture");
        auto tweak=reinterpret_cast<void(*)(unsigned,double)>(GetProcAddress(module,"KJTestTweakAll"));
        check(tweak!=nullptr,"Missing fixture edit callback");
        std::string gain;
        for (const auto& c : kj::VST3Host::scan(argv[1])) if(c.name()=="KJ Test Gain")gain=c.ID().toString();
        {
            kj::VST3Host host;host.load(argv[1],gain);
            host.setParameter(1,.6);
            check(host.takeTweakedParameters().empty(),"Host writes were learned as knob edits");
            tweak(2,.2);check(host.takeTweakedParameters().empty(),"Read-only meter learned");
            tweak(1,.6);tweak(1,.7);
            auto targets=host.takeTweakedParameters();
            check(targets.size()==1&&targets[0].id==1&&targets[0].name=="Gain","Knob edit was not discovered/coalesced");
            check(host.takeTweakedParameters().empty(),"Tweak delivered twice");
        }
        initTracks();int track=getTracks().front().id;
        auto a=kj::addTrackVst3(track,false,argv[1],gain,44100);
        auto b=kj::addTrackVst3(track,false,argv[1],gain,44100);
        check(kj::getTrackVst3ModTargets(track).empty(),"Untouched plugin exposed targets");
        tweak(1,.5);kj::serviceTrackVst3Controllers();tweak(1,.5);
        auto targets=kj::getTrackVst3ModTargets(track);
        check(targets.size()==2&&targets[0].slotId!=targets[1].slotId,"Per-slot discovery or deduplication failed");
        ModMatrixAssignment route;route.id=1;route.trackId=track;route.sourceIndex=0;route.parameterIndex=-1;
        route.vstSlotId=a;route.vstParameterId=1;route.vstParameterName=targets[0].label;route.normalizedAmount=.25f;
        check(std::abs(render(track,{route})-.6)<1e-6,"Route did not modulate only its slot");
        kj::moveTrackVst3(track,a,1);
        check(std::abs(render(track,{route})-.6)<1e-6,"Reorder broke route identity");
        check(std::abs(render(track,{})-.4)<1e-6,"Removing route did not restore base value");
        route.normalizedAmount=-1;
        check(std::abs(render(track,{route}))<1e-6,"Negative modulation did not clamp at zero");
        route.normalizedAmount=1;
        check(std::abs(render(track,{route})-.8)<1e-6,"Positive modulation did not clamp at one");
        route.normalizedAmount=.25f;
        tweak(1,.25);
        check(std::abs(render(track,{route})-.2)<1e-6,"Live knob edit did not update modulation base");
        kj::stopTrackVst3Audio();
        modMatrixSetAssignments({route});
        check(saveProjectToFile(argv[2])&&loadProjectFromFile(argv[2]),"Modulation project recall failed");
        track=getTracks().front().id;
        targets=kj::getTrackVst3ModTargets(track);
        check(targets.size()==2,"Learned targets lost on recall");
        auto routes=modMatrixGetAssignments();
        check(routes.size()==1&&routes[0].vstSlotId==a&&routes[0].vstParameterId==1,"Route identity lost on recall");
        check(std::abs(render(track,routes)-.2)<1e-6,"Modulation was saved as base or route lost");
        check(std::abs(render(track,{})-.1)<1e-6,"Recalled base value changed");
        kj::removeTrackVst3(track,a);
        check(std::abs(render(track,routes)-.1)<1e-6,"Removed slot route affected another instance");
        std::cout<<"PASS knob discovery, deduplication, slot identity, live modulation, clamping, base restoration and project recall\n";
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n';result=1; }
    kj::stopTrackVst3Audio();kj::clearTrackVst3();
    if(module)FreeLibrary(module);
    CoUninitialize();return result;
}
