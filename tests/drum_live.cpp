#include "core/audio_engine.h"
#include "core/sequencer.h"
#include "core/tracks.h"
#include "core/track_type_sample.h"
#include "core/sample_loader.h"
#include <windows.h>
#include <objbase.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>
#include <wrl/client.h>
#include <chrono>
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

double measuredHz=0;
double level(){
    isPlaying=false;pump(100);requestSequencerReset();isPlaying=true;pump(180);
    double power=0;int count=0,crossings=0,intervals=0;
    for(int i=0;i<20;++i){pump(10);auto samples=getMasterWaveformSnapshot(44100);for(size_t j=0;j<samples.size();++j){float value=samples[j];power+=value*value;++count;if(j){++intervals;if(samples[j-1]<=0&&value>0)++crossings;}}}
    measuredHz=crossings*getAudioSampleRate()/std::max(1,intervals);
    return std::sqrt(power/std::max(1,count));
}
int main(){CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);int result=0;
try{
    std::cout<<std::unitbuf;initTracks();initSequencer();int id=getTracks().front().id;
    auto bank=std::make_shared<SampleBankBuffers>();
    for(int lane=0;lane<2;++lane){auto sample=std::make_shared<SampleBuffer>();sample->channels=1;sample->sampleRate=48000;
        for(int i=0;i<480000;++i)sample->samples.push_back(float(.1*std::sin(6.283185307179586*(lane?880:440)*i/48000)));
        bank->push_back(sample);
    }
    sampleSetBankBuffers(bank);trackSetType(id,TrackType::Sample);trackSetSampleDrumMode(id,true);
    trackSetVolume(id,.2f);trackSetStepState(id,0,true);trackSetStepNote(id,0,128);setActiveSequencerTrackId(id);
    for(int lane=0;lane<2;++lane){trackSetDrumParameter(id,lane,DrumParameter::Attack,0);trackSetDrumParameter(id,lane,DrumParameter::Release,4);}
    initAudio(false);pump(1000);quietTestSession();
    double base=level();if(base<1e-5)throw std::runtime_error("No drum output");
    double originalHz=measuredHz;
    trackSetDrumParameter(id,0,DrumParameter::Pitch,12);level();double tunedHz=measuredHz;
    if(std::abs(originalHz-440)>45||std::abs(tunedHz-880)>70)throw std::runtime_error("Drum pitch did not change rendered frequency by an octave");
    trackSetDrumParameter(id,0,DrumParameter::Pitch,0);
    trackSetDrumParameter(id,0,DrumParameter::Volume,.25f);double quiet=level();
    if(std::abs(quiet/base-.25)>.035)throw std::runtime_error("Drum volume not applied to rendered audio");
    trackSetDrumParameter(id,0,DrumParameter::Volume,1);trackSetDrumParameter(id,0,DrumParameter::Pan,-1);double left=level();
    trackSetDrumParameter(id,0,DrumParameter::Pan,1);double right=level();
    if(std::abs(left/base-.5)>.04||std::abs(right/base-.5)>.04)throw std::runtime_error("Drum pan did not isolate channels before master mono capture");
    trackSetDrumParameter(id,0,DrumParameter::Pan,0);trackSetDrumParameter(id,0,DrumParameter::Attack,1);double slow=level();
    if(slow>base*.65)throw std::runtime_error("Per-drum attack did not slow envelope");
    trackSetDrumParameter(id,0,DrumParameter::Attack,0);trackSetDrumParameter(id,0,DrumParameter::Release,.03f);double shortTail=level();
    if(shortTail>base*.05)throw std::runtime_error("Per-drum release did not shorten tail");
    trackSetDrumParameter(id,0,DrumParameter::Volume,0);trackToggleStepNote(id,0,129);double other=level();
    if(other<base*.8)throw std::runtime_error("Muted drum affected overlapping drum");
    trackSetDrumParameter(id,1,DrumParameter::Volume,0);double both=level();
    if(both>1e-8)throw std::runtime_error("Muted lanes produced audio");
    std::cout<<"PASS live drum pitch="<<originalHz<<" -> "<<tunedHz<<" Hz; volume ratio="<<quiet/base<<", pan="<<left/base<<"/"<<right/base<<", attack="<<slow/base<<", release="<<shortTail/base<<", independent overlapping lane="<<other/base<<"\n";
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";result=1;}
shutdownAudio();CoUninitialize();return result;}
