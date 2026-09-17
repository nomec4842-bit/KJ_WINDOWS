#pragma once
#include <windows.h>
enum class EditingPage { Fx, Mod, Instrument };
void createEditingPanel(HWND parent);
void layoutEditingPanel(HWND parent);
RECT sequencerContentRect(HWND parent);
void revealEditingPanel(EditingPage page, int trackId = 0);
void resetEditingPanelTracks();
bool isEditingPanelVisible(EditingPage page);
bool handleEditingPanelKeyboard(MSG* message);
class LICE_SysBitmap;
void drawEditingPanel(LICE_SysBitmap& surface);
bool handleEditingPanelMessage(HWND window, UINT message, WPARAM wp, LPARAM lp);
bool editingPanelCreated();
void focusEditingModTarget(int parameter, int track);
void revealEditingLfos(int track);
void appendLfoPreferences(HMENU menuBar);
bool handleLfoPreferenceCommand(int command);
// Existing effect controls are instantiated directly as child views.
HWND createEmbeddedEffect(HWND parent, int effect, int track);
void bindEmbeddedEffect(HWND view, int effect, int track);
