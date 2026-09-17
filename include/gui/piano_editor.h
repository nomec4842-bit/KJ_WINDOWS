#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
LRESULT CALLBACK PianoEditorWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

void setPianoTrimEmptyBars(bool enabled);
bool pianoTrimEmptyBars();
