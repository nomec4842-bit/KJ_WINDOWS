#include "core/audio_input.h"
#include "core/audio_engine.h"
#include "core/audio_meter.h"
#include "core/audio_recording.h"
#include "core/tracks.h"
#include "core/sequencer.h"
#include "core/track_type_midi.h"
#include "core/piano_pattern.h"
#ifdef KJ_ENABLE_VST3
#include "hosting/TrackVST3.h"
#include "hosting/VST3Host.h"
#endif
#include <windows.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>
#include <wrl/client.h>
#include <thread>
#include <chrono>
#include <iostream>
#include <cmath>
#include <fstream>
#include <stdexcept>
namespace input {void setTestSource(const std::string&,std::shared_ptr<Buffer>);}
void setMidiTestObserver(void(*)(int,uint32_t));
std::atomic<int> noteOns{0},noteOffs{0};
void observeMidi(int port,uint32_t message){if(port!=1234)return;if((message&0xf0)==0x90)++noteOns;if((message&0xf0)==0x80)++noteOffs;}
void check(bool v,const char* message){if(!v)throw std::runtime_error(message);}
void pump(int ms){auto until=std::chrono::steady_clock::now()+std::chrono::milliseconds(ms);do{MSG m;while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}Sleep(5);}while(std::chrono::steady_clock::now()<until);}
void muteSession(){using Microsoft::WRL::ComPtr;ComPtr<IMMDeviceEnumerator> e;ComPtr<IMMDevice>d;ComPtr<IAudioSessionManager2> m;ComPtr<IAudioSessionEnumerator>s;
 check(SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,IID_PPV_ARGS(&e)))&&SUCCEEDED(e->GetDefaultAudioEndpoint(eRender,eConsole,&d))&&SUCCEEDED(d->Activate(__uuidof(IAudioSessionManager2),CLSCTX_ALL,nullptr,reinterpret_cast<void**>(m.GetAddressOf())))&&SUCCEEDED(m->GetSessionEnumerator(&s)),"Cannot access test output session");int n=0;s->GetCount(&n);bool muted=false;for(int i=0;i<n;++i){ComPtr<IAudioSessionControl>c;ComPtr<IAudioSessionControl2>c2;ComPtr<ISimpleAudioVolume>v;DWORD pid=0;if(SUCCEEDED(s->GetSession(i,&c))&&SUCCEEDED(c.As(&c2))&&SUCCEEDED(c2->GetProcessId(&pid))&&pid==GetCurrentProcessId()&&SUCCEEDED(c.As(&v)))muted=SUCCEEDED(v->SetMute(TRUE,nullptr));}check(muted,"Could not mute test process output");}
