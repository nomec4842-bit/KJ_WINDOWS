#include "core/audio_engine.h"
#include "core/audio_recording.h"
#include "core/sequencer.h"
#include "core/tracks.h"
#include "core/piano_pattern.h"
#include "core/track_type_sample.h"
#include "core/track_type_synth.h"
#include "core/sample_loader.h"
#include "core/project_io.h"
#include "hosting/TrackVST3.h"
#include "hosting/VST3Host.h"
#include <windows.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>
#include <psapi.h>
#include <wrl/client.h>
#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <fstream>
void check(bool value,const char* error){if(!value)throw std::runtime_error(error);}
void pump(int ms){auto end=std::chrono::steady_clock::now()+std::chrono::milliseconds(ms);do{MSG message;while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}Sleep(5);}while(std::chrono::steady_clock::now()<end);}
void muteSession(){using Microsoft::WRL::ComPtr;ComPtr<IMMDeviceEnumerator> e;ComPtr<IMMDevice>d;ComPtr<IAudioSessionManager2>m;ComPtr<IAudioSessionEnumerator>s;check(SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,IID_PPV_ARGS(&e)))&&SUCCEEDED(e->GetDefaultAudioEndpoint(eRender,eConsole,&d))&&SUCCEEDED(d->Activate(__uuidof(IAudioSessionManager2),CLSCTX_ALL,nullptr,reinterpret_cast<void**>(m.GetAddressOf())))&&SUCCEEDED(m->GetSessionEnumerator(&s)),"Cannot locate test audio session");int n=0;s->GetCount(&n);for(int i=0;i<n;++i){ComPtr<IAudioSessionControl>c;ComPtr<IAudioSessionControl2>c2;ComPtr<ISimpleAudioVolume>v;DWORD pid=0;if(SUCCEEDED(s->GetSession(i,&c))&&SUCCEEDED(c.As(&c2))&&SUCCEEDED(c2->GetProcessId(&pid))&&pid==GetCurrentProcessId()&&SUCCEEDED(c.As(&v))){check(SUCCEEDED(v->SetMute(TRUE,nullptr)),"Cannot mute test audio");return;}}throw std::runtime_error("No test session; refusing audible test playback");}
struct Usage{SIZE_T bytes;DWORD handles;};
Usage usage(){PROCESS_MEMORY_COUNTERS_EX p{};p.cb=sizeof(p);check(GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&p),sizeof(p)),"Memory query failed");DWORD handles=0;GetProcessHandleCount(GetCurrentProcess(),&handles);return {p.PrivateUsage,handles};}
int main(int argc,char** argv){CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);std::cout<<std::unitbuf;int result=0;std::wstring error;
try{
 check(argc==5,"Expected seconds, fixture, Surge instrument, Surge effects");int seconds=std::max(30,std::stoi(argv[1]));
 initTracks();initSequencer();initAudio(false);pump(1200);check(!getActiveAudioOutputDevice().id.empty(),"Output did not initialize");muteSession();double rate=getAudioSampleRate();shutdownAudio();
 int synth=getTracks().front().id,sampler=addTrack().id,plugin=addTrack().id;
 auto sample=std::make_shared<SampleBuffer>();sample->channels=1;sample->sampleRate=48000;for(int i=0;i<192000;++i)sample->samples.push_back(float(.15*std::sin(6.283185307179586*330*i/48000)));
 trackSetType(synth,TrackType::Synth);trackSetType(sampler,TrackType::Sample);trackSetType(plugin,TrackType::Vst3);trackSetSampleBuffer(sampler,sample);
 for(int id:{synth,sampler,plugin}){trackSetVolume(id,.12f);auto doc=piano::document(id);doc.pattern.notes={{0,384,id==synth?57:60,.8f,1.f,false}};piano::commit(id,doc);}
 auto inst=kj::VST3Host::scan(argv[3]).front();auto effect=kj::VST3Host::scan(argv[4]).front();kj::loadTrackVst3(plugin,true,argv[3],inst.ID().toString(),rate);auto surgeFx=kj::addTrackVst3(plugin,false,argv[4],effect.ID().toString(),rate);trackSetFxOrder(plugin,{surgeFx});
 std::string gain;for(const auto& c:kj::VST3Host::scan(argv[2]))if(c.name()=="KJ Test Gain")gain=c.ID().toString();check(!gain.empty(),"Missing gain fixture");trackSetEqEnabled(synth,true);trackSetFxOrder(synth,{"kj:eq"});
 initAudio(false);pump(800);muteSession();isPlaying=true;requestSequencerReset();pump(1000);
 std::cout<<"START seconds="<<seconds<<" rate="<<rate<<" tracks=3 synth/sampler/SurgeXT+SurgeEffects\n";
 auto start=std::chrono::steady_clock::now(),lastSignal=start,lastTransport=start;int lastStep=-1,edits=0,racks=0,transports=0,recalls=0,nextRack=2,nextTransport=15,nextRecall=60,nextReport=30;bool recording=false,recorded=false;Usage baseline=usage();SIZE_T maxBytes=baseline.bytes;DWORD maxHandles=baseline.handles;double peak=0;
 while(true){double elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();if(elapsed>=seconds)break;
  trackSetPan(synth,float(std::sin(elapsed)*.7));trackSetVolume(sampler,float(.08+.02*std::sin(elapsed)));trackSetSynthAttack(synth,float(.005+.02*(1+std::sin(elapsed))));trackSetSampleAttack(sampler,.01f);trackSetEqLowGain(synth,float(6*std::sin(elapsed)));trackSetMute(sampler,(edits%19)==0);++edits;
  if(elapsed>=nextRack){auto slot=kj::addTrackVst3(synth,false,argv[2],gain,rate);trackSetFxOrder(synth,{"kj:eq",slot});pump(30);kj::bypassTrackVst3(synth,slot,true);kj::moveTrackVst3(synth,slot,-1);kj::bypassTrackVst3(synth,slot,false);pump(30);kj::removeTrackVst3(synth,slot);trackSetFxOrder(synth,{"kj:eq"});++racks;nextRack+=2;}
  if(!recorded&&elapsed>2){check(startAudioRecording("soak_master.wav",error),"Recording start failed");recording=true;recorded=true;}
  if(recording&&elapsed>7){check(stopAudioRecording(error),"Recording stop failed");recording=false;check(std::filesystem::file_size("soak_master.wav")>100000,"Recording missing samples");}
  if(elapsed>=nextTransport){isPlaying=false;pump(80);isPlaying=true;requestSequencerReset();++transports;nextTransport+=15;lastSignal=lastTransport=std::chrono::steady_clock::now();}
  if(elapsed>=nextRecall){check(saveProjectToFile("soak_session.jik"),"Project save failed");check(loadProjectFromFile("soak_session.jik"),"Project recall failed");auto tracks=getTracks();check(tracks.size()==3,"Recall lost tracks");synth=tracks[0].id;sampler=tracks[1].id;plugin=tracks[2].id;trackSetSampleBuffer(sampler,sample);check(kj::getTrackVst3Slots(plugin).size()==2,"Recall lost Surge rack");isPlaying=true;requestSequencerReset();pump(800);muteSession();++recalls;nextRecall+=60;lastSignal=lastTransport=std::chrono::steady_clock::now();}
  pump(100);auto now=std::chrono::steady_clock::now();auto samples=getMasterWaveformSnapshot(4096);double blockPeak=0;for(float v:samples){check(std::isfinite(v),"Non-finite master output");blockPeak=std::max(blockPeak,std::abs(double(v)));}peak=std::max(peak,blockPeak);if(blockPeak>1e-7)lastSignal=now;int step=sequencerCurrentStep.load();if(step!=lastStep){lastStep=step;lastTransport=now;}check(now-lastSignal<std::chrono::seconds(3),"Audio stopped unexpectedly");check(now-lastTransport<std::chrono::seconds(3),"Sequencer stalled");
  auto pluginError=kj::takeTrackVst3Error();check(pluginError.empty(),pluginError.c_str());AudioThreadNotification notification;check(!consumeAudioThreadNotification(notification),"Audio engine raised an error");check(kj::takeTrackVst3RateRequest()==0,"Unexpected plugin rate mismatch");
  if(elapsed>=nextReport){auto u=usage();maxBytes=std::max(maxBytes,u.bytes);maxHandles=std::max(maxHandles,u.handles);if(nextReport==30)baseline=u;std::cout<<"PROGRESS seconds="<<int(elapsed)<<" edits="<<edits<<" racks="<<racks<<" recalls="<<recalls<<" privateMiB="<<u.bytes/1048576.<<" handles="<<u.handles<<" peak="<<peak<<'\n';nextReport+=30;}
 }
 auto end=usage();check(end.bytes<=baseline.bytes+256ull*1048576,"Private memory grew by more than 256 MiB after warmup");check(end.handles<=baseline.handles+64,"Handle count grew excessively");
 std::cout<<"PASS duration="<<seconds<<" edits="<<edits<<" rackCycles="<<racks<<" transportCycles="<<transports<<" recalls="<<recalls<<" privateMiB="<<end.bytes/1048576.<<" baselineMiB="<<baseline.bytes/1048576.<<" handles="<<end.handles<<" baselineHandles="<<baseline.handles<<" peak="<<peak<<'\n';
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';result=1;}
stopAudioRecording(error);shutdownAudio();kj::clearTrackVst3();CoUninitialize();std::cout<<"TEARDOWN complete\n";return result;}
