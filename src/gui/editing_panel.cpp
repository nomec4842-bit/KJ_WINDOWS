#include "gui/mixer.h"
#include "core/audio_input.h"
#include "core/track_type_sample.h"
#include "gui/editing_panel.h"
#include "gui/piano_editor.h"
#include "gui/eq_graph.h"
#include "core/parametric_eq.h"
#include "gui/editing_panel_model.h"
#ifdef KJ_ENABLE_VST3
#include "gui/vst3_menu.h"
#include "hosting/TrackVST3.h"
#endif
#include "gui/lfo_window.h"
#include "core/tracks.h"
#include "core/audio_engine.h"
#include "core/audio_recording.h"
#include "core/track_type_synth.h"
#include "core/sequencer.h"
#include "core/mod_matrix.h"
#include "core/mod_matrix_parameters.h"
#include "wdl/lice/lice.h"
#include <windowsx.h>
#include <algorithm>
#include <cmath>
#include <functional>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace {
HWND owner=nullptr;
EqGraph eqGraph;
bool eqGraphVisible=false;
int dpi=96, top=0, active=0, focused=-1, divider=0;
double splitFraction=.5;
bool split=false, dockFocus=false;
bool lfoView=false;
bool instrumentView=false;
int selectedLfo=0;
HMENU lfoCountMenu=nullptr, pianoPreferencesMenu=nullptr;
std::wstring preferencePath;
struct Pane { editing::Binding binding; int effect=0, selected=0, scroll=0, contentHeight=0, editorTop=0; RECT rect{},body{}; } panes[2];
struct Hit {
    RECT rect{}; std::string label; std::function<void()> action;
    std::function<void(float)> set; float value=0, lo=0, hi=1; bool logarithmic=false;
};
std::vector<Hit> hits;
Hit moving, numeric;
bool sliding=false, typing=false;
std::string entry;
struct Choice { std::string text; std::function<void()> action; };
std::vector<Choice> choices;
RECT chooser{}; int choiceScroll=0,choiceSelected=0;
const char* sources[]={"LFO 1","LFO 2","LFO 3","Envelope 1","Macro 1","Macro 2"};
const char* effects[]={"EQ","Delay","Compressor","Sidechain"};
const char* nativeKeys[]={"kj:eq","kj:delay","kj:compressor","kj:sidechain"};
void redraw();
bool nativeEnabled(int track,int effect){return effect==0?trackGetEqEnabled(track):effect==1?trackGetDelayEnabled(track):effect==2?trackGetCompressorEnabled(track):trackGetSidechainEnabled(track);}
void setNativeEnabled(int track,int effect,bool enabled){switch(effect){case 0:trackSetEqEnabled(track,enabled);break;case 1:trackSetDelayEnabled(track,enabled);break;case 2:trackSetCompressorEnabled(track,enabled);break;case 3:trackSetSidechainEnabled(track,enabled);break;}}
void nativeAction(int track,int effect,int action){
    auto order=trackGetFxOrder(track);
    auto it=std::find(order.begin(),order.end(),nativeKeys[effect]);
    if(action==0){if(it==order.end())order.push_back(nativeKeys[effect]);setNativeEnabled(track,effect,true);}
    else if(action==1)setNativeEnabled(track,effect,!nativeEnabled(track,effect));
    else if(action==2){if(it!=order.end())order.erase(it);setNativeEnabled(track,effect,false);}
    else if(it!=order.end()){
        int index=static_cast<int>(it-order.begin()),next=index+(action==3?-1:1);
        if(next>=0&&next<static_cast<int>(order.size()))std::swap(order[index],order[next]);
    }
    trackSetFxOrder(track,std::move(order));redraw();
}
int px(int n){return MulDiv(n,dpi,96);}
RECT rect(int x,int y,int w,int h){return {x,y,x+w,y+h};}
bool contains(RECT r,POINT p){return PtInRect(&r,p)!=FALSE;}
void redraw(){if(owner)InvalidateRect(owner,nullptr,FALSE);}
std::string narrow(const wchar_t* s){if(!s)return {};int n=WideCharToMultiByte(CP_UTF8,0,s,-1,nullptr,0,nullptr,nullptr);std::string out(n,0);WideCharToMultiByte(CP_UTF8,0,s,-1,out.data(),n,nullptr,nullptr);out.pop_back();return out;}
std::wstring wide(const std::string& s){int n=MultiByteToWideChar(CP_UTF8,0,s.data(),static_cast<int>(s.size()),nullptr,0);std::wstring out(n,0);MultiByteToWideChar(CP_UTF8,0,s.data(),static_cast<int>(s.size()),out.data(),n);return out;}
void fill(HDC dc,RECT r,COLORREF color){HBRUSH b=CreateSolidBrush(color);FillRect(dc,&r,b);DeleteObject(b);}
void text(HDC dc,RECT r,const std::string& s,bool center=false){auto w=wide(s);SetTextColor(dc,RGB(230,230,230));DrawTextW(dc,w.c_str(),static_cast<int>(w.size()),&r,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|(center?DT_CENTER:DT_LEFT));}
std::string number(float n){std::ostringstream s;s<<std::fixed<<std::setprecision(3)<<n;return s.str();}
void commitNumber(bool commit){
    if(typing && commit){try{size_t used=0;float v=std::stof(entry,&used);if(used!=entry.size()||!std::isfinite(v))throw 0;numeric.set(std::clamp(v,numeric.lo,numeric.hi));}catch(...){MessageBeep(MB_ICONWARNING);}}
    typing=false;entry.clear();
}
void cancelGesture(){eqGraph.dragging=false;sliding=false;divider=0;if(owner&&GetCapture()==owner)ReleaseCapture();}
bool instrumentAvailable(){
#ifdef KJ_ENABLE_VST3
    return panes[0].binding.track>0&&trackGetType(panes[0].binding.track)==TrackType::Vst3;
#else
    return false;
#endif
}
void sync(){
    auto tracks=getTracks();int selected=getActiveSequencerTrackId();
    if(std::none_of(tracks.begin(),tracks.end(),[&](const Track& t){return t.id==selected;}))selected=tracks.empty()?0:tracks.front().id;
    for(auto& p:panes){int old=p.binding.track;bool exists=std::any_of(tracks.begin(),tracks.end(),[&](const Track& t){return t.id==old;});
        int next=(!p.binding.pinned||!exists)?selected:old;
        if(next!=old){commitNumber(exists);cancelGesture();choices.clear();p.selected=0;p.scroll=0;focused=-1;}
        p.binding.follow(selected,exists);
    }
    if(instrumentView&&!instrumentAvailable()){instrumentView=false;panes[0].scroll=0;choices.clear();focused=-1;}
}
bool both(){RECT r{};GetClientRect(owner,&r);return editing::splitFits(split,r.right,dpi);}
void layout(){if(!owner)return;RECT r{};GetClientRect(owner,&r);int h=editing::panelHeight(r.bottom);top=r.bottom-h;
    bool two=both();int left=two?std::clamp(static_cast<int>(r.right*splitFraction),px(420),static_cast<int>(r.right)-px(426)):r.right;
    for(int i=0;i<2;++i){auto& p=panes[i];p.rect=rect(two&&i?left+px(6):0,top+px(36),two?(i?r.right-left-px(6):left):r.right,h-px(36));p.body=p.rect;p.body.top+=px(32);}
}
void registerHit(Hit h,RECT clip){RECT visible{};if(IntersectRect(&visible,&h.rect,&clip)&&visible.bottom-visible.top>=px(6)){h.rect=visible;hits.push_back(std::move(h));}}
void button(HDC dc,RECT r,const std::string& label,std::function<void()> action,bool selected,RECT clip){
    fill(dc,r,selected?RGB(0,100,165):RGB(48,48,48));FrameRect(dc,&r,static_cast<HBRUSH>(GetStockObject(GRAY_BRUSH)));text(dc,r,label,true);registerHit({r,label,std::move(action)},clip);
}
void slider(HDC dc,RECT r,const std::string& label,float value,float lo,float hi,std::function<void(float)> setter,bool logarithmic,RECT clip){
    fill(dc,r,RGB(34,34,34));RECT title=r;title.bottom=title.top+px(22);title.left+=px(6);text(dc,title,label+": "+number(value));
    RECT bar=r;bar.left+=px(6);bar.right-=px(6);bar.top+=px(27);bar.bottom-=px(5);fill(dc,bar,RGB(65,65,65));
    float t=logarithmic?std::log(std::clamp(value,lo,hi)/lo)/std::log(hi/lo):(value-lo)/(hi-lo);RECT amount=bar;amount.right=amount.left+static_cast<int>((bar.right-bar.left)*std::clamp(t,0.f,1.f));fill(dc,amount,RGB(0,125,190));
    registerHit({r,label,{},std::move(setter),value,lo,hi,logarithmic},clip);
}
void openChoices(RECT anchor,std::vector<Choice> options){
    commitNumber(true);choices=std::move(options);choiceScroll=0;choiceSelected=0;RECT r{};GetClientRect(owner,&r);
    int width=std::min<int>(px(340),r.right),height=std::min<int>(px(24)*static_cast<int>(choices.size()),std::max(px(24),static_cast<int>(r.bottom)-px(12)));
    int x=std::clamp<int>(anchor.left,0,std::max(0,static_cast<int>(r.right)-width));int y=std::clamp<int>(anchor.bottom,px(6),std::max(px(6),static_cast<int>(r.bottom)-height));chooser=rect(x,y,width,height);redraw();
}
#ifdef KJ_ENABLE_VST3
int pluginRack(HDC dc,Pane& p,int x,int y,int width,bool instrument){
    const int track=p.binding.track;RECT clip=p.body;
    auto slots=kj::getTrackVst3Slots(track);
    slots.erase(std::remove_if(slots.begin(),slots.end(),[instrument](const auto& s){return s.instrument!=instrument;}),slots.end());
    const int addWidth=std::min(px(136),width/2);
    text(dc,rect(x,y,width-addWidth-px(6),px(28)),instrument?"VST3 instrument":"VST3 effects rack");
    button(dc,rect(x+width-addWidth,y,addWidth,px(28)),instrument?(slots.empty()?"+ Instrument":"Replace..."):"+ Effect",
        [track,instrument]{handleVst3Menu(owner,instrument?kMenuVst3Instrument:kMenuVst3Effect,track);},false,clip);y+=px(36);
    if(slots.empty()){
        text(dc,rect(x,y,width,px(26)),instrument?"Load an instrument to play this track's notes.":"No plugin effects. Add an effect to start a chain.");y+=px(32);
    }
    for(size_t i=0;i<slots.size();++i){
        const auto slot=slots[i];
        fill(dc,rect(x,y,width,px(68)),RGB(32,32,32));
        std::string label=instrument?slot.name:std::to_string(i+1)+". "+slot.name;
        if(slot.bypass)label+=" [Bypassed]";
        if(!slot.error.empty())label+=" [Unavailable]";
        text(dc,rect(x+px(6),y,width-px(12),px(28)),label);y+=px(32);
        auto control=[&](int column,int count,const std::string& label,Vst3SlotAction action,bool selected=false){
            int w=(width-px(6)*(count-1))/count;
            button(dc,rect(x+column*(w+px(6)),y,w,px(28)),label,[track,id=slot.id,action]{handleVst3SlotAction(owner,track,id,action);},selected,clip);
        };
        int count=instrument?4:6;
        control(0,count,"Editor",Vst3SlotAction::Editor);
        control(1,count,slot.bypass?"Enable":"Bypass",Vst3SlotAction::Bypass,slot.bypass);
        control(2,count,"Locate",Vst3SlotAction::Locate);
        control(3,count,"Remove",Vst3SlotAction::Remove);
        if(!instrument){control(4,count,"Up",Vst3SlotAction::Up);control(5,count,"Down",Vst3SlotAction::Down);}
        y+=px(42);
        if(!slot.error.empty()){
            RECT r=rect(x,y,width,px(25));
            button(dc,r,"Unavailable: "+slot.error,[error=slot.error]{MessageBoxA(owner,error.c_str(),"VST3 plugin unavailable",MB_OK|MB_ICONINFORMATION);},false,clip);y+=px(33);
        }
    }
    text(dc,rect(x,y,width,px(24)),"Instrument > effects rack > track output");
    return y+px(32);
}
void instrument(HDC dc,Pane& p){
    int x=p.body.left+px(8),width=p.body.right-p.body.left-px(28),y=p.body.top+px(6)-p.scroll;
    p.contentHeight=pluginRack(dc,p,x,y,width,true)-y+px(12);
}
#endif
int mixedRack(HDC dc,Pane& p,int x,int y,int width){
    const int track=p.binding.track;RECT clip=p.body;auto order=trackGetFxOrder(track);
#ifdef KJ_ENABLE_VST3
    auto plugins=kj::getTrackVst3Slots(track);
#endif
    int addWidth=std::min(px(136),width/2);
    text(dc,rect(x,y,width-addWidth-px(6),px(28)),"Effects rack");
    RECT add=rect(x+width-addWidth,y,addWidth,px(28));
    button(dc,add,"+ Effect",[track,add]{
        std::vector<Choice> opts;auto order=trackGetFxOrder(track);
        for(int i=0;i<4;++i)if(std::find(order.begin(),order.end(),nativeKeys[i])==order.end())opts.push_back({std::string("KJ ")+effects[i],[track,i]{nativeAction(track,i,0);panes[0].effect=i;}});
#ifdef KJ_ENABLE_VST3
        opts.push_back({"VST3 plugin...",[track]{handleVst3Menu(owner,kMenuVst3Effect,track);}});
#endif
        if(opts.empty())opts.push_back({"All native effects are in the rack",[]{}});
        openChoices(add,std::move(opts));
    },false,clip);y+=px(36);
    int position=0;
    for(const auto& key:order){
        int native=-1;for(int i=0;i<4;++i)if(key==nativeKeys[i])native=i;
        std::string name;bool bypass=false,missing=false;
        if(native>=0){name=std::string("KJ ")+effects[native];bypass=!nativeEnabled(track,native);}
        else {
#ifdef KJ_ENABLE_VST3
            auto it=std::find_if(plugins.begin(),plugins.end(),[&](const auto& s){return !s.instrument&&s.id==key;});
            if(it==plugins.end())continue;name=it->name;bypass=it->bypass;missing=!it->error.empty();
#else
            continue;
#endif
        }
        fill(dc,rect(x,y,width,px(68)),RGB(32,32,32));
        std::string label=std::to_string(++position)+". "+name+(bypass?" [Bypassed]":"")+(missing?" [Unavailable]":"");
        text(dc,rect(x+px(6),y,width-px(12),px(28)),label);y+=px(32);
        int count=native>=0?5:6,w=(width-px(6)*(count-1))/count;
        auto control=[&](int col,const std::string& label,std::function<void()> action,bool selected=false){button(dc,rect(x+col*(w+px(6)),y,w,px(28)),label,std::move(action),selected,clip);};
        if(native>=0){
            control(0,"Edit",[native]{panes[0].effect=native;panes[0].scroll=panes[0].editorTop;});
            control(1,bypass?"Enable":"Bypass",[track,native]{nativeAction(track,native,1);},bypass);
            control(2,"Remove",[track,native]{nativeAction(track,native,2);});
            control(3,"Up",[track,native]{nativeAction(track,native,3);});
            control(4,"Down",[track,native]{nativeAction(track,native,4);});
        }else{
#ifdef KJ_ENABLE_VST3
            const Vst3SlotAction actions[]={Vst3SlotAction::Editor,Vst3SlotAction::Bypass,Vst3SlotAction::Locate,Vst3SlotAction::Remove,Vst3SlotAction::Up,Vst3SlotAction::Down};
            const char* labels[]={"Editor",bypass?"Enable":"Bypass","Locate","Remove","Up","Down"};
            for(int i=0;i<6;++i)control(i,labels[i],[track,key,action=actions[i]]{handleVst3SlotAction(owner,track,key,action);},i==1&&bypass);
#endif
        }
        y+=px(42);
    }
    text(dc,rect(x,y,width,px(24)),position?"Signal flows top to bottom through native and plugin effects.":"No effects. Add a KJ effect or VST3 plugin.");
    return y+px(32);
}
void fx(HDC dc,Pane& p){
    const int track=p.binding.track;int x=p.body.left+px(8),width=p.body.right-p.body.left-px(28),y=p.body.top+px(6)-p.scroll;
    RECT clip=p.body;int start=y,gap=px(6),cell=(width-gap)/2;
    if(trackGetType(track)==TrackType::Sample && trackGetSampleDrumMode(track)) {
        const int lane=trackGetSelectedDrum(track);auto bank=sampleGetBankBuffers();
        RECT select=rect(x,y,width,px(28));
        button(dc,select,"Drum: Sample "+std::to_string(lane+1),[track,select]{
            std::vector<Choice> opts;auto bank=sampleGetBankBuffers();
            for(size_t i=0;bank&&i<bank->size();++i)opts.push_back({"Sample "+std::to_string(i+1),[track,i]{trackSetSelectedDrum(track,static_cast<int>(i));}});
            openChoices(select,std::move(opts));
        },true,clip);y+=px(34);
        if(bank && lane<static_cast<int>(bank->size())) {
            auto drum=trackGetDrumSettings(track,lane);
            auto control=[&](const char* label,float value,float lo,float hi,DrumParameter parameter){
                slider(dc,rect(x,y,width,px(48)),label,value,lo,hi,[track,lane,parameter](float v){trackSetDrumParameter(track,lane,parameter,v);},false,clip);y+=px(54);
            };
            control("Drum attack (s)",drum.attack,0,4,DrumParameter::Attack);
            control("Drum release (s)",drum.release,0,4,DrumParameter::Release);
            control("Drum pitch (semitones)",drum.pitch,-48,48,DrumParameter::Pitch);
            control("Drum pan",drum.pan,-1,1,DrumParameter::Pan);
            control("Drum volume",drum.volume,0,1,DrumParameter::Volume);
        }
    }
    if(trackGetType(track)==TrackType::AudioIn){
        for(const auto& t:getTracks())if(t.id==track){
            button(dc,rect(x,y,width,px(32)),"Input: "+(t.inputDeviceName.empty()?std::string("Select device / channels"):t.inputDeviceName),[track]{showAudioInputSettings(owner,track);},false,clip);y+=px(38);
            button(dc,rect(x,y,width,px(32)),t.inputMonitor?"Monitor: On":"Monitor: Off",[track,t]{trackSetInputMonitor(track,!t.inputMonitor);},t.inputMonitor,clip);y+=px(38);
            text(dc,rect(x,y,width,px(28)),audioInputDescription(t));y+=px(34);
        }
    }
    y=mixedRack(dc,p,x,y,width);
    text(dc,rect(x,y,width,px(24)),"Track output");y+=px(30);
    slider(dc,rect(x,y,cell,px(48)),"Volume",trackGetVolume(track),0,1,[track](float v){trackSetVolume(track,v);},false,clip);
    slider(dc,rect(x+cell+gap,y,cell,px(48)),"Pan",trackGetPan(track),-1,1,[track](float v){trackSetPan(track,v);},false,clip);y+=px(56);
    auto order=trackGetFxOrder(track);
    if(p.effect<0||std::find(order.begin(),order.end(),nativeKeys[p.effect])==order.end()){
        p.effect=-1;for(int i=0;i<4;++i)if(std::find(order.begin(),order.end(),nativeKeys[i])!=order.end()){p.effect=i;break;}
    }
    if(p.effect<0){p.contentHeight=y-start+px(12);return;}
    p.editorTop=y-start;
    button(dc,rect(x,y,width,px(26)),std::string("< Rack   |   KJ ")+effects[p.effect],[]{panes[0].scroll=0;},false,clip);y+=px(34);
    bool enabled=p.effect==0?trackGetEqEnabled(track):p.effect==1?trackGetDelayEnabled(track):p.effect==2?trackGetCompressorEnabled(track):trackGetSidechainEnabled(track);int effect=p.effect;
    button(dc,rect(x,y,width,px(25)),std::string(enabled?"[x] ":"[ ] ")+effects[effect]+" enabled",[track,effect,enabled]{switch(effect){case 0:trackSetEqEnabled(track,!enabled);break;case 1:trackSetDelayEnabled(track,!enabled);break;case 2:trackSetCompressorEnabled(track,!enabled);break;case 3:trackSetSidechainEnabled(track,!enabled);break;}},enabled,clip);y+=px(33);
    auto row=[&](const std::string& label,float v,float lo,float hi,std::function<void(float)> set,bool log=false){slider(dc,rect(x,y,width,px(48)),label,v,lo,hi,std::move(set),log,clip);y+=px(54);};
    if(effect==0){
        if(eqGraph.track!=track){eqGraph.dragging=false;eqGraph.track=track;}
        eqGraph.bounds=rect(x,y,width,px(260));eqGraph.clip=clip;eqGraphVisible=true;drawEqGraph(dc,eqGraph);y+=px(266);
        text(dc,rect(x,y,width,px(24)),"Drag: frequency / gain   |   Wheel: Q   |   Right-click: filter type");y+=px(30);
        row("Track output (lower for boost headroom)",trackGetVolume(track),0,1,[track](float v){trackSetVolume(track,v);});
        for(int band=0;band<3;++band){
        RECT shapeRect=rect(x,y,width,px(25));
        button(dc,shapeRect,"Band "+std::to_string(band+1)+" / "+eq::names[trackGetEqShape(track,band)],[track,band,shapeRect]{eqGraph.selected=band;std::vector<Choice> opts;for(int i=0;i<7;++i)opts.push_back({eq::names[i],[track,band,i]{trackSetEqShape(track,band,i);}});openChoices(shapeRect,std::move(opts));},band==eqGraph.selected,clip);y+=px(31);
int cw=(width-px(12))/3;std::string prefix="B"+std::to_string(band+1)+" ";float gain=band==0?trackGetEqLowGain(track):band==1?trackGetEqMidGain(track):trackGetEqHighGain(track);
        if(eq::hasGain(static_cast<eq::Shape>(trackGetEqShape(track,band))))slider(dc,rect(x,y,cw,px(48)),prefix+"dB",gain,-12,12,[track,band](float v){if(band==0)trackSetEqLowGain(track,v);else if(band==1)trackSetEqMidGain(track,v);else trackSetEqHighGain(track,v);},false,clip);
        else text(dc,rect(x,y,cw,px(48)),prefix+"Gain not used");
        slider(dc,rect(x+cw+gap,y,cw,px(48)),prefix+"Hz",trackGetEqFrequency(track,band),20,20000,[track,band](float v){trackSetEqFrequency(track,band,v);},true,clip);
        slider(dc,rect(x+2*(cw+gap),y,cw,px(48)),prefix+"Q",trackGetEqQ(track,band),.1f,10,[track,band](float v){trackSetEqQ(track,band,v);},true,clip);y+=px(54);}}
    else if(effect==1){row("Time (ms)",trackGetDelayTimeMs(track),1,2000,[track](float v){trackSetDelayTimeMs(track,v);},true);row("Feedback",trackGetDelayFeedback(track),0,.95f,[track](float v){trackSetDelayFeedback(track,v);});row("Mix",trackGetDelayMix(track),0,1,[track](float v){trackSetDelayMix(track,v);});}
    else if(effect==2){row("Threshold (dB)",trackGetCompressorThresholdDb(track),-60,0,[track](float v){trackSetCompressorThresholdDb(track,v);});row("Ratio",trackGetCompressorRatio(track),1,20,[track](float v){trackSetCompressorRatio(track,v);});row("Attack (s)",trackGetCompressorAttack(track),.001f,1,[track](float v){trackSetCompressorAttack(track,v);},true);row("Release (s)",trackGetCompressorRelease(track),.01f,4,[track](float v){trackSetCompressorRelease(track,v);},true);}
    else {int source=trackGetSidechainSourceTrack(track);std::string label="Input: None";for(auto& t:getTracks())if(t.id==source)label="Input: "+t.name;
        RECT r=rect(x,y,width,px(26));button(dc,r,label,[track,r]{std::vector<Choice> options{{"None",[track]{trackSetSidechainSourceTrack(track,0);}}};for(auto& t:getTracks())if(t.id!=track){int id=t.id;options.push_back({t.name,[track,id]{trackSetSidechainSourceTrack(track,id);}});}openChoices(r,std::move(options));},false,clip);y+=px(34);
        row("Amount",trackGetSidechainAmount(track),0,1,[track](float v){trackSetSidechainAmount(track,v);});row("Attack (s)",trackGetSidechainAttack(track),0,4,[track](float v){trackSetSidechainAttack(track,v);});row("Release (s)",trackGetSidechainRelease(track),0,4,[track](float v){trackSetSidechainRelease(track,v);});}
    p.contentHeight=y-start+px(12);
}
void drawLfos(HDC dc,Pane& p){
    selectedLfo=std::clamp(selectedLfo,0,lfoCount()-1);
    int track=p.binding.track,index=selectedLfo,x=p.body.left+px(8),width=p.body.right-p.body.left-px(28),y=p.body.top+px(6)-p.scroll,start=y;RECT clip=p.body;
    button(dc,rect(x,y,px(100),px(26)),"< Routes",[]{lfoView=false;panes[1].scroll=0;},false,clip);
    RECT select=rect(x+px(108),y,width-px(108),px(26));
    button(dc,select,"LFO "+std::to_string(index+1)+" / "+std::to_string(lfoCount()),[select]{std::vector<Choice> opts;for(int i=0;i<lfoCount();++i)opts.push_back({"LFO "+std::to_string(i+1),[i]{selectedLfo=i;}});openChoices(select,std::move(opts));},true,clip);y+=px(34);
    slider(dc,rect(x,y,width,px(48)),"Rate (Hz)",trackGetLfoRate(track,index),.05f,20,[track,index](float v){trackSetLfoRate(track,index,v);},true,clip);y+=px(56);
    auto shape=trackGetLfoShape(track,index);const char* names[]={"Sine","Triangle","Saw","Square"};RECT sr=rect(x,y,width,px(26));
    button(dc,sr,std::string("Shape: ")+names[std::clamp(static_cast<int>(shape),0,3)],[track,index,sr]{const char* names[]={"Sine","Triangle","Saw","Square"};std::vector<Choice> opts;for(int i=0;i<4;++i)opts.push_back({names[i],[track,index,i]{trackSetLfoShape(track,index,static_cast<LfoShape>(i));}});openChoices(sr,std::move(opts));},false,clip);y+=px(34);
    slider(dc,rect(x,y,width,px(48)),"Deform",trackGetLfoDeform(track,index),0,1,[track,index](float v){trackSetLfoDeform(track,index,v);},false,clip);y+=px(56);
    text(dc,rect(x,y,width,px(24)),"LFO count: Preferences > Settings > Mod Matrix");y+=px(30);p.contentHeight=y-start+px(12);
}
std::string modTargetLabel(const ModMatrixAssignment& a) {
    if (!a.vstSlotId.empty()) {
#ifdef KJ_ENABLE_VST3
        for (const auto& target : kj::getTrackVst3ModTargets(a.trackId))
            if (target.slotId == a.vstSlotId && target.parameterId == a.vstParameterId) return target.label;
#endif
        return a.vstParameterName + " (unavailable)";
    }
    auto info = modMatrixGetParameterInfo(a.parameterIndex);
    return info ? narrow(info->label) : "Unavailable";
}
void mod(HDC dc,Pane& p){
    if(lfoView){drawLfos(dc,p);return;}
    int track=p.binding.track,x=p.body.left+px(8),width=p.body.right-p.body.left-px(28),y=p.body.top+px(6)-p.scroll,start=y;RECT clip=p.body;
    auto all=modMatrixGetAssignments();std::vector<ModMatrixAssignment> routes;for(auto& a:all)if(a.trackId==track)routes.push_back(a);
    if(std::none_of(routes.begin(),routes.end(),[&](const ModMatrixAssignment& a){return a.id==p.selected;}))p.selected=routes.empty()?0:routes.front().id;
    int selected=p.selected,bw=(width-px(12))/3;
    button(dc,rect(x,y,bw,px(26)),"+ Route",[track]{auto a=modMatrixCreateAssignment();a.trackId=track;a.sourceIndex=0;a.parameterIndex=0;a.normalizedAmount=0;modMatrixUpdateAssignment(a);panes[1].selected=a.id;},false,clip);
    button(dc,rect(x+bw+px(6),y,bw,px(26)),"Remove",[selected]{if(selected)modMatrixRemoveAssignment(selected);},false,clip);
    button(dc,rect(x+2*(bw+px(6)),y,bw,px(26)),"LFO editor",[]{lfoView=true;panes[1].scroll=0;},false,clip);y+=px(34);
    auto assignment=modMatrixGetAssignment(selected);
    if(assignment){auto a=*assignment;auto info=modMatrixGetParameterInfo(a.parameterIndex);
        RECT sr=rect(x,y,width,px(26));button(dc,sr,"Source: "+modulationSourceLabel(a.sourceIndex),[a,sr]{std::vector<Choice> opts;std::vector<int> ids;for(int i=0;i<lfoCount();++i)ids.push_back(lfoSourceId(i));for(int i=3;i<=5;++i)ids.push_back(i);for(int source:ids)opts.push_back({modulationSourceLabel(source),[assignmentId=a.id,source]{auto live=modMatrixGetAssignment(assignmentId);if(live){live->sourceIndex=source;modMatrixUpdateAssignment(*live);}}});openChoices(sr,std::move(opts));},false,clip);y+=px(32);
        RECT tr=rect(x,y,width,px(26));button(dc,tr,"Target: "+modTargetLabel(a),[track,a,tr]{auto tracks=getTracks();auto it=std::find_if(tracks.begin(),tracks.end(),[track](const Track& t){return t.id==track;});std::vector<Choice> opts;if(it!=tracks.end())for(int i=0;i<modMatrixGetParameterCount();++i)if(modMatrixParameterAvailableForTrack(i,*it)){auto info=modMatrixGetParameterInfo(i);opts.push_back({narrow(info->label),[a,i]{auto live=modMatrixGetAssignment(a.id);if(live){live->parameterIndex=i;live->vstSlotId.clear();live->vstParameterName.clear();live->normalizedAmount=0;modMatrixUpdateAssignment(*live);}}});}
#ifdef KJ_ENABLE_VST3
            for (const auto& target : kj::getTrackVst3ModTargets(track)) opts.push_back({target.label,[a,target]{
                auto live=modMatrixGetAssignment(a.id);if(live){live->parameterIndex=-1;live->vstSlotId=target.slotId;live->vstParameterId=target.parameterId;live->vstParameterName=target.label;live->normalizedAmount=0;modMatrixUpdateAssignment(*live);}
            }});
#endif
            openChoices(tr,std::move(opts));},false,clip);y+=px(34);
        slider(dc,rect(x,y,width-px(68),px(48)),"Amount (%)",a.normalizedAmount*100,-100,100,[selected](float v){auto live=modMatrixGetAssignment(selected);if(live){live->normalizedAmount=modMatrixClampNormalized(v/100);modMatrixUpdateAssignment(*live);}},false,clip);
        button(dc,rect(x+width-px(62),y,px(62),px(48)),"Reset",[selected]{auto live=modMatrixGetAssignment(selected);if(live){live->normalizedAmount=0;modMatrixUpdateAssignment(*live);}},false,clip);y+=px(56);
    }else{text(dc,rect(x,y,width,px(28)),"No routes. Add a route to begin.");y+=px(36);}
#ifdef KJ_ENABLE_VST3
    text(dc,rect(x,y,width,px(22)),"Tweak a plugin knob to add it to the Target menu.");y+=px(26);
#endif
    text(dc,rect(x,y,width,px(22)),"Routes for this track");y+=px(26);
    for(auto a:routes){auto info=modMatrixGetParameterInfo(a.parameterIndex);std::string label=modulationSourceLabel(a.sourceIndex)+" > "+modTargetLabel(a)+"  "+number(a.normalizedAmount*100)+"%";
        button(dc,rect(x,y,width,px(26)),label,[a]{commitNumber(true);panes[1].selected=a.id;},a.id==selected,clip);y+=px(30);}
    p.contentHeight=y-start+px(12);
}
void slide(Hit& h,int x){float t=std::clamp(float(x-h.rect.left-px(6))/std::max(1,int(h.rect.right-h.rect.left-px(12))),0.f,1.f);float v=h.logarithmic?h.lo*std::pow(h.hi/h.lo,t):h.lo+(h.hi-h.lo)*t;h.set(v);h.value=v;}
void activate(Hit h){commitNumber(true);if(h.action)h.action();redraw();}
}

