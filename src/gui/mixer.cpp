#include "gui/mixer.h"
#include "gui/editing_panel.h"
#include "core/tracks.h"
#include "core/sequencer.h"
#include "core/audio_input.h"
#include "core/audio_meter.h"
#include <windowsx.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <vector>
namespace {
HWND window=nullptr,ownerWindow=nullptr;
int scroll=0;
bool clipped=false;
std::vector<input::Device> knownDevices;
ULONGLONG lastDeviceRefresh=0;
void refreshDevices(){knownDevices=input::devices();lastDeviceRefresh=GetTickCount64();}
constexpr int stripWidth=210;
struct Hit {RECT r; int track; int slider; std::function<void()> action;};
std::vector<Hit> hits;
Hit drag{};
bool dragging=false;
std::wstring wide(const std::string& s){int n=MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),nullptr,0);std::wstring w(n,0);MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),w.data(),n);return w;}
RECT box(int x,int y,int w,int h){return {x,y,x+w,y+h};}
void fill(HDC dc,RECT r,COLORREF color){HBRUSH brush=CreateSolidBrush(color);FillRect(dc,&r,brush);DeleteObject(brush);}
void text(HDC dc,RECT r,const std::string& s,COLORREF color=RGB(235,235,235)){SetTextColor(dc,color);auto w=wide(s);DrawTextW(dc,w.c_str(),int(w.size()),&r,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX);}
void button(HDC dc,RECT r,const std::string& label,bool on,int track,std::function<void()> action){fill(dc,r,on?RGB(0,109,162):RGB(48,48,48));FrameRect(dc,&r,static_cast<HBRUSH>(GetStockObject(GRAY_BRUSH)));RECT labelRect=r;labelRect.left+=8;text(dc,labelRect,label);hits.push_back({r,track,0,std::move(action)});}
void slider(HDC dc,RECT r,const char* name,float value,int id,bool pan){fill(dc,r,RGB(34,34,34));char label[64];std::snprintf(label,sizeof(label),"%s: %.3f",name,value);RECT labelRect=r;labelRect.bottom=labelRect.top+24;text(dc,labelRect,label);RECT bar=r;bar.top+=27;bar.bottom-=3;fill(dc,bar,RGB(65,65,65));bar.right=bar.left+int((bar.right-bar.left)*(pan?(value+1)*.5f:value));fill(dc,bar,RGB(0,128,181));hits.push_back({r,id,pan?2:1,{}});}
void drawMeter(HDC dc,int x,int y,int width,int height,float l,float r){
    auto draw=[&](int xx,float value){RECT bounds=box(xx,y,width,height);fill(dc,bounds,RGB(12,18,20));double level=std::clamp((20*std::log10(std::max(value,0.00001f))+60)/60.,0.,1.);bounds.top=bounds.bottom-int(level*height);fill(dc,bounds,value>=1?RGB(219,75,65):RGB(35,176,146));};
    draw(x,l);draw(x+width+4,r);
}
void updateScroll(HWND hwnd){RECT r;GetClientRect(hwnd,&r);int total=int(getTracks().size()+1)*stripWidth+12;scroll=std::clamp(scroll,0,std::max(0,total-int(r.right)));SCROLLINFO si{sizeof(si),SIF_RANGE|SIF_PAGE|SIF_POS,0,total-1,UINT(r.right),scroll};SetScrollInfo(hwnd,SB_HORZ,&si,TRUE);}
void paint(HWND hwnd,HDC dc){RECT client;GetClientRect(hwnd,&client);fill(dc,client,RGB(18,18,18));SetBkMode(dc,TRANSPARENT);SelectObject(dc,GetStockObject(DEFAULT_GUI_FONT));hits.clear();auto tracks=getTracks();int x=8-scroll;
    for(const auto& t:tracks){
        int id=t.id;fill(dc,box(x,8,stripWidth-8,std::max(470,int(client.bottom)-16)),RGB(27,27,27));
        button(dc,box(x+6,14,190,32),t.name,getActiveSequencerTrackId()==id,id,[id]{setActiveSequencerTrackId(id);if(ownerWindow)InvalidateRect(ownerWindow,nullptr,FALSE);});
        text(dc,box(x+8,48,185,24),t.type==TrackType::AudioIn?"Audio In":t.type==TrackType::MidiOut?"MIDI Out (external)":"Track output",RGB(130,180,194));
        if(t.type!=TrackType::MidiOut){auto level=meter::track(id);drawMeter(dc,x+74,80,22,130,level?level->left.load():0,level?level->right.load():0);
            slider(dc,box(x+8,220,186,48),"Volume",trackGetVolume(id),id,false);slider(dc,box(x+8,274,186,48),"Pan",trackGetPan(id),id,true);
        }else{text(dc,box(x+8,120,186,40),"External audio / no fader",RGB(130,130,130));}
        button(dc,box(x+8,332,89,32),"Mute",t.mute,id,[id,t]{trackSetMute(id,!t.mute);});button(dc,box(x+103,332,91,32),"Solo",t.solo,id,[id,t]{trackSetSolo(id,!t.solo);});
        button(dc,box(x+8,372,186,32),"FX / Track editor",false,id,[id]{revealEditingPanel(EditingPage::Fx,id);if(ownerWindow){ShowWindow(ownerWindow,SW_SHOW);SetForegroundWindow(ownerWindow);}});
        if(t.type==TrackType::AudioIn){
            button(dc,box(x+8,412,186,32),t.inputMonitor?"Monitor: On":"Monitor: Off",t.inputMonitor,id,[id,t]{trackSetInputMonitor(id,!t.inputMonitor);});
            button(dc,box(x+8,452,186,32),t.inputDeviceName.empty()?"Select input / channels":t.inputDeviceName,false,id,[hwnd,id]{showAudioInputSettings(hwnd,id);});
            text(dc,box(x+8,488,186,26),audioInputDescription(t),RGB(150,188,196));
        }
        x+=stripWidth;
    }
    fill(dc,box(x,8,stripWidth-8,std::max(470,int(client.bottom)-16)),RGB(23,32,36));text(dc,box(x+12,14,180,32),"MASTER");
    auto& master=meter::master();float l=master.left.load(),r=master.right.load();clipped=master.clipped.load();drawMeter(dc,x+60,80,34,242,l,r);
    button(dc,box(x+8,332,186,32),clipped?"CLIP - click to clear":"No clipping",clipped,0,[]{clipped=false;meter::master().clipped=false;});
    text(dc,box(x+8,372,186,32),"Output / master recording");
}
void slide(int x){float amount=std::clamp(float(x-drag.r.left)/std::max(1L,drag.r.right-drag.r.left),0.f,1.f);if(drag.slider==1)trackSetVolume(drag.track,amount);else trackSetPan(drag.track,amount*2-1);if(ownerWindow)InvalidateRect(ownerWindow,nullptr,FALSE);}
LRESULT CALLBACK proc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){switch(msg){
case WM_CREATE:SetTimer(hwnd,1,33,nullptr);return 0;
case WM_GETMINMAXINFO:{auto p=reinterpret_cast<MINMAXINFO*>(lp);p->ptMinTrackSize={340,580};return 0;}
case WM_SIZE:updateScroll(hwnd);InvalidateRect(hwnd,nullptr,FALSE);return 0;
case WM_TIMER:updateScroll(hwnd);InvalidateRect(hwnd,nullptr,FALSE);return 0;
case WM_ERASEBKGND:return 1;
case WM_PRINTCLIENT:paint(hwnd,reinterpret_cast<HDC>(wp));return 0;
case WM_PAINT:{PAINTSTRUCT ps;HDC dc=BeginPaint(hwnd,&ps);RECT r;GetClientRect(hwnd,&r);HDC mem=CreateCompatibleDC(dc);HBITMAP bmp=CreateCompatibleBitmap(dc,std::max(1L,r.right),std::max(1L,r.bottom));auto old=SelectObject(mem,bmp);paint(hwnd,mem);BitBlt(dc,0,0,r.right,r.bottom,mem,0,0,SRCCOPY);SelectObject(mem,old);DeleteObject(bmp);DeleteDC(mem);EndPaint(hwnd,&ps);return 0;}
case WM_HSCROLL:{SCROLLINFO si{sizeof(si),SIF_ALL};GetScrollInfo(hwnd,SB_HORZ,&si);switch(LOWORD(wp)){case SB_LINELEFT:scroll-=40;break;case SB_LINERIGHT:scroll+=40;break;case SB_PAGELEFT:scroll-=si.nPage;break;case SB_PAGERIGHT:scroll+=si.nPage;break;case SB_THUMBTRACK:case SB_THUMBPOSITION:scroll=si.nTrackPos;break;case SB_LEFT:scroll=0;break;case SB_RIGHT:scroll=si.nMax;break;}updateScroll(hwnd);InvalidateRect(hwnd,nullptr,FALSE);return 0;}
case WM_MOUSEWHEEL:scroll-=GET_WHEEL_DELTA_WPARAM(wp)/WHEEL_DELTA*100;updateScroll(hwnd);InvalidateRect(hwnd,nullptr,FALSE);return 0;
case WM_LBUTTONDOWN:{POINT pt{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};for(auto h:hits)if(PtInRect(&h.r,pt)){if(h.slider){drag=h;dragging=true;SetCapture(hwnd);slide(pt.x);}else if(h.action)h.action();InvalidateRect(hwnd,nullptr,FALSE);break;}return 0;}
case WM_MOUSEMOVE:if(dragging){slide(GET_X_LPARAM(lp));InvalidateRect(hwnd,nullptr,FALSE);}return 0;
case WM_LBUTTONUP:if(dragging){dragging=false;ReleaseCapture();}return 0;
case WM_CAPTURECHANGED:dragging=false;return 0;
case WM_CLOSE:DestroyWindow(hwnd);return 0;
case WM_DESTROY:KillTimer(hwnd,1);window=nullptr;hits.clear();dragging=false;return 0;
}return DefWindowProcW(hwnd,msg,wp,lp);}
}
std::string audioInputDescription(const Track& t){
    if(!lastDeviceRefresh||GetTickCount64()-lastDeviceRefresh>2000)refreshDevices();
    if(t.inputDeviceId.empty())return "Select an input device";
    auto found=std::find_if(knownDevices.begin(),knownDevices.end(),[&](const input::Device& d){return d.id==t.inputDeviceId;});
    if(found==knownDevices.end())return "Input unavailable";
    if(t.inputChannel+(t.inputStereo?1:0)>=found->channels)return "Selected channels unavailable";
    std::string channels=t.inputStereo?"Stereo "+std::to_string(t.inputChannel+1)+"/"+std::to_string(t.inputChannel+2):"Mono "+std::to_string(t.inputChannel+1);
    return (t.inputMonitor?input::status(t.inputDeviceId):std::string("Monitor off"))+" | "+channels;
}
void showAudioInputSettings(HWND hwnd,int id){
    for(;;){auto all=getTracks();auto it=std::find_if(all.begin(),all.end(),[id](const Track& t){return t.id==id;});if(it==all.end()||it->type!=TrackType::AudioIn)return;Track t=*it;
        refreshDevices();auto devices=knownDevices;HMENU menu=CreatePopupMenu(),deviceMenu=CreatePopupMenu(),channelMenu=CreatePopupMenu();int count=0;
        for(size_t i=0;i<devices.size();++i){auto label=wide(devices[i].name);AppendMenuW(deviceMenu,MF_STRING|(t.inputDeviceId==devices[i].id?MF_CHECKED:0),100+UINT(i),label.c_str());if(devices[i].id==t.inputDeviceId)count=devices[i].channels;}
        if(devices.empty())AppendMenuW(deviceMenu,MF_STRING|MF_GRAYED,0,L"No Windows inputs available");
        AppendMenuW(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(deviceMenu),L"Input device");
        for(int ch=0;ch<std::min(count,input::maxChannels);++ch){auto label=L"Mono channel "+std::to_wstring(ch+1);AppendMenuW(channelMenu,MF_STRING|(!t.inputStereo&&t.inputChannel==ch?MF_CHECKED:0),1000+ch,label.c_str());}
        for(int ch=0;ch+1<std::min(count,input::maxChannels);ch+=2){auto label=L"Stereo pair "+std::to_wstring(ch+1)+L" / "+std::to_wstring(ch+2);AppendMenuW(channelMenu,MF_STRING|(t.inputStereo&&t.inputChannel==ch?MF_CHECKED:0),2000+ch,label.c_str());}
        if(!count)AppendMenuW(channelMenu,MF_STRING|MF_GRAYED,0,L"Selected input unavailable");
        AppendMenuW(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(channelMenu),L"Input channels");
        AppendMenuW(menu,MF_STRING|(t.inputMonitor?MF_CHECKED:0)|(t.inputDeviceId.empty()?MF_GRAYED:0),1,L"Monitor");AppendMenuW(menu,MF_STRING,2,L"Refresh Devices");
        POINT pt;GetCursorPos(&pt);int cmd=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_NONOTIFY|TPM_RIGHTBUTTON,pt.x,pt.y,0,hwnd,nullptr);DestroyMenu(menu);
        if(cmd==2)continue;
        if(cmd==1)trackSetInputMonitor(id,!t.inputMonitor);
        else if(cmd>=100&&cmd<100+int(devices.size()))trackSetInputDevice(id,devices[cmd-100].id,devices[cmd-100].name);
        else if(cmd>=1000&&cmd<1000+count)trackSetInputChannels(id,cmd-1000,false);
        else if(cmd>=2000&&cmd<2000+count)trackSetInputChannels(id,cmd-2000,true);
        InvalidateRect(hwnd,nullptr,FALSE);return;
    }
}
void showMixerWindow(HWND owner,bool visible){ownerWindow=owner;if(window){ShowWindow(window,SW_RESTORE);SetForegroundWindow(window);return;}WNDCLASSW wc{};wc.lpfnWndProc=proc;wc.hInstance=GetModuleHandleW(nullptr);wc.hCursor=LoadCursor(nullptr,IDC_ARROW);wc.lpszClassName=L"KJMixer";RegisterClassW(&wc);window=CreateWindowExW(0,wc.lpszClassName,L"KJ Mixer",WS_OVERLAPPEDWINDOW|WS_HSCROLL,CW_USEDEFAULT,CW_USEDEFAULT,900,600,owner,nullptr,wc.hInstance,nullptr);if(visible)ShowWindow(window,SW_SHOW);}
void closeMixerWindow(){if(window)DestroyWindow(window);}
