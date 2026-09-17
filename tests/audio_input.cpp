#include "core/audio_input.h"
#include "core/audio_meter.h"
#include "core/project_io.h"
#include "core/sequencer.h"
#include "core/eq_processor.h"
#include "gui/mixer.h"
#include <windows.h>
#include <objbase.h>
#include <iostream>
#include <cmath>
#include <stdexcept>
#include <fstream>
namespace input {void setTestSource(const std::string&,std::shared_ptr<Buffer>);}
void check(bool v,const char* message){if(!v)throw std::runtime_error(message);}
void conversion(int rate,int out,double drift,double frequency=997){
 auto buffer=std::make_shared<input::Buffer>();buffer->configure(4,rate);input::Reader reader,other;reader.bind(buffer);other.bind(buffer);
 uint64_t generated=0;double due=rate*.04;double power=0;int count=0,dropouts=0,crossings=0;double prev=0;
 for(int block=0;block<3000;++block){
  due+=256.*rate/out*(1+drift);int frames=int(due)-int(generated);std::vector<float> values(frames*4);
  for(int i=0;i<frames;++i){double wave=.1*std::sin(6.283185307179586*frequency*(generated+i)/rate);values[i*4]=float(wave);values[i*4+1]=float(-wave*.5);values[i*4+2]=.125f;values[i*4+3]=-.25f;}
  buffer->push(values.data(),frames);generated+=frames;reader.beginBlock(out);other.beginBlock(out);
  for(int i=0;i<256;++i){double l,r,a,b;reader.read(0,true,l,r);other.read(2,true,a,b);check(std::abs(r+l*.5)<1e-8,"Stereo SRC lost channel relationship");check(std::abs(a-.125)<1e-8&&std::abs(b+.25)<1e-8,"Shared reader lost independent channel selection");if(block>50){power+=l*l;++count;if(l==0&&r==0)++dropouts;if(prev<=0&&l>0)++crossings;prev=l;}}
 }
 double rms=std::sqrt(power/count),hz=double(crossings)*out/count;
 check(dropouts<3,"Clock drift caused dropouts");if(frequency<out*.5){check(std::abs(rms-.07071)<.002,"SRC amplitude changed");check(std::abs(hz-frequency*(1+drift))<2,"SRC pitch changed");}else check(rms<.001,"Downsampling aliased ultrasonic input");
 std::cout<<rate<<" -> "<<out<<" drift "<<drift<<" RMS "<<rms<<" Hz "<<hz<<'\n';
 reader.beginBlock(out);double l,r;reader.read(3,true,l,r);check(l==0&&r==0,"Invalid pair not silent");reader.read(2,false,l,r);check(l==.125&&r==.125,"Mono did not duplicate selected channel");
 buffer->available=false;reader.read(0,true,l,r);check(l==0&&r==0&&!reader.ready(0,true),"Disconnect not silent");buffer->configure(1,48000);reader.beginBlock(out);reader.read(0,false,l,r);check(l==0&&r==0,"Reconnect played stale samples");
 std::vector<float> silence(20000,.25f);buffer->push(silence.data(),int(silence.size()));reader.beginBlock(out);reader.read(0,false,l,r);check(l==.25&&r==.25,"Overrun did not recover");
 for(int i=0;i<20000;++i)reader.read(0,false,l,r);check(l==0&&r==0,"Underrun did not become silent");
}
void click(HWND hwnd,int x,int y){SendMessageW(hwnd,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(x,y));SendMessageW(hwnd,WM_LBUTTONUP,0,MAKELPARAM(x,y));}
std::string preview;
void draw(HWND hwnd){HDC dc=GetDC(hwnd);RECT r;GetClientRect(hwnd,&r);HDC mem=CreateCompatibleDC(dc);auto bmp=CreateCompatibleBitmap(dc,r.right,r.bottom);auto old=SelectObject(mem,bmp);SendMessageW(hwnd,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(mem),PRF_CLIENT);SelectObject(mem,old);
 static bool saved=false;if(!saved){saved=true;BITMAPINFO info{};info.bmiHeader={sizeof(BITMAPINFOHEADER),r.right,r.bottom,1,32,BI_RGB};std::vector<unsigned char> pixels(r.right*r.bottom*4);GetDIBits(mem,bmp,0,r.bottom,pixels.data(),&info,DIB_RGB_COLORS);BITMAPFILEHEADER header{};header.bfType=0x4d42;header.bfOffBits=sizeof(header)+sizeof(info.bmiHeader);header.bfSize=header.bfOffBits+DWORD(pixels.size());std::ofstream file(preview,std::ios::binary);file.write(reinterpret_cast<char*>(&header),sizeof(header));file.write(reinterpret_cast<char*>(&info.bmiHeader),sizeof(info.bmiHeader));file.write(reinterpret_cast<char*>(pixels.data()),pixels.size());}
 DeleteObject(bmp);DeleteDC(mem);ReleaseDC(hwnd,dc);}
