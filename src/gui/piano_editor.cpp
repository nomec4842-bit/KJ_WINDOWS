#include "gui/piano_editor.h"
#include "core/piano_pattern.h"
#include "core/sequencer.h"
#include "core/track_type_sample.h"
#include "core/audio_engine.h"
#include <windowsx.h>
#include <commctrl.h>
#include <algorithm>
#include <cmath>
#include <set>
#include <string>

namespace {
bool trimEmptyBars = true;
enum Command { Undo=6000, Redo, Cut, Copy, Paste, Delete, SelectAll, ClearSelection,
    Up, Down, OctaveUp, OctaveDown, Velocity, Shorter, Longer, Lock, Unlock,
    Snap, Quarter, Eighth, Sixteenth, ThirtySecond, Triplets,
    ZoomIn, ZoomOut, Names, Velocities, Follow, Previous, Next,
    Every1, Every2, Every4, Every8, EveryCustom, Evolve, Original, Freeze, Restore,
    Amount, Seed, Hits, Chromatic, Major, Minor, KeyBase=6100, AlgorithmBase=6200 };
const wchar_t* algorithms[] = {L"Scramble",L"Chaos",L"Probability",L"Euclidean Rhythm",L"Random Walk",L"Mutate"};
const wchar_t* keys[] = {L"C",L"C#",L"D",L"D#",L"E",L"F",L"F#",L"G",L"G#",L"A",L"A#",L"B"};
struct State {
    int track=0, offset=0, span=384, topPitch=83, drumOffset=0, grid=24, cursor=0;
    bool snap=true, triplets=false, names=true, velocity=true, follow=false, dragging=false, resizing=false, lane=false;
    int anchorTick=0, anchorPitch=0, horizontalRemainder=0;
    piano::Document before, edit;
    std::set<size_t> selected;
    std::vector<piano::Note> clipboard;
};
constexpr int barTicks = piano::ticksPerBeat * 4;
constexpr int maxTicks = kMaxSequencerSteps * piano::ticksPerStep;
int ghostStart(int length) { return (length + barTicks - 1) / barTicks * barTicks; }
int lastBar(int length) { return std::min(ghostStart(length), maxTicks - barTicks); }
int editableEnd(int length) { return std::min(maxTicks, ghostStart(length) + barTicks); }
void fitLengthToNotes(piano::Document& d) {
    int end = trimEmptyBars ? barTicks : d.pattern.length;
    for (const auto& n : d.pattern.notes) end = std::max(end, n.start + n.length);
    d.pattern.length = std::min(maxTicks, ghostStart(end));
}
struct Layout { RECT grid, lane, status; int rows; };
bool drums(int track) { return trackGetType(track)==TrackType::Sample && trackGetSampleDrumMode(track); }
Layout layout(HWND hwnd, const State& s) {
    RECT c{}; GetClientRect(hwnd,&c);
    int rows=24; if (drums(s.track)) { auto bank=sampleGetBankBuffers(); rows=std::clamp(bank ? static_cast<int>(bank->size())-s.drumOffset : 1,1,12); }
    return {{90,24,std::max<LONG>(100,c.right-12),std::max<LONG>(100,c.bottom-(s.velocity?110:38))},
        {90,c.bottom-98,c.right-12,c.bottom-38}, {12,c.bottom-28,c.right-12,c.bottom-4},rows};
}
int quantum(const State& s) { return s.snap ? std::max(1,s.triplets?s.grid*2/3:s.grid) : 1; }
int tickAt(const State& s,const Layout& l,int x) { int t=s.offset+static_cast<int>(double(x-l.grid.left)*s.span/std::max<LONG>(1,l.grid.right-l.grid.left)); return std::max(0,t/quantum(s)*quantum(s)); }
int pitchAt(const State& s,const Layout& l,int y) { int row=std::clamp<int>((y-l.grid.top)*l.rows/std::max<LONG>(1,l.grid.bottom-l.grid.top),0,l.rows-1); return drums(s.track)?kSampleDrumNoteBase+s.drumOffset+row:std::clamp(s.topPitch-row,0,127); }
RECT noteRect(const State& s,const Layout& l,const piano::Note& n) {
    int row=drums(s.track)?n.pitch-kSampleDrumNoteBase-s.drumOffset:s.topPitch-n.pitch;
    int w=l.grid.right-l.grid.left,h=l.grid.bottom-l.grid.top;
    return {l.grid.left+MulDiv(n.start-s.offset,w,s.span),l.grid.top+row*h/l.rows+1,
        l.grid.left+MulDiv(n.start+n.length-s.offset,w,s.span),l.grid.top+(row+1)*h/l.rows-1};
}
void text(HDC dc,RECT r,const std::wstring& value,UINT style=DT_LEFT|DT_VCENTER|DT_SINGLELINE) { DrawTextW(dc,value.c_str(),-1,&r,style); }
void fill(HDC dc,RECT r,COLORREF color) { auto b=CreateSolidBrush(color); FillRect(dc,&r,b); DeleteObject(b); }
struct NumberDialog { int value, min, max; bool done=false, accepted=false; HWND edit=nullptr; };
LRESULT CALLBACK NumberProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    auto* s=reinterpret_cast<NumberDialog*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    if (msg==WM_CREATE) { s=static_cast<NumberDialog*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams); SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(s));
        auto label=L"Range: "+std::to_wstring(s->min)+L" to "+std::to_wstring(s->max);
        CreateWindowW(L"STATIC",label.c_str(),WS_CHILD|WS_VISIBLE,16,12,280,22,hwnd,nullptr,nullptr,nullptr);
        s->edit=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",std::to_wstring(s->value).c_str(),WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_NUMBER,16,40,280,26,hwnd,nullptr,nullptr,nullptr);
        SendMessageW(s->edit,EM_SETLIMITTEXT,9,0);
        CreateWindowW(L"BUTTON",L"Apply",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_DEFPUSHBUTTON,116,82,85,28,hwnd,reinterpret_cast<HMENU>(IDOK),nullptr,nullptr);
        CreateWindowW(L"BUTTON",L"Cancel",WS_CHILD|WS_VISIBLE|WS_TABSTOP,211,82,85,28,hwnd,reinterpret_cast<HMENU>(IDCANCEL),nullptr,nullptr);
        SetFocus(s->edit); SendMessageW(s->edit,EM_SETSEL,0,-1); return 0;
    }
    if (msg==WM_COMMAND && s) { if (LOWORD(wp)==IDOK) { wchar_t buf[32]{}; GetWindowTextW(s->edit,buf,32); wchar_t* end; long n=wcstol(buf,&end,10); if(end==buf||*end||n<s->min||n>s->max) { MessageBeep(MB_ICONWARNING); return 0; } s->value=static_cast<int>(n); s->accepted=true; s->done=true; }
        if(LOWORD(wp)==IDCANCEL) s->done=true; return 0; }
    if(msg==WM_CLOSE && s) {s->done=true;return 0;} return DefWindowProcW(hwnd,msg,wp,lp);
}
bool number(HWND owner,const wchar_t* title,int& value,int min,int max) {
    static bool registered=false; if(!registered){WNDCLASSW wc{};wc.lpfnWndProc=NumberProc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"KJPianoNumber";wc.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_BTNFACE+1);registered=RegisterClassW(&wc)!=0;}
    NumberDialog s{value,min,max}; RECT r{};GetWindowRect(owner,&r);
    HWND win=CreateWindowExW(WS_EX_DLGMODALFRAME,L"KJPianoNumber",title,WS_CAPTION|WS_SYSMENU,r.left+60,r.top+60,330,160,owner,nullptr,GetModuleHandleW(nullptr),&s);
    if(!win)return false; EnableWindow(owner,FALSE);ShowWindow(win,SW_SHOW);
    MSG msg{};while(!s.done){int result=GetMessageW(&msg,nullptr,0,0);if(result<=0){if(result==0)PostQuitMessage(static_cast<int>(msg.wParam));break;}if(!IsDialogMessageW(win,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}}
    EnableWindow(owner,TRUE);DestroyWindow(win);SetForegroundWindow(owner);if(s.accepted)value=s.value;return s.accepted;
}
void menu(HWND hwnd,State& s) {
    HMENU bar=CreateMenu(); auto sub=[&](const wchar_t* label){auto m=CreatePopupMenu();AppendMenuW(bar,MF_POPUP,reinterpret_cast<UINT_PTR>(m),label);return m;};
    auto item=[](HMENU m,int id,const wchar_t* label,bool checked=false){AppendMenuW(m,MF_STRING|(checked?MF_CHECKED:0),id,label);};
    auto edit=sub(L"&Edit");item(edit,Undo,L"Undo\tCtrl+Z");item(edit,Redo,L"Redo\tCtrl+Y");AppendMenuW(edit,MF_SEPARATOR,0,nullptr);
    item(edit,Cut,L"Cut\tCtrl+X");item(edit,Copy,L"Copy\tCtrl+C");item(edit,Paste,L"Paste at cursor\tCtrl+V");item(edit,Delete,L"Delete\tDel");item(edit,SelectAll,L"Select All\tCtrl+A");item(edit,ClearSelection,L"Deselect\tEsc");
    auto notes=sub(L"&Notes"); item(notes,Up,L"Transpose +1 semitone");item(notes,Down,L"Transpose -1 semitone");item(notes,OctaveUp,L"Transpose +1 octave");item(notes,OctaveDown,L"Transpose -1 octave");item(notes,Velocity,L"Set velocity...");item(notes,Shorter,L"Shorten by grid unit");item(notes,Longer,L"Lengthen by grid unit");item(notes,Lock,L"Lock selected notes");item(notes,Unlock,L"Unlock selected notes");
    auto grid=sub(L"&Grid");item(grid,Snap,L"Snap",s.snap);item(grid,Quarter,L"1/4",s.grid==96);item(grid,Eighth,L"1/8",s.grid==48);item(grid,Sixteenth,L"1/16",s.grid==24);item(grid,ThirtySecond,L"1/32",s.grid==12);item(grid,Triplets,L"Triplets",s.triplets);
    auto view=sub(L"&View");item(view,ZoomIn,L"Zoom in");item(view,ZoomOut,L"Zoom out");item(view,Previous,L"Previous bar\tPgUp");item(view,Next,L"Next bar\tPgDn");item(view,Names,L"Note names",s.names);item(view,Velocities,L"Velocity lane",s.velocity);item(view,Follow,L"Follow playback",s.follow);
    auto generate=sub(L"&Generate");auto d=piano::document(s.track);
    for(int i=0;i<6;++i){auto m=CreatePopupMenu();AppendMenuW(generate,MF_POPUP,reinterpret_cast<UINT_PTR>(m),algorithms[i]);item(m,AlgorithmBase+i*2,L"Generate once");item(m,AlgorithmBase+i*2+1,L"Continuous",d.settings.continuous&&static_cast<int>(d.settings.algorithm)==i);}
    AppendMenuW(generate,MF_SEPARATOR,0,nullptr);item(generate,Every1,L"Every loop",d.settings.every==1);item(generate,Every2,L"Every 2 loops",d.settings.every==2);item(generate,Every4,L"Every 4 loops",d.settings.every==4);item(generate,Every8,L"Every 8 loops",d.settings.every==8);item(generate,EveryCustom,L"Every N loops...");
    item(generate,Original,L"From original",!d.settings.evolve);item(generate,Evolve,L"Evolve previous",d.settings.evolve);item(generate,Amount,L"Intensity / probability / density...");item(generate,Hits,L"Euclidean hit count...");item(generate,Seed,L"Seed...");
    auto scale=CreatePopupMenu();AppendMenuW(generate,MF_POPUP,reinterpret_cast<UINT_PTR>(scale),L"Scale");item(scale,Chromatic,L"Chromatic",d.settings.scale==0);item(scale,Major,L"Major",d.settings.scale==1);item(scale,Minor,L"Natural minor",d.settings.scale==2);
    auto key=CreatePopupMenu();AppendMenuW(generate,MF_POPUP,reinterpret_cast<UINT_PTR>(key),L"Key");for(int i=0;i<12;++i)item(key,KeyBase+i,keys[i],d.settings.key==i);
    item(generate,Freeze,L"Freeze current variation");item(generate,Restore,L"Restore original pattern");
    HMENU old=GetMenu(hwnd);SetMenu(hwnd,bar);if(old)DestroyMenu(old);DrawMenuBar(hwnd);
}
void refresh(HWND hwnd,State& s) {
    int length = piano::document(s.track).pattern.length;
    s.offset = std::clamp(s.offset, 0, lastBar(length));
    SCROLLINFO info{sizeof(SCROLLINFO), SIF_RANGE | SIF_PAGE | SIF_POS};
    info.nMin=0;info.nMax=lastBar(length)+s.span-1;info.nPage=s.span;info.nPos=s.offset;
    SetScrollInfo(hwnd,SB_HORZ,&info,TRUE);
    InvalidateRect(hwnd,nullptr,FALSE);
    // Scrollbar tracking runs a modal loop; paint now rather than waiting for
    // a queued WM_PAINT after the user releases the thumb.
    UpdateWindow(hwnd);
}
void command(HWND hwnd,State& s,int id) {
    // Commands act on committed notes, never on an unfinished mouse gesture.
    if(s.dragging){s.dragging=false;s.selected.clear();ReleaseCapture();}
    auto d=piano::document(s.track); bool save=false;
    if (id==SelectAll || id==Copy || id==Cut || id==Paste || id==Delete || (id>=Up && id<=Unlock))
        d.pattern=piano::displayed(s.track);
    if(id==Undo||id==Redo){piano::undo(s.track,id==Redo);s.selected.clear();}
    else if(id==SelectAll){s.selected.clear();for(size_t i=0;i<d.pattern.notes.size();++i)s.selected.insert(i);}
    else if(id==ClearSelection)s.selected.clear();
    else if(id==Copy||id==Cut){s.clipboard.clear();int first=d.pattern.length;for(auto i:s.selected)if(i<d.pattern.notes.size()){s.clipboard.push_back(d.pattern.notes[i]);first=std::min(first,d.pattern.notes[i].start);}for(auto& n:s.clipboard)n.start-=first;if(id==Cut)command(hwnd,s,Delete);}
    else if(id==Paste){for(auto n:s.clipboard){n.start+=s.cursor;if(n.start<editableEnd(d.pattern.length)&&d.pattern.notes.size()<2048){n.length=std::min(n.length,editableEnd(d.pattern.length)-n.start);n.locked=false;d.pattern.notes.push_back(n);}}save=true;s.selected.clear();}
    else if(id==Delete){std::vector<piano::Note> keep;for(size_t i=0;i<d.pattern.notes.size();++i)if(!s.selected.count(i)||d.pattern.notes[i].locked)keep.push_back(d.pattern.notes[i]);d.pattern.notes=std::move(keep);save=true;s.selected.clear();}
    else if(id>=Up&&id<=Unlock){int velocity=80;if(id==Velocity&&!number(hwnd,L"Velocity (%)",velocity,0,100))return;
        for(auto i:s.selected)if(i<d.pattern.notes.size()){auto& n=d.pattern.notes[i];if(id==Lock)n.locked=true;else if(id==Unlock)n.locked=false;else if(!n.locked){if(id==Velocity)n.velocity=velocity/100.0f;else if(id==Shorter)n.length=std::max(1,n.length-quantum(s));else if(id==Longer)n.length=std::min(n.length+quantum(s),editableEnd(d.pattern.length)-n.start);else if(!drums(s.track)){int delta=id==Up?1:id==Down?-1:id==OctaveUp?12:-12;n.pitch=std::clamp(n.pitch+delta,0,127);}}}save=true;}
    else if(id==Snap)s.snap=!s.snap;
    else if(id>=Quarter&&id<=ThirtySecond)s.grid=96>>(id-Quarter);
    else if(id==Triplets)s.triplets=!s.triplets;
    else if(id==ZoomIn)s.span=std::max(96,s.span/2);
    else if(id==ZoomOut)s.span=std::min(24576,s.span*2);
    else if(id==Names)s.names=!s.names;
    else if(id==Velocities)s.velocity=!s.velocity;
    else if(id==Follow)s.follow=!s.follow;
    else if(id==Previous){s.offset=std::max(0,s.offset-barTicks);s.cursor=s.offset;}
    else if(id==Next){s.offset=std::min(lastBar(d.pattern.length),s.offset+barTicks);s.cursor=s.offset;}
    else if(id>=AlgorithmBase&&id<AlgorithmBase+12){int a=(id-AlgorithmBase)/2;bool continuous=(id-AlgorithmBase)%2;bool was=d.settings.continuous&&static_cast<int>(d.settings.algorithm)==a;if(continuous&&d.settings.continuous)d.pattern=piano::displayed(s.track);d.settings.algorithm=static_cast<piano::Algorithm>(a);d.settings.continuous=continuous&&!was;if(!d.hasOriginal){d.original=d.pattern;d.hasOriginal=true;}piano::commit(s.track,d);if(!continuous)piano::generateOnce(s.track);s.selected.clear();}
    else if(id==Freeze){piano::freeze(s.track);s.selected.clear();}
    else if(id==Restore){piano::restoreOriginal(s.track);s.selected.clear();}
    else if(id==Original||id==Evolve){d.settings.evolve=id==Evolve;save=true;}
    else if(id>=Every1&&id<=Every8){d.settings.every=1<<(id-Every1);save=true;}
    else if(id==EveryCustom){int v=d.settings.every;if(number(hwnd,L"Regenerate every N loops",v,1,64)){d.settings.every=v;save=true;}}
    else if(id==Amount){int v=static_cast<int>(d.settings.amount*100);if(number(hwnd,L"Intensity / probability / density (%)",v,0,100)){d.settings.amount=v/100.0f;save=true;}}
    else if(id==Seed){int v=static_cast<int>(d.settings.seed%1000000000);if(number(hwnd,L"Repeatable seed",v,0,999999999)){d.settings.seed=v;save=true;}}
    else if(id==Hits){int v=d.settings.hits;if(number(hwnd,L"Euclidean hits per full pattern",v,1,d.pattern.length/24)){d.settings.hits=v;save=true;}}
    else if(id>=Chromatic&&id<=Minor){d.settings.scale=id-Chromatic;save=true;}
    else if(id>=KeyBase&&id<KeyBase+12){d.settings.key=id-KeyBase;save=true;}
    if(save){if(id==Paste||id==Delete||(id>=Up&&id<=Unlock))fitLengthToNotes(d);piano::commit(s.track,d);}
    menu(hwnd,s);refresh(hwnd,s);
}
}

