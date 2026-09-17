#pragma once
#include <windows.h>
#include <string>
struct Track;
void showMixerWindow(HWND owner,bool visible=true);
void closeMixerWindow();
void showAudioInputSettings(HWND owner,int trackId);
std::string audioInputDescription(const Track& track);
