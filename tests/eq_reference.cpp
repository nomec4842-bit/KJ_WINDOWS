#include "hosting/VST3Host.h"
#include "core/eq_processor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "public.sdk/source/vst/hosting/processdata.h"
#include <windows.h>
#include <objbase.h>
#include <iostream>
#include <map>
#include <cmath>
#include <thread>
#include <atomic>
#include <iomanip>
#include <fstream>
using namespace Steinberg::Vst;
int main(int argc,char** argv){
 if(argc<3)return 1;
 std::ofstream results(argv[2]);results<<std::unitbuf;
 CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);std::cout<<std::unitbuf;
 try{
  kj::VST3Host host;auto classes=kj::VST3Host::scan(argv[1]);host.load(argv[1],classes.front().ID().toString());
  auto c=host.controller();std::map<std::wstring,ParamID> ids;
  for(int i=0;i<c->getParameterCount();++i){ParameterInfo info{};c->getParameterInfo(i,info);ids[reinterpret_cast<wchar_t*>(info.title)]=info.id;}
  auto normalized=[&](const std::wstring& name,double v){host.setParameter(ids.at(name),v);};
  auto physical=[&](const std::wstring& name,const wchar_t* text){
   String128 input{};wcscpy_s(reinterpret_cast<wchar_t*>(input),128,text);double v=0;
   if(c->getParamValueByString(ids.at(name),input,v)!=Steinberg::kResultOk)throw std::runtime_error("Physical value conversion failed");
   host.setParameter(ids.at(name),v);String128 shown{};c->getParamStringByValue(ids.at(name),v,shown);
   std::wcout<<name<<L" = "<<reinterpret_cast<wchar_t*>(shown)<<L" (normalized "<<v<<L")\n";
  };
  normalized(L"Auto Gain",0);normalized(L"Adaptive Q",0);normalized(L"Oversampling",0);normalized(L"Linear Phase",0);normalized(L"Intent Mode",0);normalized(L"Processing Mode",0);
  physical(L"Output Gain",L"0");physical(L"Scale",L"1");
  for(int band=1;band<=8;++band){auto prefix=L"Band "+std::to_wstring(band);normalized(prefix+L" On",band==1?1:0);normalized(prefix+L" Solo",0);normalized(prefix+L" Dyn On",0);normalized(prefix+L" Drive",0);}
  normalized(L"Band 1 Channel",0);normalized(L"Band 1 Link",0);normalized(L"Band 1 Slope",0);
  for(double rate:{44100.,48000.}){
   host.prepare(rate,256);
   for(int shape:{0,1})for(double gain:{-12.,0.,8.654,12.}){
    Track track;track.eqEnabled=true;track.eqShape[0]=shape;track.eqFrequency[0]=shape?200.f:66.376f;track.eqQ[0]=.707f;track.lowGainDb=float(gain);
    normalized(L"Band 1 Type",shape/5.);physical(L"Band 1 Freq",shape?L"200":L"66.376");physical(L"Band 1 Q",L"0.707");physical(L"Band 1 Gain",std::to_wstring(gain).c_str());
    std::exception_ptr error;std::atomic<bool> done{false};
    std::thread audio([&]{try{
     HostProcessData data;if(!data.prepare(*host.component(),256,kSample32))throw std::runtime_error("Buffer allocation failed");
     data.processMode=kRealtime;host.startProcessing();
     for(double hz:{20.,40.,55.,60.,66.376,100.,200.,2000.}){
      eq::Processor native;native.configure(track,rate);double input=0,plugin[2]{},kjPower[2]{};
      for(int offset=0;offset<int(rate*2);offset+=256){
       data.numSamples=std::min(256,int(rate*2)-offset);
       for(int bus=0;bus<data.numInputs;++bus){data.inputs[bus].silenceFlags=0;for(int ch=0;ch<data.inputs[bus].numChannels;++ch)for(int i=0;i<data.numSamples;++i)data.inputs[bus].channelBuffers32[ch][i]=bus==0?float(.02*std::sin(6.283185307179586*hz*(offset+i)/rate)*(ch==0?1:-.37)):0;}
       for(int bus=0;bus<data.numOutputs;++bus){data.outputs[bus].silenceFlags=0;for(int ch=0;ch<data.outputs[bus].numChannels;++ch)std::fill_n(data.outputs[bus].channelBuffers32[ch],data.numSamples,0.f);}
       host.process(data);
       for(int i=0;i<data.numSamples;++i){double x=.02*std::sin(6.283185307179586*hz*(offset+i)/rate),l=x,r=-.37*x;native.process(l,r);
        if(offset+i>=int(rate)){input+=x*x;kjPower[0]+=l*l;kjPower[1]+=r*r;for(int ch=0;ch<2;++ch){double y=data.outputs[0].channelBuffers32[ch][i];plugin[ch]+=y*y;}}
       }
      }
      for(int ch=0;ch<2;++ch){double base=input*(ch==0?1:.37*.37);double a=10*std::log10(kjPower[ch]/base),b=10*std::log10(plugin[ch]/base);
       results<<"RESULT,"<<rate<<","<<shape<<","<<gain<<","<<hz<<","<<ch<<","<<a<<","<<b<<","<<b-a<<"\n";}
     }
     host.stopProcessing();data.unprepare();
    }catch(...){error=std::current_exception();}done=true;});
    while(!done){MSG m;while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}Sleep(1);}audio.join();if(error)std::rethrow_exception(error);
   }
  }
  results<<"Comparison complete\n";
 }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
 CoUninitialize();return 0;
}
