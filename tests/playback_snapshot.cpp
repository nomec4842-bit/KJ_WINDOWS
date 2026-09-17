#include "core/playback_snapshot.h"
#include <atomic>
#include <iostream>
#include <thread>

struct Notes { int generation=0; std::vector<int> pitches; };
int main() {
    PlaybackSnapshot<Notes> snapshots;
    auto fill=[](Notes& n,int generation){ n.generation=generation; n.pitches.assign(128,generation); };
    snapshots.publish([&](Notes& n){fill(n,0);});
    auto held=snapshots.read();
    // Simulate an audio block outliving many cache refreshes.
    for(int i=1;i<100;++i) snapshots.publish([&](Notes& n){fill(n,i);});
    if(held->generation!=0 || held->pitches!=std::vector<int>(128,0)) return 1;
    std::atomic<bool> done{false};
    std::thread writer([&]{
        for(int i=100;i<20000;++i) snapshots.publish([&](Notes& n){fill(n,i);});
        done.store(true);
    });
    bool valid=true;
    do {
        auto block=snapshots.read();
        for(int pitch:block->pitches) if(pitch!=block->generation) valid=false;
    } while(!done.load());
    writer.join();
    if(!valid || held->generation!=0) return 1;
    std::cout<<"Playback snapshots remain stable across concurrent refreshes\n";
}
