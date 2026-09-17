#include "gui/piano_editor.h"
#include "core/piano_pattern.h"
#include "core/sequencer.h"
#include <stdexcept>
#include <iostream>

void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
int main(){
    HWND window=nullptr;int result=0;
    try{
        initTracks();int track=getTracks().front().id;setActiveSequencerTrackId(track);
        WNDCLASSW wc{};wc.lpfnWndProc=PianoEditorWndProc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"KJPianoScrollTest";
        RegisterClassW(&wc);
        window=CreateWindowW(wc.lpszClassName,L"Piano test",WS_OVERLAPPEDWINDOW,0,0,900,600,nullptr,nullptr,wc.hInstance,nullptr);
        check(window!=nullptr,"Cannot create hidden piano editor");
        auto scroll=[&]{SCROLLINFO info{sizeof(SCROLLINFO),SIF_ALL};GetScrollInfo(window,SB_HORZ,&info);info.nMax-=info.nPage-1;return info;};
        auto click=[&]{SendMessageW(window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(100,100));SendMessageW(window,WM_LBUTTONUP,0,MAKELPARAM(100,100));};
        check(scroll().nMax==384,"Initial ghost bar missing");
        SendMessageW(window,WM_HSCROLL,SB_LINERIGHT,0);
        check(scroll().nPos==24,"Scroll arrow snapped to a whole bar");
        SendMessageW(window,WM_MOUSEHWHEEL,MAKEWPARAM(0,30),0);
        check(scroll().nPos==30,"High-resolution wheel movement was lost or snapped");
        SendMessageW(window,WM_HSCROLL,SB_RIGHT,0);
        check(scroll().nPos==384&&piano::document(track).pattern.length==384&&trackGetStepCount(track)==16,"Scrolling extended sequence");
        SendMessageW(window,WM_KEYDOWN,VK_NEXT,0);
        check(scroll().nPos==384,"Scrolled beyond the single ghost bar");
        SendMessageW(window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(100,100));
        SendMessageW(window,WM_KEYDOWN,VK_ESCAPE,0);
        check(piano::document(track).pattern.notes.empty()&&trackGetStepCount(track)==16,"Cancelled drawing extended sequence");
        click();auto d=piano::document(track);
        check(d.pattern.length==768&&trackGetStepCount(track)==32&&d.pattern.notes.size()==1&&d.pattern.notes[0].start==384,"Populating ghost failed to extend by a bar");
        check(scroll().nMax==768,"Next ghost bar missing");
        SendMessageW(window,WM_KEYDOWN,VK_NEXT,0);click();
        d=piano::document(track);
        check(d.pattern.length==1152&&trackGetStepCount(track)==48&&d.pattern.notes.back().start==768,"Second ghost bar did not extend");
        SendMessageW(window,WM_COMMAND,6000,0); // Undo
        check(piano::document(track).pattern.length==768&&trackGetStepCount(track)==32&&scroll().nMax==768,"Undo did not restore sequence length");
        SendMessageW(window,WM_COMMAND,6001,0); // Redo
        check(piano::document(track).pattern.length==1152&&trackGetStepCount(track)==48,"Redo did not restore sequence length");
        auto saved=piano::serialize(track);piano::reset();piano::deserialize(track,saved);
        check(piano::document(track).pattern.length==1152&&piano::document(track).pattern.notes.back().start==768,"Extended pattern did not serialize");
        SendMessageW(window,WM_HSCROLL,SB_RIGHT,0);
        check(scroll().nPos==1152&&trackGetStepCount(track)==48,"End navigation changed sequence length");
        SendMessageW(window,WM_HSCROLL,SB_LINELEFT,0);
        check(scroll().nPos==1128,"Left scroll did not move within the bar");
        SendMessageW(window,WM_COMMAND,6006,0); // Select all
        SendMessageW(window,WM_COMMAND,6003,0); // Copy
        SendMessageW(window,WM_HSCROLL,SB_RIGHT,0);
        SendMessageW(window,WM_COMMAND,6004,0); // Paste into ghost
        check(piano::document(track).pattern.length==1536&&trackGetStepCount(track)==64,"Pasting into ghost did not extend sequence");
        SendMessageW(window,WM_COMMAND,6000,0);
        check(piano::document(track).pattern.length==1152&&trackGetStepCount(track)==48,"Paste undo failed");
        // Delete the note in the last populated bar through the editor.
        SendMessageW(window,WM_HSCROLL,SB_LEFT,0);
        SendMessageW(window,WM_KEYDOWN,VK_NEXT,0);
        SendMessageW(window,WM_KEYDOWN,VK_NEXT,0);
        SendMessageW(window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(100,100));
        SendMessageW(window,WM_CANCELMODE,0,0);
        SendMessageW(window,WM_KEYDOWN,VK_DELETE,0);
        check(piano::document(track).pattern.length==768&&trackGetStepCount(track)==32&&scroll().nMax==768,"Empty last bar did not become ghost");
        SendMessageW(window,WM_COMMAND,6000,0);
        check(piano::document(track).pattern.length==1152&&trackGetStepCount(track)==48,"Undo deletion did not restore final bar");
        SendMessageW(window,WM_COMMAND,6006,0);
        SendMessageW(window,WM_KEYDOWN,VK_DELETE,0);
        check(piano::document(track).pattern.length==384&&trackGetStepCount(track)==16,"Empty trailing bars were not removed");
        SendMessageW(window,WM_COMMAND,6000,0); // Restore populated bars.
        setPianoTrimEmptyBars(false);
        SendMessageW(window,WM_COMMAND,6006,0);
        SendMessageW(window,WM_KEYDOWN,VK_DELETE,0);
        check(piano::document(track).pattern.notes.empty()&&piano::document(track).pattern.length==1152&&trackGetStepCount(track)==48,"Disabled trimming still shortened sequence");
        SendMessageW(window,WM_HSCROLL,SB_RIGHT,0);click();
        check(piano::document(track).pattern.length==1536&&trackGetStepCount(track)==64,"Disabled trimming prevented ghost expansion");
        setPianoTrimEmptyBars(true);
        std::cout<<"PASS one-bar scrolling, ghost boundaries, cancellation, population, undo/redo and serialization\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';result=1;}
    if(window)DestroyWindow(window);return result;
}
