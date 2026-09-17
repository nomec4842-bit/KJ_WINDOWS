#pragma once
#include <windows.h>
#include "core/eq_spectrum.h"
struct EqGraph {
    RECT bounds{}, clip{};
    int track=0, selected=1;
    bool dragging=false;
    std::shared_ptr<spectrum::View> analyzer;
    std::array<float,spectrum::size/2+1> spectrumDb{};
    ULONGLONG spectrumTime=0,levelTime=0;
    int dragOffsetX=0, dragOffsetY=0;
};
void drawEqGraph(HDC dc, EqGraph& graph);
bool handleEqGraph(HWND owner, EqGraph& graph, UINT message, WPARAM wp, LPARAM lp);
HWND createEqGraph(HWND parent);
void setEqGraphTrack(HWND graph, int track);