void setPianoTrimEmptyBars(bool enabled) { trimEmptyBars = enabled; }
bool pianoTrimEmptyBars() { return trimEmptyBars; }

LRESULT CALLBACK PianoEditorWndProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    auto* s=reinterpret_cast<State*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    if(msg==WM_CREATE){s=new State;s->track=getActiveSequencerTrackId();SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(s));menu(hwnd,*s);refresh(hwnd,*s);SetTimer(hwnd,8,30,nullptr);return 0;}
    if(!s)return DefWindowProcW(hwnd,msg,wp,lp);
    if(msg==WM_NCDESTROY){KillTimer(hwnd,8);delete s;SetWindowLongPtrW(hwnd,GWLP_USERDATA,0);return DefWindowProcW(hwnd,msg,wp,lp);}
    if(msg==WM_GETMINMAXINFO){auto* m=reinterpret_cast<MINMAXINFO*>(lp);m->ptMinTrackSize={640,400};return 0;}
    if(msg==WM_COMMAND){command(hwnd,*s,LOWORD(wp));return 0;}
    if(msg==WM_KEYDOWN && wp==VK_ESCAPE && s->dragging){s->dragging=false;s->selected.clear();ReleaseCapture();refresh(hwnd,*s);return 0;}
    if(msg==WM_TIMER){int track=getActiveSequencerTrackId();if(trackGetType(track)==TrackType::AudioIn){DestroyWindow(hwnd);return 0;}if(track!=s->track){s->track=track;s->selected.clear();s->offset=0;s->dragging=false;menu(hwnd,*s);}if(s->follow&&isPlaying.load()){int tick=sequencerCurrentStep.load()*24;if(tick<s->offset||tick>=s->offset+s->span)s->offset=tick/s->span*s->span;}refresh(hwnd,*s);return 0;}
    if(msg==WM_KEYDOWN){bool ctrl=GetKeyState(VK_CONTROL)<0;int id=0;if(ctrl){if(wp=='Z')id=Undo;if(wp=='Y')id=Redo;if(wp=='X')id=Cut;if(wp=='C')id=Copy;if(wp=='V')id=Paste;if(wp=='A')id=SelectAll;}else{if(wp==VK_DELETE)id=Delete;if(wp==VK_ESCAPE)id=ClearSelection;if(wp==VK_PRIOR)id=Previous;if(wp==VK_NEXT)id=Next;}if(id){command(hwnd,*s,id);return 0;}}
    if(msg==WM_HSCROLL){
        if(s->dragging)return 0;
        int offset=s->offset;SCROLLINFO info{sizeof(SCROLLINFO),SIF_TRACKPOS};GetScrollInfo(hwnd,SB_HORZ,&info);
        switch(LOWORD(wp)){
        case SB_LINELEFT:offset-=piano::ticksPerStep;break;
        case SB_PAGELEFT:offset-=barTicks;break;
        case SB_LINERIGHT:offset+=piano::ticksPerStep;break;
        case SB_PAGERIGHT:offset+=barTicks;break;
        case SB_THUMBTRACK:case SB_THUMBPOSITION:offset=info.nTrackPos;break;
        case SB_LEFT:offset=0;break;case SB_RIGHT:offset=lastBar(piano::document(s->track).pattern.length);break;
        }
        s->offset=std::clamp(offset,0,lastBar(piano::document(s->track).pattern.length));
        s->cursor=s->offset;
        refresh(hwnd,*s);return 0;
    }
    if(msg==WM_MOUSEHWHEEL || (msg==WM_MOUSEWHEEL && (GET_KEYSTATE_WPARAM(wp)&MK_SHIFT))){
        if(s->dragging)return 0;
        int delta=GET_WHEEL_DELTA_WPARAM(wp);
        if(msg==WM_MOUSEWHEEL)delta=-delta;
        s->horizontalRemainder+=delta*piano::ticksPerStep;
        s->offset=std::clamp(s->offset+s->horizontalRemainder/WHEEL_DELTA,0,lastBar(piano::document(s->track).pattern.length));
        s->horizontalRemainder%=WHEEL_DELTA;
        s->cursor=s->offset;
        refresh(hwnd,*s);
        return 0;
    }
    if(msg==WM_MOUSEWHEEL){int delta=GET_WHEEL_DELTA_WPARAM(wp)/WHEEL_DELTA;if(GetKeyState(VK_CONTROL)<0)command(hwnd,*s,delta>0?ZoomIn:ZoomOut);else if(drums(s->track)){auto bank=sampleGetBankBuffers();s->drumOffset=std::clamp(s->drumOffset-delta*3,0,std::max(0,static_cast<int>(bank?bank->size():0)-12));}else s->topPitch=std::clamp(s->topPitch+delta*3,23,127);refresh(hwnd,*s);return 0;}
    if(msg==WM_LBUTTONDOWN||msg==WM_RBUTTONUP){SetFocus(hwnd);auto l=layout(hwnd,*s);POINT pt{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};auto d=piano::document(s->track);d.pattern=piano::displayed(s->track);int hit=-1;
        for(int i=static_cast<int>(d.pattern.notes.size())-1;i>=0;--i){auto rect=noteRect(*s,l,d.pattern.notes[i]);if(PtInRect(&rect,pt)&&PtInRect(&l.grid,pt)){hit=i;break;}}
        if(msg==WM_RBUTTONUP){if(hit>=0){s->selected={static_cast<size_t>(hit)};auto m=CreatePopupMenu();AppendMenuW(m,MF_STRING,Delete,L"Delete");AppendMenuW(m,MF_STRING,Lock,L"Lock");AppendMenuW(m,MF_STRING,Unlock,L"Unlock");POINT screen=pt;ClientToScreen(hwnd,&screen);int id=TrackPopupMenu(m,TPM_RETURNCMD|TPM_RIGHTBUTTON,screen.x,screen.y,0,hwnd,nullptr);DestroyMenu(m);if(id)command(hwnd,*s,id);}return 0;}
        if(drums(s->track)&&pt.y>=l.grid.top&&pt.y<l.grid.bottom&&pt.x<l.grid.right){
            trackSetSelectedDrum(s->track,pitchAt(*s,l,pt.y)-kSampleDrumNoteBase);
            if(pt.x<l.grid.left){refresh(hwnd,*s);return 0;}
        }
        if(PtInRect(&l.grid,pt)){if(tickAt(*s,l,pt.x)>=editableEnd(d.pattern.length))return 0;s->cursor=std::min(editableEnd(d.pattern.length)-1,tickAt(*s,l,pt.x));s->anchorTick=s->cursor;s->anchorPitch=pitchAt(*s,l,pt.y);s->before=d;s->edit=d;s->lane=false;
            if(hit>=0){if(GetKeyState(VK_CONTROL)<0){if(s->selected.count(hit))s->selected.erase(hit);else s->selected.insert(hit);refresh(hwnd,*s);return 0;}if(!s->selected.count(hit))s->selected={static_cast<size_t>(hit)};s->resizing=std::abs(pt.x-noteRect(*s,l,d.pattern.notes[hit]).right)<=6;}
            else{if(d.pattern.notes.size()>=2048)return 0;auto bank=sampleGetBankBuffers();if(drums(s->track)&&(!bank||bank->empty()))return 0;if(GetKeyState(VK_CONTROL)<0){s->selected.clear();return 0;}s->edit.pattern.notes.push_back({s->cursor,std::min(quantum(*s),editableEnd(d.pattern.length)-s->cursor),s->anchorPitch,0.8f,1.0f,false});s->selected={s->edit.pattern.notes.size()-1};s->resizing=true;}
            s->dragging=true;SetCapture(hwnd);refresh(hwnd,*s);return 0;
        }
        if(s->velocity&&PtInRect(&l.lane,pt)){int tick=tickAt(*s,l,pt.x);s->before=d;s->edit=d;s->selected.clear();for(size_t i=0;i<d.pattern.notes.size();++i)if(std::abs(d.pattern.notes[i].start-tick)<=quantum(*s))s->selected.insert(i);s->lane=true;s->dragging=true;SetCapture(hwnd);SendMessageW(hwnd,WM_MOUSEMOVE,wp,lp);return 0;}
    }
    if(msg==WM_MOUSEMOVE&&s->dragging){auto l=layout(hwnd,*s);int x=GET_X_LPARAM(lp),y=GET_Y_LPARAM(lp);int tick=tickAt(*s,l,x),pitch=pitchAt(*s,l,y);auto baseline=s->before.pattern;
        if(s->edit.pattern.notes.size()>baseline.notes.size())baseline=s->edit.pattern;
        for(auto i:s->selected)if(i<s->edit.pattern.notes.size()&&i<baseline.notes.size()){auto& n=s->edit.pattern.notes[i];const auto& old=baseline.notes[i];if(n.locked)continue;
            if(s->lane)n.velocity=std::clamp(float(l.lane.bottom-y)/std::max<LONG>(1,l.lane.bottom-l.lane.top),0.0f,1.0f);
            else if(s->resizing){
                bool drawingNew=s->edit.pattern.notes.size()>s->before.pattern.notes.size();
                int end=drawingNew ? tick+quantum(*s) : old.start+old.length+tick-s->anchorTick;
                n.length=std::clamp(end-old.start,1,editableEnd(s->before.pattern.length)-old.start);
            }
            else{n.start=std::clamp(old.start+tick-s->anchorTick,0,editableEnd(s->before.pattern.length)-old.length);if(!drums(s->track))n.pitch=std::clamp(old.pitch+pitch-s->anchorPitch,0,127);}
        }refresh(hwnd,*s);return 0;}
    if(msg==WM_LBUTTONUP&&s->dragging){s->dragging=false;fitLengthToNotes(s->edit);piano::commit(s->track,s->edit);s->selected.clear();ReleaseCapture();menu(hwnd,*s);refresh(hwnd,*s);return 0;}
    if(msg==WM_CANCELMODE||msg==WM_CAPTURECHANGED){s->dragging=false;if(msg==WM_CANCELMODE&&GetCapture()==hwnd)ReleaseCapture();refresh(hwnd,*s);return 0;}
    if(msg==WM_PAINT){PAINTSTRUCT ps{};HDC target=BeginPaint(hwnd,&ps);RECT c{};GetClientRect(hwnd,&c);HDC dc=CreateCompatibleDC(target);HBITMAP bitmap=CreateCompatibleBitmap(target,std::max<LONG>(1,c.right),std::max<LONG>(1,c.bottom));auto old=SelectObject(dc,bitmap);SetBkMode(dc,TRANSPARENT);SelectObject(dc,GetStockObject(DEFAULT_GUI_FONT));SetTextColor(dc,RGB(220,225,235));fill(dc,c,RGB(22,24,29));auto l=layout(hwnd,*s);
        auto p=s->dragging?s->edit.pattern:piano::displayed(s->track);auto doc=piano::document(s->track);
        for(int row=0;row<l.rows;++row){int pitch=drums(s->track)?128+s->drumOffset+row:s->topPitch-row;int pc=pitch%12;bool black=!drums(s->track)&&(pc==1||pc==3||pc==6||pc==8||pc==10);RECT r{l.grid.left,l.grid.top+row*(l.grid.bottom-l.grid.top)/l.rows,l.grid.right,l.grid.top+(row+1)*(l.grid.bottom-l.grid.top)/l.rows};fill(dc,r,black?RGB(31,34,40):RGB(39,42,49));if(s->names){r.left=8;r.right=85;std::wstring name=drums(s->track)?L"Sample "+std::to_wstring(pitch-127):std::wstring(keys[pc])+std::to_wstring(pitch/12-1);if(drums(s->track)&&pitch-kSampleDrumNoteBase==trackGetSelectedDrum(s->track))fill(dc,r,RGB(0,90,130));text(dc,r,name);}}
        int ghostX=l.grid.left+MulDiv(ghostStart(doc.pattern.length)-s->offset,l.grid.right-l.grid.left,s->span);
        if(ghostX<l.grid.right && doc.pattern.length<maxTicks){
            int ghostRight=std::min<int>(l.grid.right,l.grid.left+MulDiv(editableEnd(doc.pattern.length)-s->offset,l.grid.right-l.grid.left,s->span));
            RECT ghost{std::max<LONG>(l.grid.left,ghostX),l.grid.top,ghostRight,l.grid.bottom};
            // Keep pitch lanes readable in the empty extension area.
            for(int row=0;row<l.rows;++row){
                int pc=(s->topPitch-row)%12;
                bool black=!drums(s->track)&&(pc==1||pc==3||pc==6||pc==8||pc==10);
                RECT lane{ghost.left,l.grid.top+row*(l.grid.bottom-l.grid.top)/l.rows,ghost.right,l.grid.top+(row+1)*(l.grid.bottom-l.grid.top)/l.rows};
                fill(dc,lane,black?RGB(26,30,37):RGB(34,38,45));
            }
        }
        SaveDC(dc);IntersectClipRect(dc,l.grid.left,0,l.grid.right,l.grid.bottom);
        auto pen=CreatePen(PS_SOLID,1,RGB(65,70,80));auto oldPen=SelectObject(dc,pen);int q=s->triplets?s->grid*2/3:s->grid;for(int t=s->offset/q*q;t<=s->offset+s->span;t+=q){int x=l.grid.left+MulDiv(t-s->offset,l.grid.right-l.grid.left,s->span);MoveToEx(dc,x,l.grid.top,nullptr);LineTo(dc,x,l.grid.bottom);if(t%96==0){RECT r{x+3,0,x+70,24};text(dc,r,std::to_wstring(t/barTicks+1)+L"."+std::to_wstring(t%barTicks/96+1));}}SelectObject(dc,oldPen);DeleteObject(pen);RestoreDC(dc,-1);
        SaveDC(dc);IntersectClipRect(dc,l.grid.left,l.grid.top,l.grid.right,l.grid.bottom);
        for(size_t i=0;i<p.notes.size();++i){const auto& n=p.notes[i];auto r=noteRect(*s,l,n);r.right=std::max(r.right,r.left+3);fill(dc,r,n.locked?RGB(173,128,50):s->selected.count(i)?RGB(103,200,245):RGB(40,130,195));if(s->names&&r.right-r.left>30){r.left+=3;std::wstring label=n.pitch<128?std::wstring(keys[n.pitch%12])+std::to_wstring(n.pitch/12-1):L"Hit";text(dc,r,label);}}
        int cursor=l.grid.left+MulDiv(s->cursor-s->offset,l.grid.right-l.grid.left,s->span);fill(dc,{cursor,l.grid.top,cursor+1,l.grid.bottom},RGB(215,215,215));
        if(isPlaying.load()){int x=l.grid.left+MulDiv(sequencerCurrentStep.load()*24-s->offset,l.grid.right-l.grid.left,s->span);fill(dc,{x,l.grid.top,x+2,l.grid.bottom},RGB(240,175,75));}RestoreDC(dc,-1);
        if(s->velocity){fill(dc,l.lane,RGB(30,33,39));SaveDC(dc);IntersectClipRect(dc,l.lane.left,l.lane.top,l.lane.right,l.lane.bottom);for(const auto& n:p.notes){int x=l.lane.left+MulDiv(n.start-s->offset,l.lane.right-l.lane.left,s->span);int h=static_cast<int>(n.velocity*(l.lane.bottom-l.lane.top));fill(dc,{x,l.lane.bottom-h,x+4,l.lane.bottom},RGB(70,160,210));}RestoreDC(dc,-1);text(dc,{8,l.lane.top,85,l.lane.bottom},L"Velocity");}
        std::wstring status=L"Ctrl+click: select | Drag: move | Right edge: resize | Shift+wheel: scroll | PgUp/PgDn: bars | "+std::to_wstring(p.notes.size())+L" notes";
        if(ghostX<l.grid.right && doc.pattern.length<maxTicks)status=L"Ghost bar: add a note to extend | Shift+wheel: scroll | PgUp/PgDn: bars";
        if(doc.settings.continuous)status=std::wstring(algorithms[static_cast<int>(doc.settings.algorithm)])+L" — continuous, every "+std::to_wstring(doc.settings.every)+L" loop(s) | Freeze to keep";
        text(dc,l.status,status);BitBlt(target,0,0,c.right,c.bottom,dc,0,0,SRCCOPY);SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);EndPaint(hwnd,&ps);return 0;}
    return DefWindowProcW(hwnd,msg,wp,lp);
}
