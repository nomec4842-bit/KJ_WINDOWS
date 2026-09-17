#include "core/audio_meter.h"
#include <map>
#include <mutex>
#include <set>
#include <cmath>
#include <algorithm>
namespace meter {
namespace { std::mutex mutex;std::map<int,std::shared_ptr<Level>> levels;Level output; }
void Level::publish(double l,double r,double seconds){if(l>=1||r>=1)clipped=true;float decay=static_cast<float>(std::exp(-seconds/.25));left.store(std::max(float(l),left.load()*decay));right.store(std::max(float(r),right.load()*decay));}
std::vector<std::shared_ptr<Level>> synchronize(const std::vector<Track>& tracks){std::lock_guard<std::mutex> lock(mutex);std::set<int> ids;std::vector<std::shared_ptr<Level>> result;for(const auto& t:tracks){ids.insert(t.id);auto& p=levels[t.id];if(!p)p=std::make_shared<Level>();result.push_back(p);}for(auto it=levels.begin();it!=levels.end();)if(!ids.count(it->first))it=levels.erase(it);else ++it;return result;}
std::shared_ptr<Level> track(int id){std::lock_guard<std::mutex> lock(mutex);auto it=levels.find(id);return it==levels.end()?nullptr:it->second;}
Level& master(){return output;}
}
