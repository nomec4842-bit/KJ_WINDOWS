#include "core/parametric_eq.h"
#include "eq_audio_checks.h"
#include "gui/eq_graph.h"
#include "core/tracks.h"
#include <windowsx.h>
#include <fstream>
#include <limits>
#include <iostream>
#include <stdexcept>
static WNDPROC originalProc=nullptr;
static int graphNotifications=0;
LRESULT CALLBACK observe(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){if(msg==WM_COMMAND&&HIWORD(wp)==EN_CHANGE)++graphNotifications;return CallWindowProcW(originalProc,hwnd,msg,wp,lp);}
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
int main(int argc,char** argv){
    HWND window=nullptr;
    try{
        for(double sr:{32000.,44100.,48000.,96000.}){
            for(double bass:{20.,30.,40.,60.,100.,200.})for(double amount:{-12.,12.}){
                auto bassBell=eq::coefficients(eq::Shape::Bell,sr,bass,amount,4);
                check(std::abs(eq::responseDb(bassBell,sr,bass)-amount)<1e-5,"Bass bell center gain wrong");
                auto shelf=eq::coefficients(eq::Shape::LowShelf,sr,bass,amount,.707);
                check(std::abs(eq::responseDb(shelf,sr,0)-amount)<1e-5,"Bass shelf DC gain wrong");
                auto cut=eq::coefficients(eq::Shape::HighPass,sr,bass,0,1/std::sqrt(2.));
                check(std::abs(eq::responseDb(cut,sr,bass)+3.0103)<1e-4,"Low cut corner response wrong");
            }
            auto bell=eq::coefficients(eq::Shape::Bell,sr,1000,9,2);
            check(std::abs(eq::responseDb(bell,sr,1000)-9)<1e-6,"Bell center gain wrong");
            check(eq::responseDb(eq::coefficients(eq::Shape::LowPass,sr,1000,0,.707),sr,10000)<-35,"High cut does not attenuate highs");
            check(eq::responseDb(eq::coefficients(eq::Shape::HighPass,sr,1000,0,.707),sr,100)<-35,"Low cut does not attenuate lows");
            check(eq::responseDb(eq::coefficients(eq::Shape::Notch,sr,1000,0,4),sr,1000)<-100,"Notch does not reject its center");
            check(std::abs(eq::responseDb(eq::coefficients(eq::Shape::LowShelf,sr,1000,9,.707),sr,20)-9)<.01,"Low shelf gain wrong");
            check(std::abs(eq::responseDb(eq::coefficients(eq::Shape::HighShelf,sr,1000,-9,.707),sr,sr*.49)+9)<.01,"High shelf gain wrong");
            for(int type=0;type<7;++type)for(double q:{.1,.707,10.})for(double hz:{20.,1000.,20000.})for(double gain:{-12.,0.,12.}){
                auto c=eq::coefficients(static_cast<eq::Shape>(type),sr,hz,gain,q);
                auto root=std::sqrt(std::complex<double>(c.a1*c.a1-4*c.a2,0));
                check(std::abs((-c.a1+root)/2.)<1&&std::abs((-c.a1-root)/2.)<1,"Unstable EQ poles");
                double z1=0,z2=0;
                for(int n=0;n<4096;++n){double in=n==0?1:0,out=c.b0*in+z1;z1=c.b1*in-c.a1*out+z2;z2=c.b2*in-c.a2*out;check(std::isfinite(out)&&std::abs(out)<100,"Nonfinite or excessive impulse response");}
            }
        }
        initTracks();int track=getTracks().front().id;trackSetEqEnabled(track,true);
        auto defaults=getTracks().front();
        check(defaults.eqShape==std::array<int,3>{{1,0,0}} && defaults.eqFrequency[0]==200 && defaults.eqQ[0]==.707f && defaults.lowGainDb==0,"New EQ defaults wrong");
        for(double rate:{44100.,48000.,96000.}) for(float gain:{-12.f,12.f}) {
            trackSetEqLowGain(track,gain);
            auto settings=getTracks().front();
            for(double hz:{40.,60.,100.,200.,2000.}) {
                double measured=measureEq(settings,rate,hz);
                checkBassResponse(hz,gain,measured);
                std::cout<<rate<<" Hz sample rate, tone "<<hz<<", gain "<<gain<<": "<<measured<<" dB\n";
            }
            settings.eqEnabled=false;
            check(std::abs(measureEq(settings,rate,40))<1e-10,"EQ bypass changed samples");
        }
        // Reconfiguration must retain the independent channel histories during edits.
        auto state=getTracks().front();state.eqEnabled=true;
        eq::Processor continuous;continuous.configure(state,48000);
        for(int i=0;i<24000;++i) { double l=.02,r=0;continuous.process(l,r);check(r==0,"Left channel leaked into right"); }
        auto unchanged=continuous;
        continuous.configure(state,48000);
        double l=.02,r=0,expectedL=l,expectedR=r;
        continuous.process(l,r);unchanged.process(expectedL,expectedR);
        check(l==expectedL&&r==expectedR,"Unchanged snapshot reset filter history");
        state.lowGainDb=-6;continuous.configure(state,48000);
        l=0;r=0;continuous.process(l,r);
        check(std::abs(l)>1e-6&&r==0,"Gain editing discarded bass history");
        state.eqEnabled=false;continuous.configure(state,48000);
        l=.013;r=-.007;continuous.process(l,r);
        check(l==.013&&r==-.007,"Bypass is not exact passthrough");
        state.eqEnabled=true;continuous.configure(state,48000);
        l=r=0;continuous.process(l,r);check(l==0&&r==0,"Bypass retained stale tail");
        trackSetEqLowGain(track,0);
        window=CreateWindowW(L"STATIC",L"EQ test",WS_POPUP,0,0,900,340,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        EqGraph graph;graph.track=track;graph.bounds=graph.clip={0,0,900,340};
        handleEqGraph(window,graph,WM_LBUTTONDOWN,0,MAKELPARAM(322,161));
        handleEqGraph(window,graph,WM_MOUSEMOVE,0,MAKELPARAM(322,74));
        handleEqGraph(window,graph,WM_LBUTTONUP,0,MAKELPARAM(322,74));
        const auto dragged=getTracks().front();
        check(dragged.lowGainDb>11.9f,"Bass graph drag did not reach boost");
        trackSetEqLowGain(track,0); trackSetEqFrequency(track,0,500);
        // Numeric sliders use these same track setters.
        trackSetEqLowGain(track,dragged.lowGainDb);
        trackSetEqFrequency(track,0,dragged.eqFrequency[0]);
        trackSetEqQ(track,0,dragged.eqQ[0]);
        for(double hz:{40.,60.,100.,200.,2000.})
            check(std::abs(measureEq(dragged,48000,hz)-measureEq(getTracks().front(),48000,hz))<1e-10,"Graph and slider audio differ");
        trackSetEqLowGain(track,0); trackSetEqFrequency(track,0,200);
        check(!handleEqGraph(window,graph,WM_LBUTTONDOWN,0,MAKELPARAM(450,60)),"Empty graph space grabbed a band");
        check(trackGetEqMidGain(track)==0&&trackGetEqFrequency(track,1)==1000,"Empty click changed a band");
        float baseHz=trackGetEqFrequency(track,1);
        handleEqGraph(window,graph,WM_LBUTTONDOWN,0,MAKELPARAM(523,163));
        check(trackGetEqFrequency(track,1)==baseHz&&trackGetEqMidGain(track)==0,"Selecting a handle changed its values");
        handleEqGraph(window,graph,WM_CANCELMODE,0,0);
        check(!graph.dragging&&GetCapture()!=window,"Cancel left mouse captured");
        trackSetEqLowGain(track,std::numeric_limits<float>::quiet_NaN());
        trackSetEqMidGain(track,std::numeric_limits<float>::infinity());
        trackSetEqHighGain(track,-std::numeric_limits<float>::infinity());
        check(trackGetEqLowGain(track)==0&&trackGetEqMidGain(track)==0&&trackGetEqHighGain(track)==0,"Invalid EQ gain entered audio state");
        // Center band's handle begins near x=519, y=161.
        check(handleEqGraph(window,graph,WM_LBUTTONDOWN,0,MAKELPARAM(519,161)),"Graph did not accept click");
        handleEqGraph(window,graph,WM_MOUSEMOVE,0,MAKELPARAM(600,120));
        handleEqGraph(window,graph,WM_LBUTTONUP,0,MAKELPARAM(600,120));
        check(trackGetEqFrequency(track,1)>1800&&trackGetEqMidGain(track)>5,"Drag did not change frequency and gain");
        POINT pt{600,120};ClientToScreen(window,&pt);float before=trackGetEqQ(track,1);
        handleEqGraph(window,graph,WM_MOUSEWHEEL,MAKEWPARAM(0,WHEEL_DELTA),MAKELPARAM(pt.x,pt.y));
        check(trackGetEqQ(track,1)>before,"Wheel did not change Q");
        trackSetEqFrequency(track,0,20);graph.selected=1;
        float midQ=trackGetEqQ(track,1),lowQ=trackGetEqQ(track,0);
        POINT lowPoint{42,161};ClientToScreen(window,&lowPoint);
        handleEqGraph(window,graph,WM_MOUSEWHEEL,MAKEWPARAM(0,WHEEL_DELTA),MAKELPARAM(lowPoint.x,lowPoint.y));
        check(trackGetEqQ(track,0)>lowQ&&trackGetEqQ(track,1)==midQ,"Low-band wheel changed another band");
        check(handleEqGraph(window,graph,WM_LBUTTONDOWN,0,MAKELPARAM(42,161)),"20 Hz handle not reachable");
        handleEqGraph(window,graph,WM_MOUSEMOVE,0,MAKELPARAM(20,120));
        handleEqGraph(window,graph,WM_LBUTTONUP,0,MAKELPARAM(20,120));
        check(trackGetEqFrequency(track,0)==20&&trackGetEqLowGain(track)>5,"Bass edge drag lost frequency clamp or gain control");
        trackSetEqFrequency(track,2,20000);
        check(handleEqGraph(window,graph,WM_LBUTTONDOWN,0,MAKELPARAM(884,161)),"Right-edge handle cannot be grabbed");
        handleEqGraph(window,graph,WM_LBUTTONUP,0,MAKELPARAM(884,161));
        trackSetEqFrequency(track,2,5000);
        trackSetEqShape(track,0,static_cast<int>(eq::Shape::HighPass));trackSetEqFrequency(track,0,75);
        trackSetEqShape(track,2,static_cast<int>(eq::Shape::HighShelf));trackSetEqHighGain(track,-4);
        auto snapshot=getTracks().front();check(snapshot.eqShape[0]==4&&snapshot.eqShape[2]==2,"Filter shapes missing from audio snapshot");
        originalProc=reinterpret_cast<WNDPROC>(SetWindowLongPtrW(window,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(observe)));
        HWND child=createEqGraph(window);setEqGraphTrack(child,track);
        // The child graph must tell the popup to refresh its numeric controls.
        POINT wheel{200,110};ClientToScreen(child,&wheel);
        SendMessageW(child,WM_MOUSEWHEEL,MAKEWPARAM(0,WHEEL_DELTA),MAKELPARAM(wheel.x,wheel.y));
        check(graphNotifications>0,"Graph edits did not notify popup controls");
        DestroyWindow(child);
        if(argc>1){
            auto screen=GetDC(window);auto dc=CreateCompatibleDC(screen);auto bmp=CreateCompatibleBitmap(screen,900,340);auto old=SelectObject(dc,bmp);drawEqGraph(dc,graph);
            for(int frame=0;frame<5;++frame){for(int i=0;i<spectrum::size;++i){float sample=static_cast<float>(.25*std::sin(6.283185307179586*9*i/spectrum::size)+.12*std::sin(6.283185307179586*90*i/spectrum::size));spectrum::capture(track,sample,-sample);}Sleep(35);drawEqGraph(dc,graph);}
            SelectObject(dc,old);
            BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=900;info.bmiHeader.biHeight=-340;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
            std::vector<unsigned char> pixels(900*340*4);GetDIBits(dc,bmp,0,340,pixels.data(),&info,DIB_RGB_COLORS);
            BITMAPFILEHEADER header{};header.bfType=0x4d42;header.bfOffBits=sizeof(header)+sizeof(info.bmiHeader);header.bfSize=header.bfOffBits+static_cast<DWORD>(pixels.size());
            std::ofstream out(argv[1],std::ios::binary);out.write(reinterpret_cast<char*>(&header),sizeof(header));out.write(reinterpret_cast<char*>(&info.bmiHeader),sizeof(info.bmiHeader));out.write(reinterpret_cast<char*>(pixels.data()),pixels.size());
            DeleteObject(bmp);DeleteDC(dc);ReleaseDC(window,screen);
        }
        DestroyWindow(window);std::cout<<"PASS EQ shapes, gain, stability, graph dragging, Q and audio snapshot\n";
    }catch(const std::exception& e){if(window)DestroyWindow(window);std::cerr<<e.what()<<'\n';return 1;}
    return 0;
}
