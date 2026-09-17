#include "core/track_type_sample.h"
#include "core/tracks_internal.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <shared_mutex>
#include <utility>

using namespace track_internal;

float trackGetSampleAttack(int trackId)
{
    auto track = findTrackData(trackId);
    if (!track)
        return kDefaultSampleAttack;

    float value = track->sampleAttack.load(std::memory_order_relaxed);
    return std::clamp(value, kMinSampleEnvelopeTime, kMaxSampleEnvelopeTime);
}

void trackSetSampleAttack(int trackId, float value)
{
    auto track = findTrackData(trackId);
    if (!track)
        return;

    float clamped = std::clamp(value, kMinSampleEnvelopeTime, kMaxSampleEnvelopeTime);
    track->sampleAttack.store(clamped, std::memory_order_relaxed);
}

float trackGetSampleRelease(int trackId)
{
    auto track = findTrackData(trackId);
    if (!track)
        return kDefaultSampleRelease;

    float value = track->sampleRelease.load(std::memory_order_relaxed);
    return std::clamp(value, kMinSampleEnvelopeTime, kMaxSampleEnvelopeTime);
}

void trackSetSampleRelease(int trackId, float value)
{
    auto track = findTrackData(trackId);
    if (!track)
        return;

    float clamped = std::clamp(value, kMinSampleEnvelopeTime, kMaxSampleEnvelopeTime);
    track->sampleRelease.store(clamped, std::memory_order_relaxed);
}

std::shared_ptr<const SampleBuffer> trackGetSampleBuffer(int trackId)
{
    std::shared_lock<std::shared_mutex> lock(gTrackMutex);
    for (const auto& track : gTracks)
    {
        if (track->track.id == trackId)
            return track->sampleBuffer;
    }

    return {};
}

void trackSetSampleBuffer(int trackId, std::shared_ptr<const SampleBuffer> buffer)
{
    std::unique_lock<std::shared_mutex> lock(gTrackMutex);
    for (auto& track : gTracks)
    {
        if (track->track.id == trackId)
        {
            track->sampleBuffer = std::move(buffer);
            return;
        }
    }
}

namespace {
std::shared_ptr<const SampleBankBuffers> gBankBuffers = std::make_shared<const SampleBankBuffers>();
}

void sampleSetBankBuffers(std::shared_ptr<const SampleBankBuffers> buffers)
{
    std::atomic_store(&gBankBuffers, std::move(buffers));
}

std::shared_ptr<const SampleBankBuffers> sampleGetBankBuffers()
{
    return std::atomic_load(&gBankBuffers);
}

bool trackGetSampleDrumMode(int trackId)
{
    auto track = findTrackData(trackId);
    return track && track->sampleDrumMode.load(std::memory_order_relaxed);
}

void trackSetSampleDrumMode(int trackId, bool drumMode)
{
    auto track = findTrackData(trackId);
    if (track)
        track->sampleDrumMode.store(drumMode, std::memory_order_relaxed);
}

double samplePlaybackIncrement(int note, bool drumMode, int sourceRate, double outputRate, double pitchOffset)
{
    if (sourceRate <= 0 || outputRate <= 0.0) return 0.0;
    double ratio = std::pow(2.0, ((drumMode ? 0 : note - kSampleRootNote) + pitchOffset) / 12.0);
    return sourceRate / outputRate * ratio;
}

DrumSettings sampleDrumSettings(const Track& track,int lane) {
    auto found=track.drums.find(lane);
    if(found!=track.drums.end())return found->second;
    DrumSettings result;result.attack=track.sampleAttack;result.release=track.sampleRelease;return result;
}
DrumSettings trackGetDrumSettings(int id,int lane) {
    std::shared_lock<std::shared_mutex> lock(gTrackMutex);
    for(const auto& t:gTracks)if(t->track.id==id){
        auto result=sampleDrumSettings(t->track,lane);
        if(!t->track.drums.count(lane)){result.attack=t->sampleAttack.load();result.release=t->sampleRelease.load();}
        return result;
    }
    return {};
}
void trackSetDrumParameter(int id,int lane,DrumParameter parameter,float value) {
    if(lane<0||lane>65535||!std::isfinite(value))return;
    std::unique_lock<std::shared_mutex> lock(gTrackMutex);
    for(auto& t:gTracks)if(t->track.id==id){
        auto result=sampleDrumSettings(t->track,lane);
        if(!t->track.drums.count(lane)){result.attack=t->sampleAttack.load();result.release=t->sampleRelease.load();}
        switch(parameter){
        case DrumParameter::Attack:result.attack=std::clamp(value,0.f,4.f);break;
        case DrumParameter::Release:result.release=std::clamp(value,0.f,4.f);break;
        case DrumParameter::Pitch:result.pitch=std::clamp(value,-48.f,48.f);break;
        case DrumParameter::Pan:result.pan=std::clamp(value,-1.f,1.f);break;
        case DrumParameter::Volume:result.volume=std::clamp(value,0.f,1.f);break;
        }
        t->track.drums[lane]=result;return;
    }
}
int trackGetSelectedDrum(int id) {
    std::shared_lock<std::shared_mutex> lock(gTrackMutex);
    for(const auto& t:gTracks)if(t->track.id==id)return t->track.selectedDrum;
    return 0;
}
void trackSetSelectedDrum(int id,int lane) {
    if(lane<0||lane>65535)return;
    std::unique_lock<std::shared_mutex> lock(gTrackMutex);
    for(auto& t:gTracks)if(t->track.id==id){t->track.selectedDrum=lane;return;}
}
