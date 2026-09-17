#include "core/piano_pattern.h"
#include <iostream>
#include <stdexcept>

void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
bool same(const piano::Pattern& a, const piano::Pattern& b) {
    if (a.length != b.length || a.notes.size() != b.notes.size()) return false;
    for (size_t i=0;i<a.notes.size();++i) {
        const auto& x=a.notes[i]; const auto& y=b.notes[i];
        if(x.start!=y.start || x.length!=y.length || x.pitch!=y.pitch || x.velocity!=y.velocity || x.probability!=y.probability || x.locked!=y.locked) return false;
    }
    return true;
}
int main() {
    try {
        initTracks(); int track=getTracks().front().id;
        for(int algorithm=0;algorithm<6;++algorithm) {
            for(bool evolve : {false,true}) {
                piano::Document d;
                d.pattern.notes={{0,24,60,0.8f,1,false},{48,24,64,0.8f,1,false}};
                d.original=d.pattern; d.hasOriginal=true;
                d.settings.algorithm=static_cast<piano::Algorithm>(algorithm);
                d.settings.evolve=evolve; d.settings.continuous=true;
                piano::commit(track,d);
                piano::prepare(track);
                d.settings.continuous=false; piano::commit(track,d);
                d.pattern.notes.push_back({96,48,79,0.7f,1,true});
                piano::commit(track,d);
                d.settings.continuous=true; piano::commit(track,d);
                auto runtime=piano::prepare(track);
                auto expected=piano::generate(d.pattern,d.settings,1,false,0);
                check(same(std::atomic_load(&runtime->next)->pattern,expected),"Restart reused the recovery snapshot");
                piano::Playback playback; std::vector<StepNoteInfo> on; std::vector<int> present; bool gate=false;
                piano::render(playback,runtime,383,false,on,present,gate);
                piano::render(playback,runtime,0,false,on,present,gate);
                check(same(piano::displayed(track),expected),"Boundary did not play the edited generation");
                auto live=piano::document(track); live.pattern=piano::displayed(track);
                live.pattern.notes.push_back({192,24,83,0.6f,1,true});
                piano::commit(track,live);
                auto edited=piano::prepare(track);
                check(edited->settings.continuous,"Live edit disabled continuous generation");
                check(same(std::atomic_load(&edited->next)->pattern,piano::generate(live.pattern,live.settings,1,false,0)),"Live edit was not used by the next generation");
                check(same(piano::document(track).original,d.original),"Recovery original was overwritten");
                auto saved=piano::serialize(track); piano::reset(); piano::deserialize(track,saved);
                check(piano::document(track).settings.continuous,"Saved live edit lost continuous state");
                check(same(std::atomic_load(&piano::prepare(track)->next)->pattern,piano::generate(live.pattern,live.settings,1,false,0)),"Loaded sequence regenerated from old notes");
            }
        }
        piano::Document sustain;
        sustain.pattern.notes={{0,192,60,0.8f,1,false}};
        piano::commit(track,sustain);
        auto heldRuntime=piano::prepare(track);
        piano::Playback held;
        std::vector<StepNoteInfo> starts; std::vector<int> present; bool gate=false;
        for(int tick=0;tick<192;++tick) {
            piano::render(held,heldRuntime,tick,false,starts,present,gate);
            check(gate && held.notes.size()==1,"Sustained note cut off before its drawn end");
            check(starts.size()==(tick==0?1u:0u),"Sustained note retriggered inside its length");
        }
        // Lengthening a held note must preserve its gate and envelope.
        sustain.pattern.notes[0].length=240;
        piano::commit(track,sustain); heldRuntime=piano::prepare(track);
        piano::render(held,heldRuntime,191,false,starts,present,gate);
        check(gate && starts.empty() && held.notes.front().sustain,"Live length edit retriggered a held note");
        piano::render(held,heldRuntime,239,false,starts,present,gate);
        check(gate && starts.empty(),"Extended note stopped too soon");
        piano::render(held,heldRuntime,240,false,starts,present,gate);
        check(!gate && held.notes.empty(),"Note did not release at its drawn end");
        sustain.pattern.notes={{0,96,60,0.8f,1,false},{48,96,60,0.5f,1,false}};
        piano::commit(track,sustain); heldRuntime=piano::prepare(track);
        held={};
        for(int tick=0;tick<=144;++tick) {
            piano::render(held,heldRuntime,tick,false,starts,present,gate);
            check(held.notes.size()==(tick<144?1u:0u),"Overlapping sustains duplicated a voice or released early");
            check(starts.size()==((tick==0||tick==48)?1u:0u),"Overlap produced extra note-on events");
        }
        // Four-step hold with a short note at each possible neighbouring position.
        // Repeat each tick like the audio callback, then wrap the pattern.
        for (int shortStart : {0,24,48,72,96}) {
            sustain.pattern.notes={{0,96,60,0.8f,1,false},{shortStart,24,64,0.8f,1,false}};
            piano::commit(track,sustain); heldRuntime=piano::prepare(track); held={};
            int heldStarts=0, shortStarts=0;
            for (int frame=0;frame<384*2*3;++frame) {
                int tick=(frame/3)%384;
                bool advanced=piano::render(held,heldRuntime,tick,false,starts,present,gate);
                for (auto n : starts) {
                    if(n.midiNote==60) ++heldStarts;
                    if(n.midiNote==64) ++shortStarts;
                }
                if(advanced && tick<96) {
                    auto n=std::find_if(held.notes.begin(),held.notes.end(),[](const StepNoteInfo& n){return n.midiNote==60;});
                    check(n!=held.notes.end(),"Neighbouring note cut off the four-step hold");
                    check(n->sustain==(tick!=0),"Neighbouring note retriggered the four-step hold");
                }
            }
            check(heldStarts==2 && shortStarts==2,"Adjacent notes produced extra triggers across loops");
        }
        sustain.pattern.notes={{0,96,60,0.0f,1,true}};
        piano::commit(track,sustain); heldRuntime=piano::prepare(track); held={};
        piano::render(held,heldRuntime,0,false,starts,present,gate);
        check(!gate && starts.empty() && held.notes.empty(),"Zero velocity note opened a playback gate");

        sustain.settings.continuous=true;
        sustain.settings.algorithm=piano::Algorithm::Chaos;
        sustain.settings.amount=0.712345659f;
        sustain.pattern.notes={{0,96,60,0.812345683f,0.912345648f,false}};
        piano::commit(track,sustain);
        auto edit=sustain; edit.pattern.notes[0].pitch=67; piano::commit(track,edit);
        check(piano::undo(track) && piano::document(track).settings.continuous,"Undo disabled continuous mode");
        check(piano::document(track).pattern.notes[0].pitch==60,"Undo lost the previous notes");
        check(piano::undo(track,true) && piano::document(track).settings.continuous,"Redo disabled continuous mode");
        heldRuntime=piano::prepare(track); held={};
        piano::render(held,heldRuntime,383,false,starts,present,gate);
        piano::render(held,heldRuntime,0,false,starts,present,gate);
        auto audible=piano::displayed(track);
        auto saved=piano::serialize(track);
        piano::reset(); piano::deserialize(track,saved);
        auto recalled=piano::document(track);
        check(same(recalled.pattern,audible),"Save did not recall the audible variation precisely");
        check(recalled.settings.continuous && recalled.settings.amount==sustain.settings.amount,"Save lost generator settings or precision");
        check(same(std::atomic_load(&piano::prepare(track)->current)->pattern,audible),"Reload playback did not start from the saved variation");
        std::cout << "Continuous editing and sustain regression tests passed\n";
        return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
