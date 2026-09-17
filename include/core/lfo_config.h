#pragma once
#include <algorithm>
#include <atomic>
#include <string>
inline constexpr int kMaxLfos=20;
inline constexpr int kDefaultLfoCount=3;
inline constexpr int kLfoSourceCount=kMaxLfos+3;
inline std::atomic<int> gLfoCount{kDefaultLfoCount};
inline int lfoCount(){return gLfoCount.load(std::memory_order_relaxed);}
inline void setLfoCount(int count){gLfoCount.store(std::clamp(count,1,kMaxLfos),std::memory_order_relaxed);}
// IDs 3, 4 and 5 remain Envelope/Macro 1/Macro 2 for existing projects.
inline constexpr int lfoSourceId(int index){return index<3?index:index+3;}
inline constexpr int sourceLfoIndex(int source){return source<0?-1:source<3?source:source>=6&&source<kLfoSourceCount?source-3:-1;}
inline std::string modulationSourceLabel(int source){int i=sourceLfoIndex(source);if(i>=0)return "LFO "+std::to_string(i+1)+(i>=lfoCount()?" (disabled)":"");return source==3?"Envelope 1":source==4?"Macro 1":source==5?"Macro 2":"Unknown";}
