#include "gui/eq_graph.h"
#include "core/parametric_eq.h"
#include "core/tracks.h"
#include "core/audio_engine.h"
#include <windowsx.h>
#include <array>
#include <string>
#include <cstdio>

namespace {
const COLORREF colors[]={RGB(46,193,173),RGB(243,167,67),RGB(119,153,244)};
RECT plot(const EqGraph& g){return {g.bounds.left+42,g.bounds.top+30,g.bounds.right-16,g.bounds.bottom-48};}
double rate(){double r=getAudioSampleRate();return r>100?r:44100;}
double maxHz(){return std::min(20000.0,rate()*.49);}
int xAt(const RECT& r,double hz){return r.left+static_cast<int>(std::log(std::clamp(hz,20.0,maxHz())/20)/std::log(maxHz()/20)*(r.right-r.left));}
int yAt(const RECT& r,double db){return r.top+static_cast<int>((18-db)/36*(r.bottom-r.top));}
float gain(int track,int b){return b==0?trackGetEqLowGain(track):b==1?trackGetEqMidGain(track):trackGetEqHighGain(track);}
void setGain(int track,int b,float v){if(b==0)trackSetEqLowGain(track,v);else if(b==1)trackSetEqMidGain(track,v);else trackSetEqHighGain(track,v);}
void fill(HDC dc,RECT r,COLORREF c){auto brush=CreateSolidBrush(c);FillRect(dc,&r,brush);DeleteObject(brush);}
void label(HDC dc,int x,int y,const std::string& s,COLORREF c){SetTextColor(dc,c);TextOutA(dc,x,y,s.c_str(),static_cast<int>(s.size()));}
void line(HDC dc,int x,int y,int x2,int y2,COLORREF color,int width=1){auto pen=CreatePen(PS_SOLID,width,color);auto old=SelectObject(dc,pen);MoveToEx(dc,x,y,nullptr);LineTo(dc,x2,y2);SelectObject(dc,old);DeleteObject(pen);}
eq::Shape shape(int track,int band){return static_cast<eq::Shape>(trackGetEqShape(track,band));}
void apply(EqGraph& g,POINT p){auto r=plot(g);double t=std::clamp(double(p.x-r.left)/std::max<LONG>(1,r.right-r.left),0.0,1.0);
    trackSetEqFrequency(g.track,g.selected,static_cast<float>(20*std::pow(maxHz()/20,t)));
    if(eq::hasGain(shape(g.track,g.selected)))setGain(g.track,g.selected,static_cast<float>(std::clamp(18-36.0*(p.y-r.top)/std::max<LONG>(1,r.bottom-r.top),-12.0,12.0)));
}
int nearest(const EqGraph& g,POINT p,bool requireHandle=false){auto r=plot(g);int best=g.selected;double distance=1e30;
    for(int b=0;b<3;++b){double x=xAt(r,trackGetEqFrequency(g.track,b))-p.x,y=yAt(r,eq::hasGain(shape(g.track,b))?gain(g.track,b):0)-p.y;double d=x*x+y*y;if(d<distance){distance=d;best=b;}}return requireHandle&&distance>196?-1:best;
}
LRESULT CALLBACK GraphProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
    auto g=reinterpret_cast<EqGraph*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    if(msg==WM_CREATE){g=new EqGraph;SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(g));SetTimer(hwnd,1,40,nullptr);return 0;}
    if(!g)return DefWindowProcW(hwnd,msg,wp,lp);
    if(msg==WM_NCDESTROY){KillTimer(hwnd,1);delete g;SetWindowLongPtrW(hwnd,GWLP_USERDATA,0);return DefWindowProcW(hwnd,msg,wp,lp);}
    if(msg==WM_TIMER){InvalidateRect(hwnd,nullptr,FALSE);return 0;}
    GetClientRect(hwnd,&g->bounds);g->clip=g->bounds;
    if(msg==WM_PAINT){PAINTSTRUCT ps{};auto target=BeginPaint(hwnd,&ps);auto dc=CreateCompatibleDC(target);auto bmp=CreateCompatibleBitmap(target,std::max<LONG>(1,g->bounds.right),std::max<LONG>(1,g->bounds.bottom));auto old=SelectObject(dc,bmp);drawEqGraph(dc,*g);BitBlt(target,0,0,g->bounds.right,g->bounds.bottom,dc,0,0,SRCCOPY);SelectObject(dc,old);DeleteObject(bmp);DeleteDC(dc);EndPaint(hwnd,&ps);return 0;}
    if(handleEqGraph(hwnd,*g,msg,wp,lp)){SendMessageW(GetParent(hwnd),WM_COMMAND,MAKEWPARAM(0,EN_CHANGE),reinterpret_cast<LPARAM>(hwnd));return 0;}
    return DefWindowProcW(hwnd,msg,wp,lp);
}
}
void drawEqGraph(HDC dc,EqGraph& g){
    int saved=SaveDC(dc);IntersectClipRect(dc,g.clip.left,g.clip.top,g.clip.right,g.clip.bottom);
    IntersectClipRect(dc,g.bounds.left,g.bounds.top,g.bounds.right,g.bounds.bottom);
    g.selected=std::clamp(g.selected,0,2);
    const bool enabled=trackGetEqEnabled(g.track);
    const double sampleRate=rate(),upperHz=std::min(20000.0,sampleRate*.49);
    SetBkMode(dc,TRANSPARENT);SelectObject(dc,GetStockObject(DEFAULT_GUI_FONT));fill(dc,g.bounds,RGB(18,24,30));
    auto r=plot(g);if(r.right<=r.left||r.bottom<=r.top){RestoreDC(dc,saved);return;}

    std::array<eq::Coefficients,3> coeff;
    for(int b=0;b<3;++b)coeff[b]=eq::coefficients(shape(g.track,b),sampleRate,trackGetEqFrequency(g.track,b),gain(g.track,b),trackGetEqQ(g.track,b));
    if(!g.analyzer||g.analyzer->track!=g.track){g.analyzer.reset();g.analyzer=spectrum::watch(g.track);g.spectrumDb.fill(-100);g.spectrumTime=0;g.levelTime=0;}
    auto now=GetTickCount64();
    if(now-g.spectrumTime>=30){
        std::array<float,spectrum::size/2+1> bins;bins.fill(-100);
        if(g.analyzer && g.analyzer->read(bins))g.levelTime=now;
        double seconds=g.spectrumTime?std::min(1.0,(now-g.spectrumTime)/1000.0):.03;
        for(size_t i=0;i<bins.size();++i){float target=std::max(-100.f,bins[i]);double blend=1-std::exp(-seconds/(target>g.spectrumDb[i]?.035:.25));g.spectrumDb[i]+=static_cast<float>((target-g.spectrumDb[i])*blend);}
        g.spectrumTime=now;
    }
    int curveSave=SaveDC(dc);IntersectClipRect(dc,r.left,r.top,r.right,r.bottom);
    std::vector<POINT> spectrumPoints;spectrumPoints.reserve(r.right-r.left+3);spectrumPoints.push_back({r.left,r.bottom});
    for(int x=r.left;x<=r.right;++x){
        double frequency=20*std::pow(upperHz/20,double(x-r.left)/(r.right-r.left));
        double next=20*std::pow(upperHz/20,double(x+1-r.left)/(r.right-r.left));
        double bin=std::clamp(frequency*spectrum::size/sampleRate,0.0,double(spectrum::size/2-1));int low=static_cast<int>(bin);
        float db=g.spectrumDb[low]+static_cast<float>(bin-low)*(g.spectrumDb[low+1]-g.spectrumDb[low]);
        int high=std::clamp(static_cast<int>(next*spectrum::size/sampleRate),low,spectrum::size/2);
        for(int i=low+1;i<=high;++i)db=std::max(db,g.spectrumDb[i]);
        int y=r.bottom-static_cast<int>((std::clamp(db,-90.f,0.f)+90)/90*(r.bottom-r.top));spectrumPoints.push_back({x,y});
    }
    spectrumPoints.push_back({r.right,r.bottom});
    auto spectrumBrush=CreateSolidBrush(RGB(30,53,64));auto spectrumPen=CreatePen(PS_SOLID,1,RGB(72,113,130));
    auto previousBrush=SelectObject(dc,spectrumBrush),previousPen=SelectObject(dc,spectrumPen);
    Polygon(dc,spectrumPoints.data(),static_cast<int>(spectrumPoints.size()));SelectObject(dc,previousBrush);SelectObject(dc,previousPen);DeleteObject(spectrumBrush);DeleteObject(spectrumPen);

    RestoreDC(dc,curveSave);
    for(int db=-12;db<=12;db+=6){int y=yAt(r,db);line(dc,r.left,y,r.right,y,db?RGB(35,45,54):RGB(80,99,110));label(dc,g.bounds.left+5,y-7,std::to_string(db),RGB(133,155,169));}
    for(int hz:{20,50,100,200,500,1000,2000,5000,10000,20000}){if(hz>maxHz())continue;int x=xAt(r,hz);line(dc,x,r.top,x,r.bottom,RGB(35,45,54));if(r.right-r.left>650||hz==20||hz==100||hz==1000||hz==10000)label(dc,x-8,r.bottom+5,hz>=1000?std::to_string(hz/1000)+"k":std::to_string(hz),RGB(133,155,169));}
    curveSave=SaveDC(dc);IntersectClipRect(dc,r.left,r.top,r.right,r.bottom);
    for(int curve=0;curve<4;++curve){auto pen=CreatePen(PS_SOLID,curve==3?2:1,curve==3?RGB(234,239,222):colors[curve]);auto old=SelectObject(dc,pen);
        for(int x=r.left;x<=r.right;++x){double hz=20*std::pow(upperHz/20,double(x-r.left)/(r.right-r.left)),db=0;
            if(curve<3)db=eq::responseDb(coeff[curve],sampleRate,hz);else if(enabled)for(auto& c:coeff)db+=eq::responseDb(c,sampleRate,hz);
            int y=yAt(r,std::clamp(db,-36.0,36.0));if(x==r.left)MoveToEx(dc,x,y,nullptr);else LineTo(dc,x,y);
        }SelectObject(dc,old);DeleteObject(pen);
    }
    RestoreDC(dc,curveSave);
    for(int b=0;b<3;++b){int x=xAt(r,trackGetEqFrequency(g.track,b)),y=yAt(r,eq::hasGain(shape(g.track,b))?gain(g.track,b):0),radius=b==g.selected?7:5;
        auto brush=CreateSolidBrush(shape(g.track,b)==eq::Shape::Off?RGB(85,90,95):colors[b]);auto oldBrush=SelectObject(dc,brush);auto oldPen=SelectObject(dc,GetStockObject(WHITE_PEN));Ellipse(dc,x-radius,y-radius,x+radius+1,y+radius+1);SelectObject(dc,oldPen);SelectObject(dc,oldBrush);DeleteObject(brush);label(dc,x+10,y-18,std::to_string(b+1),colors[b]);
    }
    char levels[160];
    if(g.analyzer && g.levelTime && now-g.levelTime<400 && g.analyzer->hasInput)
        sprintf_s(levels,"EQ RMS: In %.1f / Out %.1f dBFS (%+.1f dB)",g.analyzer->inputDb,g.analyzer->outputDb,g.analyzer->outputDb-g.analyzer->inputDb);
    else sprintf_s(levels,"EQ RMS: no input");
    label(dc,g.bounds.left+12,g.bounds.top+7,levels,RGB(218,230,235));
    if(r.right-r.left>600) {
        bool clipping=isAudioOutputClipping();
        label(dc,g.bounds.right-174,g.bounds.top+7,clipping?"MASTER CLIP":"Post EQ: -90..0 dBFS",clipping?RGB(255,120,85):RGB(115,158,176));
    }
    char value[192];sprintf_s(value,"B%d  %s  |  %.0f Hz  |  %.1f dB  |  Q %.2f%s",g.selected+1,eq::names[trackGetEqShape(g.track,g.selected)],trackGetEqFrequency(g.track,g.selected),gain(g.track,g.selected),trackGetEqQ(g.track,g.selected),enabled?"":"  [BYPASSED]");
    label(dc,g.bounds.left+12,g.bounds.bottom-20,value,RGB(206,218,225));RestoreDC(dc,saved);
}
bool handleEqGraph(HWND owner,EqGraph& g,UINT msg,WPARAM wp,LPARAM lp){
    if(msg==WM_CANCELMODE||msg==WM_CAPTURECHANGED||msg==WM_KILLFOCUS){g.dragging=false;if(msg!=WM_CAPTURECHANGED&&GetCapture()==owner)ReleaseCapture();return false;}
    auto repaint=[&]{InvalidateRect(owner,&g.bounds,FALSE);UpdateWindow(owner);};
    if(msg==WM_LBUTTONUP&&g.dragging){g.dragging=false;ReleaseCapture();return true;}
    POINT p{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
    if(msg==WM_MOUSEMOVE&&g.dragging){p.x+=g.dragOffsetX;p.y+=g.dragOffsetY;apply(g,p);repaint();return true;}
    if(msg==WM_MOUSEWHEEL)ScreenToClient(owner,&p);
    auto r=plot(g);RECT hit=r;InflateRect(&hit,10,10);if(r.right<=r.left||r.bottom<=r.top||!PtInRect(&hit,p)||!PtInRect(&g.clip,p)||!PtInRect(&g.bounds,p)||g.track<=0)return false;
    if(msg==WM_LBUTTONDOWN){int band=nearest(g,p,true);if(band<0)return false;g.selected=band;
        g.dragOffsetX=xAt(r,trackGetEqFrequency(g.track,band))-p.x;
        g.dragOffsetY=yAt(r,eq::hasGain(shape(g.track,band))?gain(g.track,band):0)-p.y;
        SetFocus(owner);SetCapture(owner);g.dragging=true;repaint();return true;}
    if(msg==WM_LBUTTONDBLCLK){int band=nearest(g,p,true);if(band<0)return false;g.selected=band;g.dragging=false;if(GetCapture()==owner)ReleaseCapture();setGain(g.track,g.selected,0);trackSetEqQ(g.track,g.selected,.707f);repaint();return true;}
    if(msg==WM_MOUSEWHEEL){int hovered=nearest(g,p,true);if(hovered>=0)g.selected=hovered;trackSetEqQ(g.track,g.selected,trackGetEqQ(g.track,g.selected)*std::pow(1.15f,GET_WHEEL_DELTA_WPARAM(wp)/120.f));repaint();return true;}
    if(msg==WM_RBUTTONUP){g.selected=nearest(g,p);auto menu=CreatePopupMenu();for(int i=0;i<7;++i)AppendMenuA(menu,MF_STRING|(i==trackGetEqShape(g.track,g.selected)?MF_CHECKED:0),i+1,eq::names[i]);ClientToScreen(owner,&p);int choice=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,p.x,p.y,0,owner,nullptr);DestroyMenu(menu);if(choice)trackSetEqShape(g.track,g.selected,choice-1);repaint();return true;}
    return false;
}
HWND createEqGraph(HWND parent){static bool registered=false;if(!registered){WNDCLASSW wc{};wc.lpfnWndProc=GraphProc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"KJParametricGraph";wc.hCursor=LoadCursor(nullptr,IDC_CROSS);wc.style=CS_DBLCLKS;registered=RegisterClassW(&wc)!=0;}return CreateWindowW(L"KJParametricGraph",L"EQ response",WS_CHILD|WS_VISIBLE,0,0,400,250,parent,nullptr,GetModuleHandleW(nullptr),nullptr);}
void setEqGraphTrack(HWND hwnd,int track){auto g=reinterpret_cast<EqGraph*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));if(g&&g->track!=track){g->dragging=false;if(GetCapture()==hwnd)ReleaseCapture();g->track=track;}InvalidateRect(hwnd,nullptr,FALSE);}
