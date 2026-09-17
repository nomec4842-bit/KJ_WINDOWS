#include "core/eq_spectrum.h"
#include <atomic>
#include <complex>
#include <cmath>
#include <algorithm>
namespace spectrum {
namespace {
struct Channel {
    std::atomic<int> track{0};
    int viewers=0; // GUI thread only
    std::array<std::atomic<float>,size> left{},right{},inputPower{};
    std::atomic<std::uint64_t> count{0};
};
std::array<Channel,8> channels;
void fft(std::array<std::complex<double>,size>& a){
    for(int i=1,j=0;i<size;++i){int bit=size>>1;for(;j&bit;bit>>=1)j^=bit;j^=bit;if(i<j)std::swap(a[i],a[j]);}
    for(int len=2;len<=size;len<<=1){auto step=std::polar(1.,-6.283185307179586/len);for(int i=0;i<size;i+=len){std::complex<double> w=1;for(int j=0;j<len/2;++j){auto u=a[i+j],v=a[i+j+len/2]*w;a[i+j]=u+v;a[i+j+len/2]=u-v;w*=step;}}}
}
}
View::View(int slot,int track,std::uint64_t start):track(track),slot(slot),start(start),last(start){}
View::~View(){if(--channels[slot].viewers==0)channels[slot].track.store(0,std::memory_order_release);}
std::shared_ptr<View> watch(int track){
    if(track<=0)return {};
    for(int i=0;i<8;++i)if(channels[i].track.load()==track){++channels[i].viewers;return std::make_shared<View>(i,track,channels[i].count.load());}
    for(int i=0;i<8;++i)if(channels[i].viewers==0){auto& c=channels[i];++c.viewers;auto result=std::make_shared<View>(i,track,c.count.load());c.track.store(track,std::memory_order_release);return result;}
    return {};
}
void capture(int track,float left,float right) noexcept { capture(track,left,right,left,right); }
void capture(int track,float left,float right,float inputLeft,float inputRight) noexcept {
    for(auto& c:channels)if(c.track.load(std::memory_order_acquire)==track){
        auto n=c.count.load(std::memory_order_relaxed);auto index=n%size;
        c.left[index].store(std::isfinite(left)?left:0,std::memory_order_relaxed);
        c.right[index].store(std::isfinite(right)?right:0,std::memory_order_relaxed);
        double il=std::isfinite(inputLeft)?inputLeft:0,ir=std::isfinite(inputRight)?inputRight:0;
        c.inputPower[index].store(static_cast<float>((il*il+ir*ir)*.5),std::memory_order_relaxed);
        c.count.store(n+1,std::memory_order_release);
    }
}
bool View::read(std::array<float,size/2+1>& db){
    auto& c=channels[slot];auto n=c.count.load(std::memory_order_acquire);
    if(n==last||n-start<size)return false;
    std::array<std::complex<double>,size> l{},r{};
    double inPower=0,outPower=0;
    for(int i=0;i<size;++i){
        auto index=(n-size+i)%size;
        double left=c.left[index].load(std::memory_order_relaxed),right=c.right[index].load(std::memory_order_relaxed);
        inPower+=c.inputPower[index].load(std::memory_order_relaxed);
        outPower+=(left*left+right*right)*.5;
        double w=.5-.5*std::cos(6.283185307179586*i/(size-1));l[i]=left*w;r[i]=right*w;
    }
    // Avoid presenting a frame overwritten during a delayed UI read.
    if(c.count.load(std::memory_order_acquire)-n>=size)return false;
    inputDb=static_cast<float>(10*std::log10(std::max(inPower/size,1e-12)));
    outputDb=static_cast<float>(10*std::log10(std::max(outPower/size,1e-12)));
    hasInput=inPower/size>1e-12;
    fft(l);fft(r);last=n;
    for(int i=0;i<=size/2;++i){double power=(std::norm(l[i])+std::norm(r[i]))*.5;double magnitude=std::sqrt(power)*4/(size-1);db[i]=static_cast<float>(20*std::log10(std::max(magnitude,1e-6)));}
    return true;
}
}
