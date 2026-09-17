// Manual integration test: requires a working Windows audio output and plays quietly.
#include "core/audio_engine.h"
#include "gui/eq_graph.h"
#include <windowsx.h>
#include "core/sequencer.h"
#include "core/tracks.h"
#include "core/track_type_synth.h"
#include "core/track_type_sample.h"
#include "core/sample_loader.h"
#include "hosting/TrackVST3.h"
#include "hosting/VST3Host.h"
#include <windows.h>
#include <objbase.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>
#include <wrl/client.h>
#include <chrono>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
void pump(int milliseconds) {
    const auto end=std::chrono::steady_clock::now()+std::chrono::milliseconds(milliseconds);
    do { MSG m;while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}Sleep(10); }
    while(std::chrono::steady_clock::now()<end);
}
// Attenuate this test process at the Windows session, after KJ's final mix.
// This allows measuring real unity-volume clipping without loud test playback.
void quietTestSession() {
    using Microsoft::WRL::ComPtr;
    ComPtr<IMMDeviceEnumerator> enumerator;ComPtr<IMMDevice> device;
    ComPtr<IAudioSessionManager2> manager;ComPtr<IAudioSessionEnumerator> sessions;
    if(FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,IID_PPV_ARGS(&enumerator))) ||
       FAILED(enumerator->GetDefaultAudioEndpoint(eRender,eConsole,&device)) ||
       FAILED(device->Activate(__uuidof(IAudioSessionManager2),CLSCTX_ALL,nullptr,reinterpret_cast<void**>(manager.GetAddressOf()))) ||
       FAILED(manager->GetSessionEnumerator(&sessions)))throw std::runtime_error("Cannot quiet test session");
    int count=0;sessions->GetCount(&count);
    for(int i=0;i<count;++i){
        ComPtr<IAudioSessionControl> control;ComPtr<IAudioSessionControl2> details;ComPtr<ISimpleAudioVolume> volume;
        DWORD pid=0;
        if(SUCCEEDED(sessions->GetSession(i,&control))&&SUCCEEDED(control.As(&details))&&
           SUCCEEDED(details->GetProcessId(&pid))&&pid==GetCurrentProcessId()&&SUCCEEDED(control.As(&volume))) {
            if(SUCCEEDED(volume->SetMasterVolume(.01f,nullptr)))return;
        }
    }
    throw std::runtime_error("Test audio session not found; refusing loud playback");
}
static std::shared_ptr<spectrum::View> liveMeter;
double measure(int id,float gain) {
    // Exercise the same mouse handler as the visible EQ editor, during playback.
    HWND owner=CreateWindowW(L"STATIC",L"EQ live test",WS_POPUP,0,0,900,340,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    EqGraph graph;graph.track=id;graph.bounds=graph.clip={0,0,900,340};
    int x=42+int(std::log(trackGetEqFrequency(id,0)/20.)/std::log(1000.)*842);
    int y=30+int((18-trackGetEqLowGain(id))/36.*262);
    handleEqGraph(owner,graph,WM_LBUTTONDOWN,0,MAKELPARAM(x,y));
    int targetY=30+int((18-gain)/36.*262);
    handleEqGraph(owner,graph,WM_MOUSEMOVE,0,MAKELPARAM(x,targetY));
    handleEqGraph(owner,graph,WM_LBUTTONUP,0,MAKELPARAM(x,targetY));
    DestroyWindow(owner);
    if(std::abs(trackGetEqLowGain(id)-gain)>.15)throw std::runtime_error("Live graph did not update gain");
    pump(500);
    double power=0,peak=0;size_t count=0,clipped=0;
    for(int i=0;i<200;++i) {
        pump(10);
        std::array<float,spectrum::size/2+1> bins;liveMeter->read(bins);
        for(float x:getMasterWaveformSnapshot(44100)){
            power+=double(x)*x;++count;peak=std::max(peak,std::abs(double(x)));
            clipped+=std::abs(x)>=.99999f;
        }
    }
    if(!count||power<1e-15)throw std::runtime_error("No actual master audio captured");
    std::cout<<"  graph gain="<<trackGetEqLowGain(id)<<", Hz="<<trackGetEqFrequency(id,0)
             <<", master RMS="<<10*std::log10(power/count)<<", peak="<<peak
             <<", actual clipped samples="<<100.*clipped/count<<"%\n";
    if(!liveMeter->hasInput)throw std::runtime_error("No live EQ input telemetry");
    std::cout<<"  Measured EQ change="<<liveMeter->outputDb-liveMeter->inputDb<<", master clip indicator="<<isAudioOutputClipping()<<"\n";
    if((clipped>count/20)!=isAudioOutputClipping())throw std::runtime_error("Master clipping indicator disagrees with captured output");
    return 10*std::log10(power/count);
}
int main(int argc,char** argv) {
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);int result=0;
    try {
        std::cout<<std::unitbuf;
        initTracks();initSequencer();int id=getTracks().front().id;
        liveMeter=spectrum::watch(id);
        trackSetVolume(id,1.f);trackSetEqEnabled(id,true);
        trackSetEqShape(id,0,0);trackSetEqFrequency(id,0,66.376f);trackSetEqQ(id,0,.707f);
        trackSetSynthWaveType(id,SynthWaveType::Sine);trackSetSynthFormant(id,1);
        trackSetSynthAttack(id,.001f);trackSetSynthSustain(id,1);trackSetSynthRelease(id,.01f);
        for(int i=0;i<16;++i){trackSetStepState(id,i,true);trackSetStepNote(id,i,33);}
        setActiveSequencerTrackId(id);
        for(int source=0;source<(argc>1?3:2);++source) {
            isPlaying=false;shutdownAudio();kj::clearTrackVst3();
            trackSetType(id,source==0?TrackType::Synth:source==1?TrackType::Sample:TrackType::Vst3);
            if(source==1) {
                auto sample=std::make_shared<SampleBuffer>();sample->sampleRate=48000;sample->channels=1;
                for(int i=0;i<48000;++i)sample->samples.push_back(float(.2*std::sin(6.283185307179586*55*i/48000)));
                trackSetSampleBuffer(id,sample);
                for(int i=0;i<16;++i)trackSetStepNote(id,i,60);
            }
            if(source==2) {
                for(int i=0;i<16;++i)trackSetStepNote(id,i,33);
                auto info=kj::VST3Host::scan(argv[1]).front();
                kj::loadTrackVst3(id,true,argv[1],info.ID().toString(),getAudioSampleRate());
            }
            initAudio(false);pump(1000);quietTestSession();requestSequencerReset();isPlaying=true;
            double baseline=measure(id,0),cut=measure(id,-12)-baseline,boost=measure(id,8.654f)-baseline;
            std::cout<<"source="<<source<<" baseline="<<baseline<<" dBFS, cut="<<cut<<" dB, boost="<<boost<<" dB\n";
            if(cut>-5||boost<3)throw std::runtime_error("Live bass bell did not change bass substantially");
            if(source==0) {
                trackSetVolume(id,.25f);
                const double baseWithHeadroom=measure(id,0),boostWithHeadroom=measure(id,8.654f)-baseWithHeadroom;
                if(boostWithHeadroom<7||isAudioOutputClipping())throw std::runtime_error("Track output did not restore clean EQ boost");
                std::cout<<"PASS headroom control restored "<<boostWithHeadroom<<" dB clean boost\n";
                trackSetVolume(id,1.f);
            }
        }
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';result=1;}
    shutdownAudio();kj::clearTrackVst3();CoUninitialize();return result;
}
