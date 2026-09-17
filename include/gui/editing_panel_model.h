#pragma once
#include <algorithm>
namespace editing {
struct Binding {
    int track=0;
    bool pinned=false;
    void follow(int selected, bool exists) { if(!exists) pinned=false; if(!pinned) track=selected; }
    void reset() { track=0; pinned=false; }
};
inline bool splitFits(bool requested, int width, int dpi) {
    return requested && width >= (840+6)*dpi/96;
}
inline int panelHeight(int height) {
    // Sequencer steps end at client y=295, independently of DPI. Keep a
    // nine-pixel gap and give the editor all remaining window height.
    return std::max(0, height - 304);
}
}
