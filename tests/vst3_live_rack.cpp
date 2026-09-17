#include "hosting/TrackVST3.h"
#include "hosting/VST3Host.h"
#include <windows.h>
#include <objbase.h>
#include <atomic>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <thread>
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
int main(int argc,char** argv){
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    std::atomic<bool> quit{false},ready{false};std::atomic<unsigned> frames{0};
    std::exception_ptr audioFailure;std::thread audio;int result=0;
    try{
        check(argc==2,"Expected test fixture");std::string gain,clip;
        for(const auto& c:kj::VST3Host::scan(argv[1]))(c.name()=="KJ Test Gain"?gain:clip)=c.ID().toString();
        kj::addTrackVst3(1,false,argv[1],gain,44100);
        auto base=kj::addTrackVst3(2,false,argv[1],gain,44100);
        audio=std::thread([&]{
            kj::setTrackVst3AudioActive(true);ready=true;
            try{
                const std::vector<StepNoteInfo> ons;const std::vector<int> notes;
                unsigned frame=0;
                while(!quit.load()){
                    kj::serviceTrackVst3Changes();
                    for(int n=0;n<64;++n,++frame){
                        for(int id:{1,2}){
                            double l=.2,r=.2;
                            kj::renderTrackVst3(id,false,44100,frame,120,false,false,false,ons,notes,l,r);
                            if(frame>=64){
                                check(std::isfinite(l)&&l>.01,"Live edit muted/reset an established chain");
                                if(id==1)check(std::abs(l-.4)<1e-6,"Editing another track interrupted unchanged audio");
                            }
                        }
                    }
                    frames=frame;Sleep(1);
                }
            }catch(...){audioFailure=std::current_exception();}
            kj::stopTrackVst3Audio();kj::setTrackVst3AudioActive(false);
        });
        while(!ready.load())Sleep(1);
        for(int run=0;run<12;++run){
            unsigned before=frames.load();
            auto added=kj::addTrackVst3(2,false,argv[1],clip,44100);
            check(frames.load()>before,"Plugin construction blocked audio progress");
            kj::moveTrackVst3(2,added,-1);kj::bypassTrackVst3(2,added,true);
            kj::bypassTrackVst3(2,added,false);kj::removeTrackVst3(2,added);
            try{kj::addTrackVst3(2,false,"Z:/missing.vst3",clip,44100);}catch(const std::exception&){}
            auto slots=kj::getTrackVst3Slots(2);check(slots.size()==1&&slots[0].id==base,"Live failed load damaged chain");
        }
        quit=true;audio.join();if(audioFailure)std::rethrow_exception(audioFailure);
        check(kj::takeTrackVst3Error().empty(),"Live edit reported a processing error");
        std::cout<<"PASS live construction/add/reorder/bypass/remove, failed-load preservation, continuous other-track audio\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';result=1;}
    quit=true;if(audio.joinable())audio.join();kj::clearTrackVst3();CoUninitialize();return result;
}
