#pragma once
#include <windows.h>
#include <string>

enum class Vst3SlotAction { Editor, Up, Down, Bypass, Remove, Locate };
void handleVst3SlotAction(HWND owner, int trackId, const std::string& slotId, Vst3SlotAction action);
enum { kMenuVst3Instrument = 31001, kMenuVst3Effect,
       kMenuVst3UnloadInstrument, kMenuVst3UnloadEffect, kMenuVst3EditInstrument, kMenuVst3EditEffect, kMenuVst3Rack, kMenuVst3Refresh };
void appendVst3Menu(HMENU menu);
void handleVst3Menu(HWND owner, int command, int trackId);
void serviceVst3MainThread(HWND owner);
