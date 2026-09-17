#pragma once
#include <algorithm>
#include <cmath>
#include <complex>

namespace eq {
enum class Shape { Bell, LowShelf, HighShelf, LowPass, HighPass, Notch, Off };
inline constexpr const char* names[] = {"Bell", "Low shelf", "High shelf", "High cut (12 dB/oct)", "Low cut (12 dB/oct)", "Notch", "Off"};
struct Coefficients { double b0=1,b1=0,b2=0,a1=0,a2=0; };
inline bool hasGain(Shape shape) { return shape==Shape::Bell||shape==Shape::LowShelf||shape==Shape::HighShelf; }
inline Coefficients coefficients(Shape shape,double rate,double frequency,double gain,double q) {
    if(shape==Shape::Off||!std::isfinite(rate)||!std::isfinite(frequency)||!std::isfinite(gain)||!std::isfinite(q))return {};
    rate=std::max(100.0,rate);
    double w=6.283185307179586*std::clamp(frequency,10.0,rate*.499)/rate;
    double c=std::cos(w),alpha=std::sin(w)/(2*std::clamp(q,.1,10.0));
    double a=std::pow(10.0,std::clamp(gain,-12.0,12.0)/40),root=2*std::sqrt(a)*alpha;
    double b0=1,b1=0,b2=0,a0=1,a1=0,a2=0;
    switch(shape){
    case Shape::Bell:b0=1+alpha*a;b1=-2*c;b2=1-alpha*a;a0=1+alpha/a;a1=-2*c;a2=1-alpha/a;break;
    case Shape::LowShelf:
        b0=a*((a+1)-(a-1)*c+root);b1=2*a*((a-1)-(a+1)*c);b2=a*((a+1)-(a-1)*c-root);
        a0=(a+1)+(a-1)*c+root;a1=-2*((a-1)+(a+1)*c);a2=(a+1)+(a-1)*c-root;break;
    case Shape::HighShelf:
        b0=a*((a+1)+(a-1)*c+root);b1=-2*a*((a-1)+(a+1)*c);b2=a*((a+1)+(a-1)*c-root);
        a0=(a+1)-(a-1)*c+root;a1=2*((a-1)-(a+1)*c);a2=(a+1)-(a-1)*c-root;break;
    case Shape::LowPass:b0=(1-c)/2;b1=1-c;b2=b0;a0=1+alpha;a1=-2*c;a2=1-alpha;break;
    case Shape::HighPass:b0=(1+c)/2;b1=-(1+c);b2=b0;a0=1+alpha;a1=-2*c;a2=1-alpha;break;
    case Shape::Notch:b0=1;b1=-2*c;b2=1;a0=1+alpha;a1=-2*c;a2=1-alpha;break;
    default:return {};
    }
    return {b0/a0,b1/a0,b2/a0,a1/a0,a2/a0};
}
inline double responseDb(const Coefficients& c,double rate,double frequency){
    auto z=std::polar(1.0,-6.283185307179586*frequency/rate);
    double magnitude=std::abs((c.b0+c.b1*z+c.b2*z*z)/(1.0+c.a1*z+c.a2*z*z));
    return 20*std::log10(std::max(1e-12,magnitude));
}
}
