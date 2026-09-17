#include "gui/editing_panel.h"
#include "gui/vst3_menu.h"
#include "hosting/TrackVST3.h"
#include "hosting/VST3Host.h"
#include "core/sequencer.h"
#include "core/audio_engine.h"
#include "wdl/lice/lice.h"
#include <windows.h>
#include <objbase.h>
#include <iostream>
#include <stdexcept>
void check(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
int main(int argc,char** argv){
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);HWND window=nullptr;int result=0;
    try{
        check(argc==2,"Expected fixture path");
        initTracks();int track=getTracks().front().id;setActiveSequencerTrackId(track);
        window=CreateWindowW(L"STATIC",L"Panel test",WS_POPUP,0,0,1000,900,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        check(window!=nullptr,"Cannot create hidden test window");createEditingPanel(window);
        HDC dc=GetDC(window);int dpi=GetDeviceCaps(dc,LOGPIXELSX);ReleaseDC(window,dc);
        auto px=[&](int n){return MulDiv(n,dpi,96);};
        LICE_SysBitmap bitmap(1000,900);drawEditingPanel(bitmap);
        const int editorTop=sequencerContentRect(window).bottom;
        check(editorTop==304,"Editor is not directly below the sequencer");
        handleEditingPanelMessage(window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(800,editorTop+2));
        handleEditingPanelMessage(window,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(800,editorTop+200));
        handleEditingPanelMessage(window,WM_LBUTTONUP,0,MAKELPARAM(800,editorTop+200));
        check(GetCapture()!=window&&sequencerContentRect(window).bottom==editorTop,"Removed divider still drags");
        SetWindowPos(window,nullptr,0,0,1000,1200,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
        layoutEditingPanel(window);
        check(sequencerContentRect(window).bottom==editorTop,"Tall window creates an empty gap above editor");
        SetWindowPos(window,nullptr,0,0,1000,900,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
        layoutEditingPanel(window);drawEditingPanel(bitmap);
        isPlaying.store(true);
        int addX=1000-px(20+68),addY=editorTop+px(36+32+6);
        handleEditingPanelMessage(window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(addX,addY+px(14)));
        handleEditingPanelMessage(window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(addX,addY+px(28+12)));
        drawEditingPanel(bitmap);
        check(isPlaying.load()&&trackGetEqEnabled(track),"Adding a native effect stopped transport or failed");
        revealEditingPanel(EditingPage::Instrument,track);drawEditingPanel(bitmap);
        check(!isEditingPanelVisible(EditingPage::Instrument),"Instrument tab shown for synth track");
        trackSetType(track,TrackType::Vst3);drawEditingPanel(bitmap);
        revealEditingPanel(EditingPage::Instrument,track);drawEditingPanel(bitmap);
        check(isEditingPanelVisible(EditingPage::Instrument)&&!isEditingPanelVisible(EditingPage::Fx),"Instrument page did not open");
        auto classes=kj::VST3Host::scan(argv[1]);
        auto first=kj::addTrackVst3(track,false,argv[1],classes[0].ID().toString(),44100);
        auto second=kj::addTrackVst3(track,false,argv[1],classes[1].ID().toString(),44100);
        trackSetFxOrder(track,{first,second});
        revealEditingPanel(EditingPage::Fx,track);drawEditingPanel(bitmap);
        check(isEditingPanelVisible(EditingPage::Fx),"FX page did not open");
        // Exercise the painted first slot's Down and Bypass controls through
        // the real panel hit-testing and GUI mutation path.
        auto clickSlot=[&](int column,int count=6){
            int top=sequencerContentRect(window).bottom,width=1000-px(28),w=(width-px(6)*(count-1))/count;
            int x=px(8)+column*(w+px(6))+w/2,y=top+px(36+32+6+36+32+14);
            isPlaying.store(true);
            handleEditingPanelMessage(window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(x,y));drawEditingPanel(bitmap);
            check(isPlaying.load(),"Rack edit stopped transport");
        };
        clickSlot(5);check(kj::getTrackVst3Slots(track)[0].id==second,"Embedded Down changed the wrong slot");
        clickSlot(1);check(kj::getTrackVst3Slots(track)[0].bypass,"Embedded bypass did not update the slot");
        clickSlot(3);check(kj::getTrackVst3Slots(track).size()==1&&kj::getTrackVst3Slots(track)[0].id==first,"Embedded remove changed the wrong slot");
        trackSetFxOrder(track,{"kj:eq",first,"kj:delay"});trackSetEqEnabled(track,true);drawEditingPanel(bitmap);
        clickSlot(4,5);check(trackGetFxOrder(track)==std::vector<std::string>({first,"kj:eq","kj:delay"}),"Native Down did not cross plugin");
        clickSlot(5);check(trackGetFxOrder(track)[0]=="kj:eq","Plugin Down did not cross native effect");
        clickSlot(1,5);check(!trackGetEqEnabled(track),"Native bypass failed");
        clickSlot(2,5);check(trackGetFxOrder(track)==std::vector<std::string>({first,"kj:delay"}),"Native remove failed");
        revealEditingPanel(EditingPage::Instrument,track);trackSetType(track,TrackType::Sample);drawEditingPanel(bitmap);
        check(!isEditingPanelVisible(EditingPage::Instrument)&&isEditingPanelVisible(EditingPage::Fx),"Type change did not return to FX");
        std::cout<<"PASS conditional instrument page, FX rack hit-testing, reorder, bypass, remove and type-change fallback\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';result=1;}
    if(window){handleEditingPanelMessage(window,WM_DESTROY,0,0);DestroyWindow(window);}
    isPlaying.store(false);kj::clearTrackVst3();CoUninitialize();return result;
}
