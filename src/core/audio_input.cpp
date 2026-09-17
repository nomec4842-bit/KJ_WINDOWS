#include "core/audio_input.h"
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#include <ksmedia.h>
#include <wrl/client.h>
#include <thread>
#include <mutex>
#include <map>
#include <set>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <array>
using Microsoft::WRL::ComPtr;
namespace input {
namespace {
// Lookup tables are initialized when capture is created, outside playback. A
// 64-tap windowed-sinc interpolator suppresses aliasing when rates differ.
struct Kernel {
    std::array<double,4097> sinc{},window{};
    Kernel(){for(size_t i=0;i<sinc.size();++i){double x=i/128.;sinc[i]=i?std::sin(3.141592653589793*x)/(3.141592653589793*x):1;window[i]=.5+.5*std::cos(3.141592653589793*x/32.);}}
    double at(const std::array<double,4097>& table,double x)const{double position=std::min(std::abs(x)*128.,4096.);int index=std::min(int(position),4095);double fraction=position-index;return table[index]*(1-fraction)+table[index+1]*fraction;}
};
const Kernel& kernel(){static const Kernel value;return value;}
}
Buffer::Buffer():data(new std::atomic<float>[capacity*maxChannels]{}){(void)kernel();}
void Buffer::configure(int count,int hz){available=false;channels=count;rate=hz;written=0;generation.fetch_add(1);available=count>0&&count<=maxChannels&&hz>0;}
void Buffer::push(const float* samples,int frames){int count=channels.load();if(!available||count<1||count>maxChannels)return;auto n=written.load();for(int i=0;i<frames;++i){for(int ch=0;ch<count;++ch){float v=samples?samples[i*count+ch]:0;data[((n+i)%capacity)*maxChannels+ch].store(std::isfinite(v)?v:0,std::memory_order_relaxed);}}written.store(n+frames,std::memory_order_release);}
float Buffer::sample(uint64_t frame,int channel)const{return data[(frame%capacity)*maxChannels+channel].load(std::memory_order_relaxed);}
bool Reader::bind(std::shared_ptr<Buffer> buffer){if(buffer==source)return false;source=std::move(buffer);position=-1;generation=0;return true;}
void Reader::beginBlock(double outputRate,int frames,uint64_t block){
    if(!source||!source->available||outputRate<=0){position=-1;return;}
    cutoff=.96*std::min(1.,outputRate/source->rate.load());
    if(frames>0){
        if(source->renderBlock==block&&source->renderRate==outputRate){position=source->renderPosition;increment=source->renderIncrement;generation=source->renderGeneration;return;}
        position=source->renderRate==outputRate?source->renderNext:-1;generation=source->renderGeneration;increment=source->renderIncrement;
    }
    auto g=source->generation.load();if(g!=generation){generation=g;position=-1;}
    bool priming=position<0;
    double n=double(source->written.load(std::memory_order_acquire));double target=std::clamp(source->rate.load()*.03,128.,capacity/4.);
    if(position<0||position>n-33||n-position>capacity-34){position=n>=target+2?n-target:-1;}
    // Correct independent hardware clocks smoothly around a 30 ms capture cushion.
    double correction=std::clamp((n-position-target)/target*.01,-.005,.005);
    double nominal=source->rate.load()/outputRate;
    if(priming)increment=nominal;else increment+=(nominal*(1+correction)-increment)*.05;
    if(frames>0){source->renderBlock=block;source->renderRate=outputRate;source->renderPosition=position;source->renderNext=position<0?-1:position+frames*increment;source->renderIncrement=increment;source->renderGeneration=generation;}
}
bool Reader::ready(int first,bool stereo)const{return source&&source->available&&position>=0&&source->generation.load()==generation&&first>=0&&first+(stereo?1:0)<source->channels.load();}
void Reader::read(int first,bool stereo,double& l,double& r){
    l=r=0;if(!source||!source->available||position<0)return;
    int channels=source->channels.load();if(first<0||first+(stereo?1:0)>=channels)return;
    auto n=source->written.load(std::memory_order_acquire);auto frame=static_cast<uint64_t>(position);
    if(frame+33>=n||frame<31||n-frame>=capacity-34||source->generation.load()!=generation){position=-1;return;}
    double fraction=position-frame;
    double sum=0;const auto& weights=kernel();
    for(int tap=-31;tap<=32;++tap){double distance=tap-fraction;double weight=weights.at(weights.sinc,distance*cutoff)*weights.at(weights.window,distance);sum+=weight;auto index=static_cast<uint64_t>(static_cast<int64_t>(frame)+tap);l+=source->sample(index,first)*weight;if(stereo)r+=source->sample(index,first+1)*weight;}
    if(std::abs(sum)>1e-12){l/=sum;r=stereo?r/sum:l;}else l=r=0;
    position+=increment;
}
namespace {
std::atomic<bool> stoppedMonitoring{true};
std::wstring wide(const std::string& s){int n=MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),nullptr,0);std::wstring w(n,0);MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),w.data(),n);return w;}
std::string narrow(const wchar_t* w){int n=WideCharToMultiByte(CP_UTF8,0,w,-1,nullptr,0,nullptr,nullptr);std::string s(n,0);WideCharToMultiByte(CP_UTF8,0,w,-1,s.data(),n,nullptr,nullptr);if(!s.empty())s.pop_back();return s;}
struct Stream {
    std::shared_ptr<Buffer> buffer=std::make_shared<Buffer>();
    std::atomic<bool> stop{false};std::mutex messageMutex;std::string message="Opening input...";std::thread worker;
    void state(const std::string& text){std::lock_guard<std::mutex> lock(messageMutex);message=text;}
    explicit Stream(std::string id):worker([this,id]{run(id);}){}
    ~Stream(){stop=true;if(worker.joinable())worker.join();}
    void run(const std::string& id){
        HRESULT com=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
        if(FAILED(com)){state("Input COM initialization failed");return;}
        while(!stop){
            ComPtr<IMMDeviceEnumerator> enumerator;ComPtr<IMMDevice> device;ComPtr<IAudioClient> client;ComPtr<IAudioCaptureClient> capture;
            WAVEFORMATEX* format=nullptr;
            HRESULT hr=CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,IID_PPV_ARGS(&enumerator));
            if(SUCCEEDED(hr))hr=enumerator->GetDevice(wide(id).c_str(),&device);
            if(SUCCEEDED(hr))hr=device->Activate(__uuidof(IAudioClient),CLSCTX_ALL,nullptr,reinterpret_cast<void**>(client.GetAddressOf()));
            if(SUCCEEDED(hr))hr=client->GetMixFormat(&format);
            bool floating=false,pcm=false;
            if(SUCCEEDED(hr)){
                floating=format->wFormatTag==WAVE_FORMAT_IEEE_FLOAT;pcm=format->wFormatTag==WAVE_FORMAT_PCM;
                if(format->wFormatTag==WAVE_FORMAT_EXTENSIBLE&&format->cbSize>=22){auto ext=reinterpret_cast<WAVEFORMATEXTENSIBLE*>(format);floating=ext->SubFormat==KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;pcm=ext->SubFormat==KSDATAFORMAT_SUBTYPE_PCM;}
                if(format->nSamplesPerSec==0||format->nBlockAlign!=format->nChannels*(format->wBitsPerSample/8)||format->nChannels<1||format->nChannels>maxChannels||!((floating&&format->wBitsPerSample==32)||(pcm&&(format->wBitsPerSample==16||format->wBitsPerSample==24||format->wBitsPerSample==32))))hr=AUDCLNT_E_UNSUPPORTED_FORMAT;
            }
            if(SUCCEEDED(hr))hr=client->Initialize(AUDCLNT_SHAREMODE_SHARED,0,1000000,0,format,nullptr);
            if(SUCCEEDED(hr))hr=client->GetService(IID_PPV_ARGS(&capture));
            UINT32 frames=0;if(SUCCEEDED(hr))hr=client->GetBufferSize(&frames);
            std::vector<float> converted;
            if(SUCCEEDED(hr)){converted.resize(size_t(frames)*format->nChannels);hr=client->Start();}
            if(SUCCEEDED(hr)){
                buffer->configure(format->nChannels,format->nSamplesPerSec);state("Ready");
                while(!stop&&SUCCEEDED(hr)){
                    UINT32 packet=0;hr=capture->GetNextPacketSize(&packet);
                    while(SUCCEEDED(hr)&&packet){
                        BYTE* data=nullptr;DWORD flags=0;UINT32 count=0;
                        hr=capture->GetBuffer(&data,&count,&flags,nullptr,nullptr);if(FAILED(hr))break;
                        if(flags&AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY)buffer->generation.fetch_add(1);
                        if(count<=frames){
                            if(flags&AUDCLNT_BUFFERFLAGS_SILENT)buffer->push(nullptr,count);
                            else{
                                int bytes=format->wBitsPerSample/8;
                                for(size_t i=0;i<size_t(count)*format->nChannels;++i){auto p=data+i*bytes;float value=0;
                                    if(floating)std::memcpy(&value,p,4);
                                    else if(bytes==2){int16_t v;std::memcpy(&v,p,2);value=v/32768.f;}
                                    else if(bytes==3){int32_t v=p[0]|(p[1]<<8)|(p[2]<<16);if(v&0x800000)v|=~0xffffff;value=v/8388608.f;}
                                    else {int32_t v;std::memcpy(&v,p,4);value=float(v/2147483648.);}
                                    converted[i]=value;
                                }
                                buffer->push(converted.data(),count);
                            }
                        }
                        capture->ReleaseBuffer(count);hr=capture->GetNextPacketSize(&packet);
                    }
                    Sleep(2);
                }
                client->Stop();
            }
            buffer->available=false;
            if(format)CoTaskMemFree(format);
            if(!stop){state(hr==AUDCLNT_E_UNSUPPORTED_FORMAT?"Unsupported input format":"Input unavailable (check device and microphone access)");for(int i=0;i<50&&!stop;++i)Sleep(10);}
        }
        CoUninitialize();
    }
};
#ifdef KJ_BUILD_TESTS
std::map<std::string,std::shared_ptr<Buffer>> testSources;
#endif
std::mutex streamsMutex;std::map<std::string,std::shared_ptr<Stream>> streams;
// Retain buffers until all playback snapshots/readers release them, then free
// their storage on the cache thread rather than inside the audio callback.
std::vector<std::shared_ptr<Buffer>> retiredBuffers;
}
void setMonitorWhileStopped(bool enabled){stoppedMonitoring=enabled;}
bool monitorWhileStopped(){return stoppedMonitoring.load();}
std::vector<Device> devices(){
    std::vector<Device> result;ComPtr<IMMDeviceEnumerator> e;ComPtr<IMMDeviceCollection> collection;
    if(FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,IID_PPV_ARGS(&e)))||FAILED(e->EnumAudioEndpoints(eCapture,DEVICE_STATE_ACTIVE,&collection)))return result;
    UINT count=0;collection->GetCount(&count);
    for(UINT i=0;i<count;++i){ComPtr<IMMDevice> device;ComPtr<IPropertyStore> props;LPWSTR id=nullptr;
        if(FAILED(collection->Item(i,&device))||FAILED(device->GetId(&id)))continue;
        Device info;info.id=narrow(id);CoTaskMemFree(id);info.name="Audio input";
        if(SUCCEEDED(device->OpenPropertyStore(STGM_READ,&props))){PROPVARIANT v;PropVariantInit(&v);if(SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName,&v))&&v.vt==VT_LPWSTR)info.name=narrow(v.pwszVal);PropVariantClear(&v);}
        ComPtr<IAudioClient> client;WAVEFORMATEX* format=nullptr;
        if(SUCCEEDED(device->Activate(__uuidof(IAudioClient),CLSCTX_ALL,nullptr,reinterpret_cast<void**>(client.GetAddressOf())))&&SUCCEEDED(client->GetMixFormat(&format))){info.channels=format->nChannels;CoTaskMemFree(format);}
        result.push_back(info);
    }
    return result;
}
std::vector<std::shared_ptr<Buffer>> synchronize(const std::vector<Track>& tracks){
    std::vector<std::shared_ptr<Buffer>> result(tracks.size());std::vector<std::shared_ptr<Stream>> retired;
    {std::lock_guard<std::mutex> lock(streamsMutex);std::set<std::string> used;
    retiredBuffers.erase(std::remove_if(retiredBuffers.begin(),retiredBuffers.end(),[](const std::shared_ptr<Buffer>& b){return b.use_count()==1;}),retiredBuffers.end());
    for(size_t i=0;i<tracks.size();++i){const auto& t=tracks[i];if(t.type!=TrackType::AudioIn||!t.inputMonitor||t.inputDeviceId.empty())continue;
#ifdef KJ_BUILD_TESTS
        auto fake=testSources.find(t.inputDeviceId);if(fake!=testSources.end()){result[i]=fake->second;continue;}
#endif
        used.insert(t.inputDeviceId);auto& stream=streams[t.inputDeviceId];if(!stream)stream=std::make_shared<Stream>(t.inputDeviceId);result[i]=stream->buffer;}
    for(auto it=streams.begin();it!=streams.end();)if(!used.count(it->first)){retiredBuffers.push_back(it->second->buffer);retired.push_back(it->second);it=streams.erase(it);}else ++it;}
    return result; // Retired workers join here, on the cache thread. Audio owns only the buffer.
}
#ifdef KJ_BUILD_TESTS
void setTestSource(const std::string& id,std::shared_ptr<Buffer> source){std::lock_guard<std::mutex> lock(streamsMutex);if(source)testSources[id]=std::move(source);else testSources.erase(id);}
#endif
void shutdown(){std::map<std::string,std::shared_ptr<Stream>> retired;{std::lock_guard<std::mutex> lock(streamsMutex);retired.swap(streams);}}
std::string status(const std::string& id){std::lock_guard<std::mutex> lock(streamsMutex);auto it=streams.find(id);if(it==streams.end())return id.empty()?"Select an input device":"Monitor off";std::lock_guard<std::mutex> messageLock(it->second->messageMutex);return it->second->message;}
}
