#include "core/waveform_snapshot.h"
#include "core/audio_engine.h"
#include "core/tracks.h"
#include "core/track_type_midi.h"
#include "core/track_type_synth.h"
#include "core/track_type_sample.h"
#include <array>
#include <atomic>
#include <thread>
#include <iostream>
#include <stdexcept>
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(){try{
    WaveformSnapshot<64> snapshots;std::array<float,64> samples{};samples.fill(1);check(snapshots.publish(samples.data(),64),"Initial waveform publish failed");
    snapshots.read([&](const float* held,std::size_t count){
        for(int generation=2;generation<100;++generation){samples.fill(float(generation));check(snapshots.publish(samples.data(),64),"Slow reader blocked all buffers");for(size_t i=0;i<count;++i)check(held[i]==1,"Slow waveform reader was overwritten");}
        snapshots.read([&](const float*,std::size_t){samples.fill(100);check(snapshots.publish(samples.data(),64),"Third waveform slot unavailable");snapshots.read([&](const float*,std::size_t){check(!snapshots.publish(samples.data(),64),"Writer overwrote a pinned waveform slot");});});
    });
    check(snapshots.publish(samples.data(),64),"Released waveform slots not reusable");
    std::atomic<bool> done{false},valid{true};
    auto read=[&]{do{snapshots.read([&](const float* data,size_t count){if(count!=64){valid=false;return;}float generation=data[0];std::this_thread::yield();for(size_t i=0;i<count;++i)if(data[i]!=generation)valid=false;});}while(!done);};
    std::thread readerA(read),readerB(read);
    for(int generation=101;generation<30000;++generation){samples.fill(float(generation));snapshots.publish(samples.data(),64);}done=true;readerA.join();readerB.join();check(valid,"Concurrent waveform read was torn");
    setActiveAudioOutputDevice(L"");check(setActiveAudioOutputDevice(L"pending B"),"Device request rejected");check(setActiveAudioOutputDevice(L""),"Return to default did not cancel pending device");check(getRequestedAudioOutputDeviceId().empty(),"Stale device request remained");check(!setActiveAudioOutputDevice(L""),"Duplicate device request accepted");
    std::wstring a(4096,L'A'),b(4096,L'B');setActiveAudioOutputDevice(a);done=false;
    std::thread requests([&]{for(int i=0;i<10000;++i)setActiveAudioOutputDevice(i%2?a:b);done=true;});
    do{auto id=getRequestedAudioOutputDeviceId();if(id!=a&&id!=b)valid=false;}while(!done);requests.join();check(valid,"Output device identity was torn");setActiveAudioOutputDevice(L"");
    initTracks();int id=getTracks().front().id;trackSetMidiPort(id,7,L"7");done=false;
    std::thread edits([&]{for(int i=0;i<15000;++i){int port=i%2?7:11;trackSetMidiPort(id,port,std::to_wstring(port));trackSetMidiChannel(id,i%16+1);trackSetSampleAttack(id,float(i%100)/100);trackSetSynthAttack(id,float(i%100)/100);trackSetSynthOscWavetableMix(id,0,float(i%100)/100);}done=true;});
    do{auto t=getTracks().front();if(t.midiPortName!=std::to_wstring(t.midiPort)||t.midiChannel<1||t.midiChannel>16||t.sampleAttack<0||t.sampleAttack>1||t.synthAttack<0||t.synthAttack>1||t.synthOscillators[0].wavetableMix<0||t.synthOscillators[0].wavetableMix>1)valid=false;}while(!done);edits.join();check(valid,"Track snapshot lost settings or MIDI identity consistency");
    trackSetSampleAttack(id,.23f);trackSetSynthAttack(id,.37f);trackSetSynthOscWavetableMix(id,0,.61f);auto t=getTracks().front();check(t.sampleAttack==trackGetSampleAttack(id)&&t.synthAttack==trackGetSynthAttack(id)&&t.synthOscillators[0].wavetableMix==trackGetSynthOscWavetableMix(id,0),"Snapshot and instrument getters disagree");
    std::cout<<"PASS pinned/concurrent waveform snapshots, rapid output selection, concurrent instrument edits and MIDI identity\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