double rms(){pump(250);double sum=0;size_t count=0;for(int i=0;i<30;++i){pump(10);auto samples=getMasterWaveformSnapshot(2048);for(float v:samples)sum+=v*v;count+=samples.size();}return std::sqrt(sum/std::max(size_t(1),count));}
int main(int argc,char**argv){CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);std::cout<<std::unitbuf;int result=0;std::atomic<float> amplitude{.04f};std::atomic<bool> stop{false};std::thread producer;std::wstring error;
try{
 initTracks();initSequencer();int id=getTracks().front().id;trackSetType(id,TrackType::AudioIn);trackSetInputDevice(id,"kj-test-source","Synthetic input");trackSetInputChannels(id,0,true);trackSetVolume(id,1);input::setMonitorWhileStopped(true);
 auto source=std::make_shared<input::Buffer>();source->configure(2,48000);input::setTestSource("kj-test-source",source);
 auto start=[&]{stop=false;producer=std::thread([&]{uint64_t count=0;auto start=std::chrono::steady_clock::now();while(!stop){auto now=std::chrono::steady_clock::now();int total=int(std::chrono::duration<double>(now-start).count()*48000);int n=std::max(0,total-int(count));std::vector<float> frames(n*2);for(int i=0;i<n;++i){frames[i*2]=float(amplitude.load()*std::sin(6.283185307179586*60*(count+i)/48000));frames[i*2+1]=frames[i*2]*.5f;}source->push(frames.data(),n);count+=n;Sleep(2);}});};
 initAudio(false);pump(1000);check(isAudioRunning(),"No Windows output for integration test");muteSession();start();trackSetInputMonitor(id,true);
 double base=rms();check(base>.005,"Stopped input monitoring is silent");check(!isPlaying&&sequencerCurrentStep==0,"Stopped monitoring advanced transport");
 trackSetVolume(id,.5f);double half=rms();check(std::abs(half/base-.5)<.06,"Input volume did not apply");trackSetVolume(id,1);trackSetPan(id,-1);pump(800);auto level=meter::track(id);check(level&&level->left>.035&&level->right<.002,"Input pan / stereo metering failed");trackSetPan(id,0);
 trackSetEqEnabled(id,true);trackSetFxOrder(id,{"kj:eq"});trackSetEqLowGain(id,12);double boost=rms();check(boost/base>3.3&&boost/base<4.2,"Native EQ did not process input");trackSetEqEnabled(id,false);double bypass=rms();std::cout<<"base="<<base<<" half="<<half<<" boost="<<boost<<" bypass="<<bypass<<"\n";check(std::abs(bypass/base-1)<.1,"Native bypass changed captured signal");
#ifdef KJ_ENABLE_VST3
 check(argc>=2,"Missing VST3 fixture");auto classes=kj::VST3Host::scan(argv[1]);std::string gain;for(auto& c:classes)if(c.name()=="KJ Test Gain")gain=c.ID().toString();check(!gain.empty(),"No fixture gain effect");auto slot=kj::addTrackVst3(id,false,argv[1],gain,getAudioSampleRate());trackSetFxOrder(id,{"kj:eq",slot});double plugin=rms();std::cout<<"plugin/base="<<plugin/base<<" error="<<kj::takeTrackVst3Error()<<"\n";check(std::abs(plugin/base-2)<.2,"VST3 did not process stopped input");trackSetEqEnabled(id,true);double mixed=rms();check(mixed/base>6.5&&mixed/base<8.5,"Mixed native/VST3 input rack failed");trackSetEqEnabled(id,false);kj::bypassTrackVst3(id,slot,true);check(std::abs(rms()/base-1)<.1,"VST3 bypass failed");kj::removeTrackVst3(id,slot);trackSetFxOrder(id,{});
#endif
 input::setMonitorWhileStopped(false);check(rms()<1e-9,"Stopped preference off leaked input");isPlaying=true;check(rms()>.005,"Play did not restore monitored input");isPlaying=false;check(rms()<1e-9,"Stop did not silence monitored input");input::setMonitorWhileStopped(true);check(rms()>.005,"Stopped preference on did not restore input");
 trackSetMute(id,true);check(rms()<1e-9,"Audio mute leaked input");trackSetMute(id,false);int second=addTrack().id;trackSetType(second,TrackType::AudioIn);trackSetInputDevice(second,"kj-test-source","Synthetic input");trackSetInputChannels(second,0,true);trackSetVolume(second,1);trackSetInputMonitor(second,true);double shared=rms();std::cout<<"shared/base="<<shared/base<<"\n";check(shared/base>1.8&&shared/base<2.2,"Shared capture missing from one track");trackSetSolo(id,true);check(std::abs(rms()/base-1)<.1,"Solo filtering failed");trackSetSolo(second,true);check(rms()/base>1.8,"Multiple solo failed");trackSetMute(id,true);check(std::abs(rms()/base-1)<.1,"Mute did not override solo");trackSetInputMonitor(second,false);check(rms()<1e-9,"Monitor off leaked");trackSetMute(id,false);
 check(startAudioRecording("input_integration_test.wav",error),"Cannot start master recording");pump(250);check(stopAudioRecording(error),"Cannot stop recording");std::ifstream wav("input_integration_test.wav",std::ios::binary);wav.seekg(44);double power=0;int samples=0;int16_t v;while(wav.read(reinterpret_cast<char*>(&v),2)){power+=double(v)*v;++samples;}check(samples>1000&&power>1,"Master recording did not receive input");
 amplitude=2.f;pump(150);check(meter::master().clipped,"Master clip indicator missed clipping");amplitude=.04f;pump(250);
 int midi=addTrack().id;trackSetType(midi,TrackType::MidiOut);trackSetMidiPort(midi,1234,L"Test MIDI sink");setMidiTestObserver(observeMidi);auto doc=piano::document(midi);doc.pattern.notes={{0,384,60,1.f,1.f,false}};piano::commit(midi,doc);trackSetSolo(id,false);trackSetSolo(second,false);isPlaying=true;requestSequencerReset();pump(120);check(noteOns>0,"MIDI note did not start");trackSetMute(midi,true);pump(100);check(noteOffs>0,"MIDI mute did not clean up held note");int ons=noteOns; pump(100);check(noteOns==ons,"Muted MIDI retriggered");trackSetMute(midi,false);pump(120);check(noteOns>ons,"Unmuted MIDI did not resume");int offs=noteOffs;trackSetSolo(id,true);pump(100);check(noteOffs>offs,"Solo filtering did not clean up MIDI");trackSetSolo(midi,true);pump(120);check(noteOns>ons+1,"Multiple solo did not allow MIDI");isPlaying=false;pump(100);setMidiTestObserver(nullptr);
 stop=true;producer.join();source->available=false;check(rms()<1e-9,"Device disconnect did not become silent");source->configure(2,44100);source->configure(2,48000);start();check(rms()>.005,"Reconnect failed to resume selected device");stop=true;producer.join();trackSetInputMonitor(id,false);rms();shutdownAudio();input::setTestSource("kj-test-source",nullptr);
 std::cout<<"PASS real render: input, stopped monitoring, Play/Stop, FX, pan/volume, meters, shared input, solo/mute, disconnect/reconnect and master recording\n";
 auto devices=input::devices();std::cout<<"Windows active input devices: "<<devices.size()<<'\n';for(const auto& device:devices){std::cout<<"Capture smoke: "<<device.name<<" ("<<device.channels<<" channels)\n";Track t;t.type=TrackType::AudioIn;t.inputMonitor=true;t.inputDeviceId=device.id;auto buffers=input::synchronize({t,t});check(buffers[0]==buffers[1],"Hardware stream not shared");pump(1800);std::cout<<input::status(device.id)<<" rate="<<buffers[0]->rate<<" captured frames="<<buffers[0]->written<<'\n';check(buffers[0]->available&&buffers[0]->written>0,"Windows input did not capture");input::shutdown();}
 if(devices.empty())std::cout<<"SKIP hardware / USB smoke: Windows exposes no active input endpoint\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';result=1;}
setMidiTestObserver(nullptr);stop=true;if(producer.joinable())producer.join();stopAudioRecording(error);shutdownAudio();input::shutdown();
#ifdef KJ_ENABLE_VST3
kj::clearTrackVst3();
#endif
CoUninitialize();return result;}
