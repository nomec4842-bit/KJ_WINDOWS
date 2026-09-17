#pragma once
#include <memory>
#include <vector>

// One producer publishes complete snapshots; readers retain ownership for the
// entire audio block. The producer also retains retired buffers, so recycling
// and destruction happen off the audio thread.
template<class T> class PlaybackSnapshot {
    std::vector<std::shared_ptr<T>> buffers;
    std::shared_ptr<const T> current;
public:
    template<class Fill> void publish(Fill fill) {
        std::shared_ptr<T> staging;
        for (auto& buffer : buffers) {
            if (buffer.use_count() == 1) { staging = buffer; break; }
        }
        if (!staging) {
            staging = std::make_shared<T>();
            buffers.push_back(staging);
        }
        fill(*staging);
        std::atomic_store(&current, std::shared_ptr<const T>(staging));
    }
    std::shared_ptr<const T> read() const { return std::atomic_load(&current); }
};
