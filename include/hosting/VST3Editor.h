#pragma once
#include "pluginterfaces/base/smartpointer.h"
#include "pluginterfaces/gui/iplugview.h"
#include <memory>

namespace kj {
class VST3Editor {
public:
    VST3Editor(Steinberg::IPtr<Steinberg::IPlugView> view, void* owner, bool visible);
    ~VST3Editor();
    void show();
    bool isOpen() const;
    void* nativeWindow() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
