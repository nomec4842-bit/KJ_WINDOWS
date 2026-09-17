#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>

// One audio writer, any number of display readers. A slow reader pins its
// buffer; the writer drops a display update rather than waiting or allocating.
template<std::size_t Capacity> class WaveformSnapshot {
    struct Slot {
        mutable std::atomic<int> readers{0}; // -1 means the producer owns it
        std::array<float,Capacity> samples{};
        std::size_t count=0;
    };
    std::array<Slot,3> slots;
    std::atomic<int> published{-1};
public:
    bool publish(const float* samples,std::size_t count) {
        if(!samples&&count)return false;
        int current=published.load(std::memory_order_acquire);
        for(int i=0;i<3;++i){
            if(i==current)continue;
            auto& slot=slots[i];int expected=0;
            if(!slot.readers.compare_exchange_strong(expected,-1,std::memory_order_acq_rel))continue;
            slot.count=std::min(count,Capacity);
            if(slot.count)std::copy_n(samples,slot.count,slot.samples.begin());
            slot.readers.store(0,std::memory_order_release);
            published.store(i,std::memory_order_release);
            return true;
        }
        return false;
    }
    template<class Read> bool read(Read consume) const {
        for(int attempt=0;attempt<3;++attempt){
            int index=published.load(std::memory_order_acquire);
            if(index<0)return false;
            const auto& slot=slots[index];
            int readers=slot.readers.load(std::memory_order_acquire);
            if(readers<0||!slot.readers.compare_exchange_strong(readers,readers+1,std::memory_order_acq_rel))continue;
            struct Release { const Slot& slot; ~Release(){slot.readers.fetch_sub(1,std::memory_order_release);} } release{slot};
            if(index!=published.load(std::memory_order_acquire))continue;
            consume(slot.samples.data(),slot.count);
            return true;
        }
        return false;
    }
};