void createEditingPanel(HWND parent){owner=parent;HDC dc=GetDC(parent);dpi=GetDeviceCaps(dc,LOGPIXELSX);ReleaseDC(parent,dc);sync();layout();}
bool editingPanelCreated(){return owner!=nullptr;}
void revealEditingLfos(int track){revealEditingPanel(EditingPage::Mod,track);lfoView=true;panes[1].scroll=0;redraw();}
static HMENU audioInputPreferencesMenu=nullptr;
void appendLfoPreferences(HMENU menuBar){
    wchar_t folder[32768]{};DWORD size=GetEnvironmentVariableW(L"LOCALAPPDATA",folder,32768);
    if(size>0&&size<32768){std::wstring directory=std::wstring(folder)+L"\\KJ";CreateDirectoryW(directory.c_str(),nullptr);preferencePath=directory+L"\\settings.ini";setLfoCount(GetPrivateProfileIntW(L"ModMatrix",L"LfoCount",kDefaultLfoCount,preferencePath.c_str()));}
    if(!preferencePath.empty())setPianoTrimEmptyBars(GetPrivateProfileIntW(L"PianoRoll",L"TrimEmptyBars",1,preferencePath.c_str())!=0);
    if(!preferencePath.empty())input::setMonitorWhileStopped(GetPrivateProfileIntW(L"AudioInput",L"MonitorWhileStopped",1,preferencePath.c_str())!=0);
    HMENU preferences=CreatePopupMenu(),settings=CreatePopupMenu();audioInputPreferencesMenu=preferences;
    AppendMenuW(preferences,MF_STRING|(input::monitorWhileStopped()?MF_CHECKED:0),6322,L"Monitor audio inputs while stopped");lfoCountMenu=CreatePopupMenu();
    for(int count=1;count<=kMaxLfos;++count){auto label=std::to_wstring(count)+L" LFOs"+(count==3?L" (default)":L"");AppendMenuW(lfoCountMenu,MF_STRING,6300+count,label.c_str());}
    CheckMenuRadioItem(lfoCountMenu,6301,6320,6300+lfoCount(),MF_BYCOMMAND);
    pianoPreferencesMenu=CreatePopupMenu();
    AppendMenuW(pianoPreferencesMenu,MF_STRING|(pianoTrimEmptyBars()?MF_CHECKED:0),6321,L"Trim empty trailing bars");
    AppendMenuW(settings,MF_POPUP,reinterpret_cast<UINT_PTR>(pianoPreferencesMenu),L"Piano Roll");
    AppendMenuW(settings,MF_POPUP,reinterpret_cast<UINT_PTR>(lfoCountMenu),L"Mod Matrix");AppendMenuW(preferences,MF_POPUP,reinterpret_cast<UINT_PTR>(settings),L"Settings");AppendMenuW(menuBar,MF_POPUP,reinterpret_cast<UINT_PTR>(preferences),L"Preferences");
}
bool handleLfoPreferenceCommand(int command){
    if(command==6322){input::setMonitorWhileStopped(!input::monitorWhileStopped());
        CheckMenuItem(audioInputPreferencesMenu,6322,MF_BYCOMMAND|(input::monitorWhileStopped()?MF_CHECKED:MF_UNCHECKED));
        if(!preferencePath.empty())WritePrivateProfileStringW(L"AudioInput",L"MonitorWhileStopped",input::monitorWhileStopped()?L"1":L"0",preferencePath.c_str());return true;}

    if(command==6321){
        setPianoTrimEmptyBars(!pianoTrimEmptyBars());
        if(pianoPreferencesMenu)CheckMenuItem(pianoPreferencesMenu,6321,MF_BYCOMMAND|(pianoTrimEmptyBars()?MF_CHECKED:MF_UNCHECKED));
        if(!preferencePath.empty()&&!WritePrivateProfileStringW(L"PianoRoll",L"TrimEmptyBars",pianoTrimEmptyBars()?L"1":L"0",preferencePath.c_str()))
            MessageBoxW(owner,L"The piano roll preference changed for this session, but could not be saved.",L"KJ Settings",MB_OK|MB_ICONWARNING);
        return true;
    }
    if(command<6301||command>6320)return false;
    commitNumber(true);choices.clear();cancelGesture();setLfoCount(command-6300);selectedLfo=std::min(selectedLfo,lfoCount()-1);
    if(lfoCountMenu)CheckMenuRadioItem(lfoCountMenu,6301,6320,command,MF_BYCOMMAND);
    if(!preferencePath.empty()&&!WritePrivateProfileStringW(L"ModMatrix",L"LfoCount",std::to_wstring(lfoCount()).c_str(),preferencePath.c_str()))MessageBoxW(owner,L"The LFO count changed for this session, but the preference could not be saved.",L"KJ Settings",MB_OK|MB_ICONWARNING);
    redraw();return true;
}
void layoutEditingPanel(HWND){layout();redraw();}
RECT sequencerContentRect(HWND parent){RECT r{};GetClientRect(parent,&r);if(owner)r.bottom=top;return r;}
bool isEditingPanelVisible(EditingPage page){return owner&&(both()||active==(page==EditingPage::Mod?1:0))&&(page==EditingPage::Mod||(page==EditingPage::Instrument?instrumentView&&instrumentAvailable():!instrumentView));}
void revealEditingPanel(EditingPage page,int track){if(!owner)return;commitNumber(true);cancelGesture();choices.clear();active=page==EditingPage::Mod?1:0;auto& p=panes[active];if(track>0){p.binding.pinned=p.binding.pinned||track!=getActiveSequencerTrackId();p.binding.track=track;}sync();if(page!=EditingPage::Mod){instrumentView=page==EditingPage::Instrument&&instrumentAvailable();p.scroll=0;}layout();redraw();}
void resetEditingPanelTracks(){commitNumber(false);cancelGesture();choices.clear();for(auto& p:panes){p.binding.reset();p.selected=0;p.scroll=0;}sync();redraw();}
void focusEditingModTarget(int parameter,int track){if(parameter<0||track<=0)return;revealEditingPanel(EditingPage::Mod,track);auto all=modMatrixGetAssignments();auto it=std::find_if(all.begin(),all.end(),[&](const ModMatrixAssignment& a){return a.trackId==track&&a.vstSlotId.empty()&&a.parameterIndex==parameter;});if(it!=all.end())panes[1].selected=it->id;else{auto a=modMatrixCreateAssignment();a.trackId=track;a.parameterIndex=parameter;a.normalizedAmount=0;modMatrixUpdateAssignment(a);panes[1].selected=a.id;}panes[1].scroll=0;redraw();}
void drawEditingPanel(LICE_SysBitmap& surface){
    if(!owner)return;sync();layout();hits.clear();eqGraphVisible=false;HDC dc=surface.getDC();int saved=SaveDC(dc);SetBkMode(dc,TRANSPARENT);SelectObject(dc,GetStockObject(DEFAULT_GUI_FONT));RECT client{};GetClientRect(owner,&client);RECT dock=client;dock.top=top;fill(dc,dock,RGB(20,20,20));
    std::vector<std::string> tabs={"FX"};if(instrumentAvailable())tabs.push_back("Instrument");tabs.push_back("Mod");tabs.push_back("Split");
    int tabWidth=std::min(px(100),std::max(1,(static_cast<int>(client.right)-px(16))/static_cast<int>(tabs.size())-px(4)));
    for(size_t i=0;i<tabs.size();++i){auto tab=tabs[i];bool selected=tab=="FX"?active==0&&!instrumentView:tab=="Instrument"?active==0&&instrumentView:tab=="Mod"?active==1:tab=="Split"&&split;
        button(dc,rect(px(8)+static_cast<int>(i)*(tabWidth+px(4)),top+px(6),tabWidth,px(26)),tab,[tab]{commitNumber(true);cancelGesture();choices.clear();focused=-1;if(tab=="FX"||tab=="Instrument"){active=0;instrumentView=tab=="Instrument";panes[0].scroll=0;}else if(tab=="Mod"){active=1;}else if(tab=="Split"){split=!split;}layout();},selected,dock);
    }
    for(int i=0;i<2;++i){if(!both()&&i!=active)continue;auto& p=panes[i];int paneSave=SaveDC(dc);IntersectClipRect(dc,p.rect.left,p.rect.top,p.rect.right,p.rect.bottom);fill(dc,p.rect,RGB(24,24,24));std::string label=i?"MOD - ":instrumentView?"INSTRUMENT - ":"FX - ";for(auto& t:getTracks())if(t.id==p.binding.track)label+=t.name;if(!p.binding.track)label+="No track";
        text(dc,rect(p.rect.left+px(8),p.rect.top,p.rect.right-p.rect.left-px(86),px(30)),label);
        button(dc,rect(p.rect.right-px(76),p.rect.top+px(3),px(68),px(25)),p.binding.pinned?"Pinned":"Pin",[i]{commitNumber(true);panes[i].binding.pinned=!panes[i].binding.pinned;sync();},p.binding.pinned,p.rect);
        IntersectClipRect(dc,p.body.left,p.body.top,p.body.right,p.body.bottom);
        if(p.binding.track){if(i)mod(dc,p);
#ifdef KJ_ENABLE_VST3
            else if(instrumentView)instrument(dc,p);
#endif
            else fx(dc,p);
        }else text(dc,p.body,"Select a track to edit.");
        int height=std::max(1,int(p.body.bottom-p.body.top));p.scroll=std::clamp(p.scroll,0,std::max(0,p.contentHeight-height));
        if(p.contentHeight>height){RECT rail=rect(p.body.right-px(12),p.body.top,px(8),height);fill(dc,rail,RGB(45,45,45));int thumb=std::max(px(20),height*height/p.contentHeight);int y=p.body.top+(height-thumb)*p.scroll/std::max(1,p.contentHeight-height);fill(dc,rect(rail.left,y,px(8),thumb),RGB(0,125,190));}
        RestoreDC(dc,paneSave);
    }
    if(dockFocus&&focused>=0&&focused<static_cast<int>(hits.size())){RECT r=hits[focused].rect;FrameRect(dc,&r,static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));}
    if(!choices.empty()){fill(dc,chooser,RGB(35,35,35));int rows=std::max(1,int(chooser.bottom-chooser.top)/px(24));choiceScroll=std::clamp(choiceScroll,0,std::max(0,int(choices.size())-rows));for(int i=0;i<rows&&i+choiceScroll<static_cast<int>(choices.size());++i){RECT r=rect(chooser.left,chooser.top+i*px(24),chooser.right-chooser.left,px(24));if(i+choiceScroll==choiceSelected)fill(dc,r,RGB(0,100,165));r.left+=px(6);text(dc,r,choices[i+choiceScroll].text);}FrameRect(dc,&chooser,static_cast<HBRUSH>(GetStockObject(GRAY_BRUSH)));}
    if(typing){RECT r=rect(px(12),top+px(40),std::min<int>(client.right-px(24),px(420)),px(62));fill(dc,r,RGB(25,45,60));text(dc,rect(r.left+px(8),r.top,r.right-r.left-px(16),px(28)),numeric.label+": "+entry+"|");text(dc,rect(r.left+px(8),r.top+px(28),r.right-r.left-px(16),px(28)),"Enter: apply    Esc: cancel");}
    RestoreDC(dc,saved);
}
bool handleEditingPanelMessage(HWND window,UINT msg,WPARAM wp,LPARAM lp){
    if(!owner||window!=owner)return false;
    if(msg==WM_DESTROY){commitNumber(false);cancelGesture();owner=nullptr;return false;}
    if(msg==WM_CANCELMODE||msg==WM_CAPTURECHANGED){cancelGesture();choices.clear();commitNumber(false);return false;}
    if(msg==WM_KILLFOCUS){commitNumber(true);cancelGesture();choices.clear();dockFocus=false;return false;}
    // TranslateMessage still generates WM_CHAR for numeric input, but dock
    // keystrokes must never fall through into the sequencer's shortcuts.
    if(msg==WM_KEYDOWN&&dockFocus)return true;
    if(msg==WM_CHAR&&dockFocus){if(typing){char c=static_cast<char>(wp);if(c==8){if(!entry.empty())entry.pop_back();}else if((c>='0'&&c<='9')||c=='-'||c=='.'){if(entry.size()<24)entry+=c;}redraw();}return true;}
    if(eqGraphVisible&&choices.empty()&&!typing&&handleEqGraph(owner,eqGraph,msg,wp,lp)){dockFocus=true;return true;}
    if(msg==WM_MOUSEWHEEL){POINT pt{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};ScreenToClient(owner,&pt);if(!choices.empty()){choiceScroll-=GET_WHEEL_DELTA_WPARAM(wp)/WHEEL_DELTA*3;redraw();return true;}if(pt.y>=top){for(int i=0;i<2;++i)if((both()||active==i)&&contains(panes[i].body,pt)){auto& p=panes[i];p.scroll=std::clamp(p.scroll-GET_WHEEL_DELTA_WPARAM(wp)/WHEEL_DELTA*px(48),0,std::max(0,p.contentHeight-int(p.body.bottom-p.body.top)));}redraw();return true;}return false;}
    POINT pt{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
    if(msg==WM_MOUSEMOVE&&(sliding||divider)){if(sliding)slide(moving,pt.x);else{RECT r{};GetClientRect(owner,&r);splitFraction=double(pt.x)/std::max<LONG>(1,r.right);layout();}redraw();return true;}
    if(msg==WM_LBUTTONUP&&(sliding||divider)){cancelGesture();return true;}
    if(msg==WM_LBUTTONDOWN||msg==WM_LBUTTONDBLCLK){
        if(!choices.empty()){if(contains(chooser,pt)){int index=choiceScroll+(pt.y-chooser.top)/px(24);if(index>=0&&index<int(choices.size())){auto action=choices[index].action;choices.clear();action();}}else choices.clear();redraw();return true;}
        if(pt.y<top){commitNumber(true);dockFocus=false;return false;}
        SetFocus(owner);dockFocus=true;commitNumber(true);
        if(both()&&pt.y>=top+px(36)&&pt.x>=panes[0].rect.right&&pt.x<panes[1].rect.left){divider=2;SetCapture(owner);return true;}
        for(int i=int(hits.size())-1;i>=0;--i)if(contains(hits[i].rect,pt)){focused=i;Hit h=hits[i];if(h.set){if(msg==WM_LBUTTONDBLCLK){numeric=h;typing=true;entry=number(h.value);}else{moving=h;sliding=true;slide(moving,pt.x);SetCapture(owner);}}else activate(h);redraw();return true;}
        return true;
    }
    if((msg==WM_RBUTTONDOWN||msg==WM_RBUTTONUP)&&pt.y>=top){commitNumber(false);choices.clear();redraw();return true;}
    return false;
}
bool handleEditingPanelKeyboard(MSG* m){
    if(!owner||m->hwnd!=owner||!dockFocus||m->message!=WM_KEYDOWN)return false;int key=static_cast<int>(m->wParam);
    if(typing){if(key==VK_RETURN)commitNumber(true);else if(key==VK_ESCAPE)commitNumber(false);else return false;redraw();return true;}
    if(!choices.empty()){if(key==VK_ESCAPE)choices.clear();else if(key==VK_UP||key==VK_DOWN){choiceSelected=std::clamp(choiceSelected+(key==VK_UP?-1:1),0,int(choices.size())-1);int rows=std::max(1,int(chooser.bottom-chooser.top)/px(24));if(choiceSelected<choiceScroll)choiceScroll=choiceSelected;if(choiceSelected>=choiceScroll+rows)choiceScroll=choiceSelected-rows+1;}else if(key==VK_RETURN){auto action=choices[choiceSelected].action;choices.clear();action();}redraw();return true;}
    if(key==VK_ESCAPE){cancelGesture();dockFocus=false;redraw();return true;}
    if(key==VK_TAB){if(!hits.empty())focused=(focused+(GetKeyState(VK_SHIFT)<0?int(hits.size())-1:1))%int(hits.size());redraw();return true;}
    if(focused>=0&&focused<int(hits.size())){Hit h=hits[focused];if(key==VK_RETURN&&h.set){numeric=h;typing=true;entry=number(h.value);}else if((key==VK_RETURN||key==VK_SPACE)&&h.action)activate(h);else if(h.set&&(key==VK_LEFT||key==VK_RIGHT)){float delta=(h.hi-h.lo)*(GetKeyState(VK_SHIFT)<0?.001f:.01f);h.set(std::clamp(h.value+(key==VK_LEFT?-delta:delta),h.lo,h.hi));}redraw();}
    return true;
}