int main(int argc,char** argv){CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);try{
 check(argc==2,"Missing project path");preview=std::string(argv[1])+".mixer.bmp";for(auto rates: {std::pair<int,int>{44100,48000},{48000,44100},{96000,48000},{48000,48000}})for(double drift:{-.0008,0.,.0008})conversion(rates.first,rates.second,drift);
 conversion(96000,48000,0,30000);
 initTracks();initSequencer();int id=getTracks().front().id;trackSetType(id,TrackType::AudioIn);trackSetInputDevice(id,"missing-{identity}","USB interface saved name");trackSetInputChannels(id,2,true);trackSetInputMonitor(id,true);trackSetMute(id,true);trackSetSolo(id,true);
 check(saveProjectToFile(argv[1])&&loadProjectFromFile(argv[1]),"Input recall failed");auto t=getTracks().front();id=t.id;check(t.type==TrackType::AudioIn&&t.inputDeviceId=="missing-{identity}"&&t.inputChannel==2&&t.inputStereo&&t.mute&&t.solo&&!t.inputMonitor,"Recall changed routing or enabled Monitor");
 check(!trackAudible(t,true),"Mute must win over solo");trackSetMute(id,false);t=getTracks().front();check(trackAudible(t,true),"Soloed track muted");t.solo=false;check(!trackAudible(t,true)&&trackAudible(t,false),"Solo filtering broken");
 auto source=std::make_shared<input::Buffer>();source->configure(2,48000);input::setTestSource(t.inputDeviceId,source);int second=addTrack().id;trackSetType(second,TrackType::AudioIn);trackSetInputDevice(second,t.inputDeviceId,t.inputDeviceName);trackSetInputMonitor(id,true);trackSetInputMonitor(second,true);auto shared=input::synchronize(getTracks());check(shared[0]==source&&shared[1]==source,"Same-device tracks do not share capture");trackSetInputMonitor(id,false);shared=input::synchronize(getTracks());check(!shared[0]&&shared[1]==source,"Disabling one input interrupted shared capture");input::shutdown();input::setTestSource(t.inputDeviceId,nullptr);
 std::vector<float> ramp(4000);for(int i=0;i<4000;++i)ramp[i]=float(i)/4000;source->push(ramp.data(),2000);input::Reader early,late;early.bind(source);late.bind(source);early.beginBlock(48000,256,1);for(int i=0;i<256;++i){double l,r;early.read(0,true,l,r);}early.beginBlock(48000,256,2);late.beginBlock(48000,256,2);for(int i=0;i<256;++i){double l,r,a,b;early.read(0,true,l,r);late.read(0,true,a,b);check(l==a&&r==b,"Late-joining reader not aligned to shared device clock");}

 for(int i=0;i<4;++i)addTrack();int midi=getTracks().back().id;trackSetType(midi,TrackType::MidiOut);showMixerWindow(nullptr,false);HWND hwnd=FindWindowW(L"KJMixer",nullptr);check(hwnd!=nullptr,"Mixer not created");SetWindowPos(hwnd,nullptr,0,0,540,600,SWP_NOZORDER|SWP_NOACTIVATE);draw(hwnd);
 click(hwnd,50,345);check(getTracks().front().mute,"Mixer mute did not update shared state");draw(hwnd);click(hwnd,50,345);draw(hwnd);click(hwnd,160,240);check(trackGetVolume(id)>.7&&trackGetVolume(id)<.9,"Mixer filled slider did not update volume");
 draw(hwnd);click(hwnd,250,25);check(getActiveSequencerTrackId()==second,"Mixer selection did not follow track");SendMessageW(hwnd,WM_HSCROLL,SB_RIGHT,0);SCROLLINFO si{sizeof(si),SIF_POS};GetScrollInfo(hwnd,SB_HORZ,&si);check(si.nPos>0,"Mixer did not scroll horizontally");draw(hwnd);closeMixerWindow();
 std::ofstream legacy(argv[1]);legacy<<"{\"tracks\":[{\"name\":\"Legacy\",\"type\":\"Synth\"}]}";legacy.close();check(loadProjectFromFile(argv[1]),"Legacy recall failed");for(auto tr:getTracks())check(!tr.mute&&!tr.solo&&!tr.inputMonitor&&tr.type!=TrackType::AudioIn,"Legacy defaults changed");
 std::cout<<"PASS input buffering, SRC, drift, sharing, recall, mixer controls / scrolling\n";CoUninitialize();return 0;
 }catch(const std::exception& e){closeMixerWindow();input::shutdown();std::cerr<<e.what()<<'\n';CoUninitialize();return 1;}}
