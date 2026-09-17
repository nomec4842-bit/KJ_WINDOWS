#include "gui/mod_matrix_window.h"
#include "gui/editing_panel.h"

#include "core/mod_matrix.h"
#include "core/sequencer.h"
#include "core/tracks.h"
#include "gui/gui_main.h"
#include "gui/lfo_window.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef _WIN32_IE
#define _WIN32_IE 0x0501
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0501
#endif
#include <windows.h>
#include <commctrl.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cwchar>
#include <cwctype>
#include <iomanip>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace
{

constexpr wchar_t kModMatrixWindowClassName[] = L"KJModMatrixWindow";
constexpr int kModMatrixWindowWidth = 720;
constexpr int kModMatrixWindowHeight = 580;

constexpr UINT WM_MOD_MATRIX_REFRESH_TRACKS = WM_APP + 120;
constexpr UINT WM_MOD_MATRIX_REFRESH_VALUES = WM_APP + 121;

constexpr int kListViewId = 2001;
constexpr int kAddButtonId = 2002;
constexpr int kRemoveButtonId = 2003;
constexpr int kSourceComboId = 2004;
constexpr int kParameterComboId = 2005;
constexpr int kAmountLabelId = 2006;
constexpr int kAmountSliderId = 2007;
constexpr int kAmountEditId = 2008;
constexpr int kLfoButtonId = 2009;
constexpr int kTrackComboId = 2010;
constexpr int kResetAmountId = 2011;

constexpr int kComboDropdownHeight = 200;

HMENU makeControlId(int id)
{
    return reinterpret_cast<HMENU>(static_cast<intptr_t>(id));
}

constexpr int kSliderResolution = 1000;

constexpr std::array<const wchar_t*, 6> kModSources = {
    L"LFO 1",
    L"LFO 2",
    L"LFO 3",
    L"Envelope 1",
    L"Macro 1",
    L"Macro 2"
};


HWND gModMatrixWindow = nullptr;
bool gModMatrixWindowClassRegistered = false;

struct ModMatrixWindowState
{
    HWND listView = nullptr;
    HWND trackCombo = nullptr;
    HWND sourceLabel = nullptr;
    HWND trackLabel = nullptr;
    HWND parameterLabel = nullptr;
    HWND editorLabel = nullptr;
    HWND hintLabel = nullptr;
    HWND resetAmountButton = nullptr;
    bool rebuildingList = false;
    HWND addButton = nullptr;
    HWND removeButton = nullptr;
    HWND sourceCombo = nullptr;
    HWND parameterCombo = nullptr;
    HWND amountLabel = nullptr;
    HWND amountSlider = nullptr;
    HWND amountEdit = nullptr;
    HWND lfoButton = nullptr;
    int selectedAssignmentId = 0;
    int boundTrack = 0;
    bool embedded = false;
};

INITCOMMONCONTROLSEX gModMatrixInitControls = {};

std::wstring toWide(const std::string& text)
{
    return std::wstring(text.begin(), text.end());
}

void repopulateAssignmentList(ModMatrixWindowState* state);
void addAssignment(ModMatrixWindowState* state);

bool trackExists(int trackId)
{
    if (trackId <= 0)
        return false;
    auto tracks = getTracks();
    return std::any_of(tracks.begin(), tracks.end(), [trackId](const Track& track) { return track.id == trackId; });
}

std::optional<TrackType> getTrackTypeForTrack(int trackId)
{
    if (!trackExists(trackId))
        return std::nullopt;
    return trackGetType(trackId);
}

std::wstring getSourceLabel(int index)
{
    if (index < 0 || index >= static_cast<int>(kModSources.size()))
        return L"Unknown";
    return kModSources[static_cast<size_t>(index)];
}

std::wstring getTrackLabel(int trackId)
{
    if (trackId <= 0)
        return L"None";
    auto tracks = getTracks();
    for (const auto& track : tracks)
    {
        if (track.id == trackId)
        {
            std::wstring name = toWide(track.name);
            if (name.empty())
            {
                std::wstringstream ss;
                ss << L"Track " << track.id;
                return ss.str();
            }
            return name;
        }
    }
    std::wstringstream ss;
    ss << L"Missing (" << trackId << L")";
    return ss.str();
}

std::wstring formatAmountText(const ModMatrixAssignment& assignment)
{
    const ModParameterInfo* info = modMatrixGetParameterInfo(assignment.parameterIndex);
    if (!info)
        return L"-";

    float value = modMatrixNormalizedToValue(assignment.normalizedAmount, *info);
    float percentage = modMatrixClampNormalized(assignment.normalizedAmount) * 100.0f;

    std::wstringstream ss;
    ss << std::showpos << std::fixed << std::setprecision(2) << value;
    ss << L" (" << std::setprecision(0) << percentage << L"%)";
    ss << std::noshowpos;
    return ss.str();
}

void syncAssignmentFromTrack(ModMatrixAssignment& assignment)
{
    if (assignment.trackId <= 0 || !trackExists(assignment.trackId))
        return;

    assignment.normalizedAmount = 0.0f;
}

void addAssignment(ModMatrixWindowState* state)
{
    ModMatrixAssignment assignment = modMatrixCreateAssignment();
    assignment.sourceIndex = 0;
    assignment.parameterIndex = 0;
    assignment.trackId = state && state->embedded ? state->boundTrack : getActiveSequencerTrackId();
    if (assignment.trackId <= 0)
    {
        auto tracks = getTracks();
        if (!tracks.empty())
            assignment.trackId = tracks.front().id;
    }

    if (assignment.trackId > 0)
        syncAssignmentFromTrack(assignment);

    modMatrixUpdateAssignment(assignment);

    if (state)
        state->selectedAssignmentId = assignment.id;
}

void populateSourceCombo(HWND combo)
{
    if (!combo)
        return;

    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    for (size_t i = 0; i < kModSources.size(); ++i)
    {
        const wchar_t* label = kModSources[i];
        LRESULT index = SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));
        if (index >= 0)
            SendMessageW(combo, CB_SETITEMDATA, static_cast<WPARAM>(index), static_cast<LPARAM>(i));
    }
}

void populateParameterCombo(HWND combo, std::optional<TrackType> trackType = std::nullopt, int trackId = -1)
{
    if (!combo)
        return;

    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    int parameterCount = modMatrixGetParameterCount();
    const auto tracks = getTracks();
    for (int i = 0; i < parameterCount; ++i)
    {
        const ModParameterInfo* info = modMatrixGetParameterInfo(i);
        if (!info)
            continue;

        if (trackType)
        {
            if (!modMatrixParameterSupportsTrackType(*info, *trackType))
                continue;
        }

        const wchar_t* label = info->label;
        bool available = false;
        for (const auto& track : tracks)
            if (track.id == trackId) { available = modMatrixParameterAvailableForTrack(i, track); break; }
        if (trackId >= 0 && !available) continue;
        LRESULT index = SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));
        if (index >= 0)
            SendMessageW(combo, CB_SETITEMDATA, static_cast<WPARAM>(index), static_cast<LPARAM>(i));
    }
}

void populateTrackCombo(HWND combo)
{
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    for (const auto& track : getTracks())
    {
        auto label = getTrackLabel(track.id);
        LRESULT row = SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
        if (row >= 0) SendMessageW(combo, CB_SETITEMDATA, row, track.id);
    }
}

int getComboSelectionData(HWND combo)
{
    if (!combo)
        return -1;
    LRESULT index = SendMessageW(combo, CB_GETCURSEL, 0, 0);
    if (index < 0)
        return -1;
    return static_cast<int>(SendMessageW(combo, CB_GETITEMDATA, static_cast<WPARAM>(index), 0));
}

bool setComboSelectionByData(HWND combo, int data)
{
    if (!combo)
        return false;

    int count = static_cast<int>(SendMessageW(combo, CB_GETCOUNT, 0, 0));
    for (int i = 0; i < count; ++i)
    {
        int itemData = static_cast<int>(SendMessageW(combo, CB_GETITEMDATA, static_cast<WPARAM>(i), 0));
        if (itemData == data)
        {
            SendMessageW(combo, CB_SETCURSEL, static_cast<WPARAM>(i), 0);
            return true;
        }
    }

    return false;
}

void setSliderFromAssignment(HWND slider, const ModMatrixAssignment& assignment)
{
    if (!slider)
        return;

    float normalized = (modMatrixClampNormalized(assignment.normalizedAmount) + 1.0f) * 0.5f;
    int position = static_cast<int>(std::round(normalized * kSliderResolution));
    SendMessageW(slider, TBM_SETRANGE, TRUE, MAKELPARAM(0, kSliderResolution));
    SendMessageW(slider, TBM_SETPOS, TRUE, position);
}

void updateAmountLabel(HWND label, const ModMatrixAssignment& assignment)
{
    if (!label)
        return;

    const ModParameterInfo* info = modMatrixGetParameterInfo(assignment.parameterIndex);
    std::wstringstream ss;
    ss << L"Mod Amount: ";
    if (info)
    {
        float value = modMatrixNormalizedToValue(assignment.normalizedAmount, *info);
        float percent = modMatrixClampNormalized(assignment.normalizedAmount) * 100.0f;
        ss << std::showpos << std::fixed << std::setprecision(2) << value;
        ss << L" (" << std::setprecision(0) << percent << L"%)";
        ss << std::noshowpos;
    }
    else
    {
        ss << L"-";
    }

    std::wstring text = ss.str();
    SetWindowTextW(label, text.c_str());
}

void setEditFromAssignment(HWND edit, const ModMatrixAssignment& assignment)
{
    if (!edit)
        return;

    const ModParameterInfo* info = modMatrixGetParameterInfo(assignment.parameterIndex);
    if (!info)
    {
        SetWindowTextW(edit, L"");
        return;
    }

    float value = modMatrixNormalizedToValue(assignment.normalizedAmount, *info);
    std::wstringstream ss;
    ss << std::showpos << std::fixed << std::setprecision(3) << value;
    std::wstring text = ss.str();
    SetWindowTextW(edit, text.c_str());
}

std::wstring trimWhitespace(const std::wstring& text)
{
    size_t start = 0;
    size_t end = text.size();
    while (start < text.size() && std::iswspace(text[start]))
        ++start;
    while (end > start && std::iswspace(text[end - 1]))
        --end;
    return text.substr(start, end - start);
}

void refreshAssignmentRowText(HWND listView, int rowIndex, const ModMatrixAssignment& assignment)
{
    if (!listView)
        return;

    std::wstring source = getSourceLabel(assignment.sourceIndex);
    std::wstring track = getTrackLabel(assignment.trackId);
    const ModParameterInfo* info = modMatrixGetParameterInfo(assignment.parameterIndex);
    std::wstring parameter = info ? info->label : L"Unknown";
    std::wstring amount = formatAmountText(assignment);

    ListView_SetItemText(listView, rowIndex, 0, const_cast<wchar_t*>(source.c_str()));
    ListView_SetItemText(listView, rowIndex, 1, const_cast<wchar_t*>(track.c_str()));
    ListView_SetItemText(listView, rowIndex, 2, const_cast<wchar_t*>(parameter.c_str()));
    ListView_SetItemText(listView, rowIndex, 3, const_cast<wchar_t*>(amount.c_str()));
}

void repopulateAssignmentList(ModMatrixWindowState* state)
{
    if (!state || !state->listView)
        return;

    state->rebuildingList = true;
    ListView_DeleteAllItems(state->listView);

    auto assignments = modMatrixGetAssignments();
    if(state->embedded) assignments.erase(std::remove_if(assignments.begin(),assignments.end(),
        [&](const ModMatrixAssignment& a){return a.trackId!=state->boundTrack;}),assignments.end());

    for (size_t i = 0; i < assignments.size(); ++i)
    {
        const auto& assignment = assignments[i];
        std::wstring source = getSourceLabel(assignment.sourceIndex);

        LVITEMW item{};
        item.mask = LVIF_TEXT | LVIF_PARAM;
        item.iItem = static_cast<int>(i);
        item.pszText = const_cast<wchar_t*>(source.c_str());
        item.lParam = assignment.id;

        int inserted = ListView_InsertItem(state->listView, &item);
        if (inserted >= 0)
        {
            refreshAssignmentRowText(state->listView, inserted, assignment);
        }
    }

    if (assignments.empty())
    {
        state->selectedAssignmentId = 0;
        state->rebuildingList = false;
        SetWindowTextW(state->editorLabel, L"No assignments. Use + Add assignment to create a route.");
        return;
    }

    bool selectionMatched = false;
    int itemCount = ListView_GetItemCount(state->listView);
    for (int row = 0; row < itemCount; ++row)
    {
        LVITEMW item{};
        item.mask = LVIF_PARAM;
        item.iItem = row;
        if (ListView_GetItem(state->listView, &item) && item.lParam == state->selectedAssignmentId)
        {
            ListView_SetItemState(state->listView, row, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            selectionMatched = true;
            break;
        }
    }

    if (!selectionMatched)
    {
        state->selectedAssignmentId = assignments.front().id;
        ListView_SetItemState(state->listView, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    }
    state->rebuildingList = false;
    SetWindowTextW(state->editorLabel, L"Selected assignment");
}

bool updateAssignmentAmountFromEdit(ModMatrixWindowState* state)
{
    if (!state || !state->amountEdit)
        return false;

    auto assignment = modMatrixGetAssignment(state->selectedAssignmentId);
    if (!assignment)
        return false;

    const ModParameterInfo* info = modMatrixGetParameterInfo(assignment->parameterIndex);
    if (!info)
    {
        SetWindowTextW(state->amountEdit, L"");
        return false;
    }

    int length = GetWindowTextLengthW(state->amountEdit);
    std::wstring buffer(static_cast<size_t>(length) + 1, L'\0');
    if (length > 0)
        GetWindowTextW(state->amountEdit, buffer.data(), length + 1);
    buffer.resize(std::wcslen(buffer.c_str()));

    std::wstring text = trimWhitespace(buffer);
    if (text.empty())
    {
        assignment->normalizedAmount = 0.0f;
    }
    else
    {
        bool isPercent = false;
        if (!text.empty() && text.back() == L'%')
        {
            isPercent = true;
            text.pop_back();
            text = trimWhitespace(text);
        }

        wchar_t* endPtr = nullptr;
        float value = std::wcstof(text.c_str(), &endPtr);
        if (endPtr == text.c_str() || *endPtr != L'\0' || !std::isfinite(value))
        {
            setEditFromAssignment(state->amountEdit, *assignment);
            return false;
        }

        if (isPercent)
        {
            float normalized = modMatrixClampNormalized(value / 100.0f);
            assignment->normalizedAmount = normalized;
        }
        else
        {
            float normalized = modMatrixValueToNormalized(value, *info);
            assignment->normalizedAmount = modMatrixClampNormalized(normalized);
        }
    }

    modMatrixUpdateAssignment(*assignment);
    setSliderFromAssignment(state->amountSlider, *assignment);
    updateAmountLabel(state->amountLabel, *assignment);
    setEditFromAssignment(state->amountEdit, *assignment);
    repopulateAssignmentList(state);
    return true;
}

void enableAssignmentControls(ModMatrixWindowState* state, bool enable)
{
    if (!state)
        return;

    const HWND controls[] = {
        state->sourceCombo,
        state->trackCombo,
        state->resetAmountButton,
        state->parameterCombo,
        state->amountLabel,
        state->amountSlider,
        state->amountEdit,
        state->removeButton,
        state->lfoButton,
    };

    for (HWND control : controls)
    {
        if (control)
            EnableWindow(control, enable ? TRUE : FALSE);
    }
}

void loadAssignmentIntoControls(ModMatrixWindowState* state, int assignmentId)
{
    if (!state)
        return;

    if (state->sourceCombo)
    {
        LRESULT count = SendMessageW(state->sourceCombo, CB_GETCOUNT, 0, 0);
        if (count <= 0)
            populateSourceCombo(state->sourceCombo);
    }

    auto assignment = modMatrixGetAssignment(assignmentId);
    if (!assignment)
    {
        populateSourceCombo(state->sourceCombo);
        populateParameterCombo(state->parameterCombo);
        populateTrackCombo(state->trackCombo);
        SendMessageW(state->sourceCombo, CB_SETCURSEL, static_cast<WPARAM>(-1), 0);
        SendMessageW(state->trackCombo, CB_SETCURSEL, static_cast<WPARAM>(-1), 0);
        SendMessageW(state->amountSlider, TBM_SETPOS, TRUE, kSliderResolution / 2);
        enableAssignmentControls(state, false);
        SetWindowTextW(state->amountLabel, L"Mod Amount:");
        if (state->amountEdit)
            SetWindowTextW(state->amountEdit, L"");
        if (state->lfoButton)
            EnableWindow(state->lfoButton, FALSE);
        return;
    }

    enableAssignmentControls(state, true);
    if (state->lfoButton)
        EnableWindow(state->lfoButton, assignment->trackId > 0 ? TRUE : FALSE);
    bool sourceSelectionSet = setComboSelectionByData(state->sourceCombo, assignment->sourceIndex);
    if (!sourceSelectionSet)
    {
        SendMessageW(state->sourceCombo, CB_SETCURSEL, 0, 0);
        int fallbackSource = getComboSelectionData(state->sourceCombo);
        if (fallbackSource >= 0 && fallbackSource != assignment->sourceIndex)
        {
            assignment->sourceIndex = fallbackSource;
            modMatrixUpdateAssignment(*assignment);

            if (state->listView)
            {
                int selectedRow = ListView_GetNextItem(state->listView, -1, LVNI_SELECTED);
                if (selectedRow >= 0)
                    refreshAssignmentRowText(state->listView, selectedRow, *assignment);
            }
        }
    }
    bool trackUpdated = false;
    if (!trackExists(assignment->trackId))
    {
        int activeTrackId = getActiveSequencerTrackId();
        if (activeTrackId <= 0)
        {
            auto tracks = getTracks();
            if (!tracks.empty())
                activeTrackId = tracks.front().id;
        }

        if (activeTrackId > 0 && activeTrackId != assignment->trackId)
        {
            assignment->trackId = activeTrackId;
            trackUpdated = true;
        }
    }

    populateTrackCombo(state->trackCombo);
    setComboSelectionByData(state->trackCombo, assignment->trackId);
    if(state->embedded) EnableWindow(state->trackCombo,FALSE);
    auto trackType = getTrackTypeForTrack(assignment->trackId);
    populateParameterCombo(state->parameterCombo, trackType, assignment->trackId);

    bool parameterSelectionSet = setComboSelectionByData(state->parameterCombo, assignment->parameterIndex);
    bool parameterChanged = false;
    if (!parameterSelectionSet && (!modMatrixGetParameterInfo(assignment->parameterIndex) ||
        (trackType && !modMatrixParameterSupportsTrackType(*modMatrixGetParameterInfo(assignment->parameterIndex), *trackType))))
    {
        SendMessageW(state->parameterCombo, CB_SETCURSEL, 0, 0);
        int fallbackParameter = getComboSelectionData(state->parameterCombo);
        if (fallbackParameter >= 0 && fallbackParameter != assignment->parameterIndex)
        {
            assignment->parameterIndex = fallbackParameter;
            parameterChanged = true;
        }
    }

    bool assignmentChanged = trackUpdated || parameterChanged;

    if (assignmentChanged)
    {
        syncAssignmentFromTrack(*assignment);
        modMatrixUpdateAssignment(*assignment);

        if (state->listView)
        {
            int selectedRow = ListView_GetNextItem(state->listView, -1, LVNI_SELECTED);
            if (selectedRow >= 0)
                refreshAssignmentRowText(state->listView, selectedRow, *assignment);
        }
    }

    setSliderFromAssignment(state->amountSlider, *assignment);
    updateAmountLabel(state->amountLabel, *assignment);
    setEditFromAssignment(state->amountEdit, *assignment);

    if (assignment->trackId > 0)
        notifyLfoWindowTrackChanged(assignment->trackId);
}

ModMatrixWindowState* getWindowState(HWND hwnd)
{
    return reinterpret_cast<ModMatrixWindowState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

void removeAssignment(ModMatrixWindowState* state, int assignmentId)
{
    if (assignmentId <= 0)
        return;

    if (!modMatrixRemoveAssignment(assignmentId))
        return;

    if (state)
    {
        auto assignments = modMatrixGetAssignments();
        if (!assignments.empty())
            state->selectedAssignmentId = assignments.front().id;
        else
            state->selectedAssignmentId = 0;
    }
}

int matrixPixels(const ModMatrixWindowState*, int value)
{
    HDC dc = GetDC(nullptr);
    int dpi = dc ? GetDeviceCaps(dc, LOGPIXELSY) : 96;
    if (dc) ReleaseDC(nullptr, dc);
    return MulDiv(value, dpi, 96);
}

void layoutMatrix(HWND hwnd, ModMatrixWindowState* state)
{
    if (!state) return;
    RECT client{};
    GetClientRect(hwnd, &client);
    auto px = [state](int value) { return matrixPixels(state, value); };
    int padding = px(16);
    int width = std::max(1, static_cast<int>(client.right) - padding * 2);
    int listTop = px(58);
    int listHeight = std::max(px(100), static_cast<int>(client.bottom) - listTop - px(240));
    auto move = [](HWND control, int x, int y, int w, int h) {
        if (control) MoveWindow(control, x, y, std::max(1, w), std::max(1, h), TRUE);
    };
    move(state->addButton, padding, padding, px(140), px(30));
    move(state->removeButton, padding + px(148), padding, px(100), px(30));
    move(state->lfoButton, client.right - padding - px(132), padding, px(132), px(30));
    move(state->listView, padding, listTop, width, listHeight);
    int available = std::max(1, width - GetSystemMetrics(SM_CXVSCROLL) - px(4));
    int columns[] = {available * 20 / 100, available * 25 / 100, available * 30 / 100, 0};
    columns[3] = available - columns[0] - columns[1] - columns[2];
    for (int i = 0; i < 4; ++i) ListView_SetColumnWidth(state->listView, i, columns[i]);
    int y = listTop + listHeight + px(12);
    int half = (width - px(16)) / 2;
    int right = padding + half + px(16);
    move(state->editorLabel, padding, y, width, px(20));
    move(state->sourceLabel, padding, y + px(28), half, px(18));
    move(state->trackLabel, right, y + px(28), half, px(18));
    move(state->sourceCombo, padding, y + px(48), half, px(kComboDropdownHeight));
    move(state->trackCombo, right, y + px(48), half, px(kComboDropdownHeight));
    move(state->parameterLabel, padding, y + px(84), width, px(18));
    move(state->parameterCombo, padding, y + px(104), width, px(kComboDropdownHeight));
    move(state->amountLabel, padding, y + px(140), width, px(20));
    int sliderWidth = width - px(190);
    move(state->amountSlider, padding, y + px(164), sliderWidth, px(32));
    move(state->amountEdit, padding + sliderWidth + px(8), y + px(164), px(110), px(28));
    move(state->resetAmountButton, client.right - padding - px(64), y + px(164), px(64), px(28));
    move(state->hintLabel, padding, y + px(200), width, px(20));
}

bool createMatrixControls(HWND hwnd, ModMatrixWindowState* state)
{
    bool success = true;
    auto create = [&](const wchar_t* cls, const wchar_t* text, DWORD style, int id = 0, DWORD extended = 0) {
        HWND control = CreateWindowExW(extended, cls, text, WS_CHILD | WS_VISIBLE | style,
            0, 0, 0, 0, hwnd, makeControlId(id), GetModuleHandle(nullptr), nullptr);
        if (!control) success = false;
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), FALSE);
        return control;
    };
    state->addButton = create(L"BUTTON", L"+ Add assignment", WS_TABSTOP | BS_PUSHBUTTON, kAddButtonId);
    state->removeButton = create(L"BUTTON", L"Remove", WS_TABSTOP | BS_PUSHBUTTON, kRemoveButtonId);
    state->lfoButton = create(L"BUTTON", L"Edit LFOs...", WS_TABSTOP | BS_PUSHBUTTON, kLfoButtonId);
    state->listView = create(WC_LISTVIEWW, L"", WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
        kListViewId, WS_EX_CLIENTEDGE);
    ListView_SetExtendedListViewStyle(state->listView, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
    const wchar_t* columns[] = {L"Source", L"Track", L"Parameter", L"Amount"};
    for (int i = 0; i < 4; ++i) {
        LVCOLUMNW column{};
        column.mask = LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM;
        column.iSubItem = i;
        column.cx = 120;
        column.pszText = const_cast<wchar_t*>(columns[i]);
        ListView_InsertColumn(state->listView, i, &column);
    }
    state->editorLabel = create(L"STATIC", L"Selected assignment", SS_LEFT);
    state->sourceLabel = create(L"STATIC", L"Source", SS_LEFT);
    state->sourceCombo = create(WC_COMBOBOXW, L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, kSourceComboId);
    state->trackLabel = create(L"STATIC", L"Target track", SS_LEFT);
    state->trackCombo = create(WC_COMBOBOXW, L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, kTrackComboId);
    state->parameterLabel = create(L"STATIC", L"Parameter", SS_LEFT);
    state->parameterCombo = create(WC_COMBOBOXW, L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, kParameterComboId);
    state->amountLabel = create(L"STATIC", L"Mod Amount:", SS_LEFT, kAmountLabelId);
    state->amountSlider = create(TRACKBAR_CLASSW, L"", WS_TABSTOP | TBS_AUTOTICKS, kAmountSliderId);
    SendMessageW(state->amountSlider, TBM_SETRANGE, TRUE, MAKELPARAM(0, kSliderResolution));
    SendMessageW(state->amountSlider, TBM_SETTICFREQ, kSliderResolution / 2, 0);
    state->amountEdit = create(L"EDIT", L"", WS_TABSTOP | ES_LEFT | ES_AUTOHSCROLL, kAmountEditId, WS_EX_CLIENTEDGE);
    SendMessageW(state->amountEdit, EM_SETLIMITTEXT, 32, 0);
    state->resetAmountButton = create(L"BUTTON", L"Reset", WS_TABSTOP | BS_PUSHBUTTON, kResetAmountId);
    state->hintLabel = create(L"STATIC", L"Amount: value or signed percent. Enter applies; Esc cancels.", SS_LEFT);
    return success;
}

void ensureModMatrixWindowClass()
{
    if (gModMatrixWindowClassRegistered)
        return;

    gModMatrixInitControls.dwSize = sizeof(gModMatrixInitControls);
    gModMatrixInitControls.dwICC = ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES;

    InitCommonControlsEx(&gModMatrixInitControls);

    WNDCLASSW wc = {};
    wc.lpfnWndProc = [](HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) -> LRESULT {
        ModMatrixWindowState* state = getWindowState(hwnd);

        switch (msg)
        {
        case WM_CREATE:
        {
            auto* newState = new ModMatrixWindowState();
            newState->embedded=(GetWindowLongPtrW(hwnd,GWL_STYLE)&WS_CHILD)!=0;
            newState->boundTrack=getActiveSequencerTrackId();
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(newState));
            if (!createMatrixControls(hwnd, newState)) return -1;
            populateSourceCombo(newState->sourceCombo);
            auto assignments = modMatrixGetAssignments();
            if (!assignments.empty()) newState->selectedAssignmentId = assignments.front().id;
            repopulateAssignmentList(newState);
            loadAssignmentIntoControls(newState, newState->selectedAssignmentId);
            layoutMatrix(hwnd, newState);
            return 0;
        }
        case WM_DESTROY:
        {
            if (state)
            {
                delete state;
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            }
            if (hwnd == gModMatrixWindow)
            {
                gModMatrixWindow = nullptr;
                requestMainMenuRefresh();
            }
            return 0;
        }
        case WM_GETMINMAXINFO:
        {
            auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
            info->ptMinTrackSize.x = matrixPixels(state, 640);
            info->ptMinTrackSize.y = matrixPixels(state, 540);
            return 0;
        }
        case WM_SIZE:
            layoutMatrix(hwnd, state);
            return 0;
        case WM_COMMAND:
        {
            if (!state)
                return 0;

            int controlId = LOWORD(wParam);
            int code = HIWORD(wParam);

            switch (controlId)
            {
            case kAddButtonId:
                addAssignment(state);
                repopulateAssignmentList(state);
                loadAssignmentIntoControls(state, state->selectedAssignmentId);
                return 0;
            case kRemoveButtonId:
                removeAssignment(state, state->selectedAssignmentId);
                repopulateAssignmentList(state);
                loadAssignmentIntoControls(state, state->selectedAssignmentId);
                return 0;
            case kLfoButtonId:
            {
                int targetTrackId = 0;
                auto assignment = modMatrixGetAssignment(state->selectedAssignmentId);
                if (assignment && assignment->trackId > 0)
                    targetTrackId = assignment->trackId;

                if (targetTrackId <= 0)
                {
                    targetTrackId = state->embedded ? state->boundTrack : getActiveSequencerTrackId();
                    if (targetTrackId <= 0)
                    {
                        auto tracks = getTracks();
                        if (!tracks.empty())
                            targetTrackId = tracks.front().id;
                    }
                }

                if (targetTrackId > 0)
                    openLfoWindow(hwnd, targetTrackId);
                return 0;
            }
            case kTrackComboId:
                if (code == CBN_SELCHANGE)
                {
                    int trackId = getComboSelectionData(state->trackCombo);
                    auto assignment = modMatrixGetAssignment(state->selectedAssignmentId);
                    if (assignment && trackExists(trackId))
                    {
                        assignment->trackId = trackId;
                        modMatrixUpdateAssignment(*assignment);
                        loadAssignmentIntoControls(state, assignment->id);
                        repopulateAssignmentList(state);
                    }
                }
                return 0;
            case kResetAmountId:
            {
                auto assignment = modMatrixGetAssignment(state->selectedAssignmentId);
                if (assignment)
                {
                    assignment->normalizedAmount = 0.0f;
                    modMatrixUpdateAssignment(*assignment);
                    loadAssignmentIntoControls(state, assignment->id);
                    repopulateAssignmentList(state);
                }
                return 0;
            }
            case kSourceComboId:
                if (code == CBN_SELCHANGE)
                {
                    int data = getComboSelectionData(state->sourceCombo);
                    auto assignment = modMatrixGetAssignment(state->selectedAssignmentId);
                    if (assignment && data >= 0)
                    {
                        assignment->sourceIndex = data;
                        modMatrixUpdateAssignment(*assignment);
                        repopulateAssignmentList(state);
                    }
                }
                return 0;
            case kParameterComboId:
                if (code == CBN_DROPDOWN)
                {
                    auto assignment = modMatrixGetAssignment(state->selectedAssignmentId);
                    if (assignment)
                    {
                        populateParameterCombo(state->parameterCombo, getTrackTypeForTrack(assignment->trackId), assignment->trackId);
                        setComboSelectionByData(state->parameterCombo, assignment->parameterIndex);
                    }
                }
                if (code == CBN_SELCHANGE)
                {
                    int data = getComboSelectionData(state->parameterCombo);
                    auto assignment = modMatrixGetAssignment(state->selectedAssignmentId);
                    if (assignment && data >= 0)
                    {
                        assignment->parameterIndex = data;
                        syncAssignmentFromTrack(*assignment);
                        modMatrixUpdateAssignment(*assignment);
                        setSliderFromAssignment(state->amountSlider, *assignment);
                        updateAmountLabel(state->amountLabel, *assignment);
                        setEditFromAssignment(state->amountEdit, *assignment);
                        repopulateAssignmentList(state);
                    }
                }
                return 0;
            case kAmountEditId:
                if (code == EN_KILLFOCUS)
                {
                    updateAssignmentAmountFromEdit(state);
                }
                return 0;
            default:
                break;
            }
            return 0;
        }
        case WM_NOTIFY:
        {
            if (!state)
                return 0;

            auto* header = reinterpret_cast<LPNMHDR>(lParam);
            if (!state->rebuildingList && header->hwndFrom == state->listView && header->code == LVN_ITEMCHANGED)
            {
                auto* changed = reinterpret_cast<LPNMLISTVIEW>(lParam);
                if ((changed->uChanged & LVIF_STATE) != 0)
                {
                    if ((changed->uNewState & LVIS_SELECTED) != 0)
                    {
                        LVITEMW item{};
                        item.mask = LVIF_PARAM;
                        item.iItem = changed->iItem;
                        item.iSubItem = 0;
                        if (ListView_GetItem(state->listView, &item))
                        {
                            state->selectedAssignmentId = static_cast<int>(item.lParam);
                            loadAssignmentIntoControls(state, state->selectedAssignmentId);
                        }
                    }
                }
            }
            return 0;
        }
        case WM_HSCROLL:
        {
            if (!state)
                return 0;

            HWND slider = reinterpret_cast<HWND>(lParam);
            if (slider == state->amountSlider)
            {
                auto assignment = modMatrixGetAssignment(state->selectedAssignmentId);
                if (!assignment)
                    return 0;

                int position = static_cast<int>(SendMessageW(state->amountSlider, TBM_GETPOS, 0, 0));
                float sliderNormalized = static_cast<float>(position) / static_cast<float>(kSliderResolution);
                assignment->normalizedAmount = modMatrixClampNormalized(sliderNormalized * 2.0f - 1.0f);
                modMatrixUpdateAssignment(*assignment);
                modMatrixApplyAssignment(*assignment);
                updateAmountLabel(state->amountLabel, *assignment);
                setEditFromAssignment(state->amountEdit, *assignment);

                int itemCount = ListView_GetItemCount(state->listView);
                for (int row = 0; row < itemCount; ++row)
                {
                    LVITEMW item{};
                    item.mask = LVIF_PARAM;
                    item.iItem = row;
                    if (ListView_GetItem(state->listView, &item) && static_cast<int>(item.lParam) == assignment->id)
                    {
                        refreshAssignmentRowText(state->listView, row, *assignment);
                        break;
                    }
                }
            }
            return 0;
        }
        case WM_MOD_MATRIX_REFRESH_TRACKS:
        {
            if (!state)
                return 0;

            repopulateAssignmentList(state);
            loadAssignmentIntoControls(state, state->selectedAssignmentId);
            return 0;
        }
        case WM_MOD_MATRIX_REFRESH_VALUES:
        {
            if (!state)
                return 0;

            auto selected = modMatrixGetAssignment(state->selectedAssignmentId);
            if (selected && GetFocus() != state->amountEdit)
            {
                setSliderFromAssignment(state->amountSlider, *selected);
                updateAmountLabel(state->amountLabel, *selected);
                setEditFromAssignment(state->amountEdit, *selected);
            }

            repopulateAssignmentList(state);
            return 0;
        }
        default:
            break;
        }

        return DefWindowProcW(hwnd, msg, wParam, lParam);
    };

    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = kModMatrixWindowClassName;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);

    if (RegisterClassW(&wc))
    {
        gModMatrixWindowClassRegistered = true;
    }
}

} // namespace

bool handleModMatrixKeyboard(MSG* message)
{
    if (!message || !gModMatrixWindow || !IsWindow(gModMatrixWindow) ||
        (message->hwnd != gModMatrixWindow && !IsChild(gModMatrixWindow, message->hwnd)))
        return false;
    auto* state = getWindowState(gModMatrixWindow);
    if (state && message->hwnd == state->amountEdit && message->message == WM_KEYDOWN)
    {
        if (message->wParam == VK_ESCAPE)
        {
            auto assignment = modMatrixGetAssignment(state->selectedAssignmentId);
            if (assignment) setEditFromAssignment(state->amountEdit, *assignment);
            SetFocus(state->listView);
            return true;
        }
        if (message->wParam == VK_RETURN)
        {
            SetFocus(state->listView);
            return true;
        }
    }
    return IsDialogMessageW(gModMatrixWindow, message) != FALSE;
}

bool isModMatrixWindowOpen()
{
    if(editingPanelCreated())return isEditingPanelVisible(EditingPage::Mod);
    if(gModMatrixWindow && (GetWindowLongPtrW(gModMatrixWindow,GWL_STYLE)&WS_CHILD))return isEditingPanelVisible(EditingPage::Mod);
    return gModMatrixWindow && IsWindow(gModMatrixWindow);
}

void closeModMatrixWindow()
{
    if (gModMatrixWindow && IsWindow(gModMatrixWindow))
    {
        DestroyWindow(gModMatrixWindow);
        gModMatrixWindow = nullptr;
    }
}

void openModMatrixWindow(HWND parent)
{
    if(editingPanelCreated()){revealEditingPanel(EditingPage::Mod);return;}
    if(gModMatrixWindow && (GetWindowLongPtrW(gModMatrixWindow,GWL_STYLE)&WS_CHILD)) {
        revealEditingPanel(EditingPage::Mod);return;
    }
    if (gModMatrixWindow && IsWindow(gModMatrixWindow))
    {
        SetForegroundWindow(gModMatrixWindow);
        return;
    }

    ensureModMatrixWindowClass();
    if (!gModMatrixWindowClassRegistered)
        return;

    RECT parentRect{0, 0, 0, 0};
    if (parent && IsWindow(parent))
        GetWindowRect(parent, &parentRect);

    int x = CW_USEDEFAULT;
    int y = CW_USEDEFAULT;
    if (parentRect.right > parentRect.left && parentRect.bottom > parentRect.top)
    {
        x = parentRect.left + 80;
        y = parentRect.top + 80;
    }

    HWND hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_CONTROLPARENT,
                                kModMatrixWindowClassName,
                                L"Modulation Matrix",
                                WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                                x,
                                y,
                                kModMatrixWindowWidth,
                                kModMatrixWindowHeight,
                                parent,
                                nullptr,
                                GetModuleHandle(nullptr),
                                nullptr);
    if (hwnd)
    {
        gModMatrixWindow = hwnd;
        ShowWindow(hwnd, SW_SHOW);
        UpdateWindow(hwnd);
        requestMainMenuRefresh();
    }
}

void toggleModMatrixWindow(HWND parent)
{
    if(editingPanelCreated()){revealEditingPanel(EditingPage::Mod);return;}
    if(gModMatrixWindow && (GetWindowLongPtrW(gModMatrixWindow,GWL_STYLE)&WS_CHILD)) {
        revealEditingPanel(EditingPage::Mod);return;
    }
    if (gModMatrixWindow && IsWindow(gModMatrixWindow))
    {
        closeModMatrixWindow();
        requestMainMenuRefresh();
        return;
    }

    openModMatrixWindow(parent);
}

void notifyModMatrixWindowTrackListChanged()
{
    if (gModMatrixWindow && IsWindow(gModMatrixWindow))
    {
        PostMessageW(gModMatrixWindow, WM_MOD_MATRIX_REFRESH_TRACKS, 0, 0);
    }
}

void notifyModMatrixWindowValuesChanged(int trackId)
{
    if (gModMatrixWindow && IsWindow(gModMatrixWindow))
    {
        PostMessageW(gModMatrixWindow, WM_MOD_MATRIX_REFRESH_VALUES, static_cast<WPARAM>(trackId), 0);
    }
}

void focusModMatrixTarget(ModMatrixParameter parameter, int trackId)
{
    if(editingPanelCreated()){focusEditingModTarget(modMatrixGetParameterIndex(parameter),trackId);return;}
    revealEditingPanel(EditingPage::Mod,trackId);
    if (trackId <= 0)
        return;

    if (!gModMatrixWindow || !IsWindow(gModMatrixWindow))
        return;

    ModMatrixWindowState* state = getWindowState(gModMatrixWindow);
    if (!state)
        return;

    int parameterIndex = modMatrixGetParameterIndex(parameter);
    if (parameterIndex < 0)
        return;

    auto assignments = modMatrixGetAssignments();
    auto existing = std::find_if(assignments.begin(), assignments.end(), [&](const ModMatrixAssignment& assignment) {
        return assignment.trackId == trackId && assignment.parameterIndex == parameterIndex;
    });

    int assignmentId = 0;
    if (existing != assignments.end())
    {
        assignmentId = existing->id;
    }
    else
    {
        ModMatrixAssignment assignment = modMatrixCreateAssignment();
        assignment.sourceIndex = 0;
        assignment.trackId = trackId;
        assignment.parameterIndex = parameterIndex;
        syncAssignmentFromTrack(assignment);
        modMatrixUpdateAssignment(assignment);
        assignmentId = assignment.id;
    }

    state->selectedAssignmentId = assignmentId;
    repopulateAssignmentList(state);
    loadAssignmentIntoControls(state, state->selectedAssignmentId);

    if (state->listView)
    {
        int itemCount = ListView_GetItemCount(state->listView);
        for (int row = 0; row < itemCount; ++row)
        {
            LVITEMW item{};
            item.mask = LVIF_PARAM;
            item.iItem = row;
            if (ListView_GetItem(state->listView, &item) && static_cast<int>(item.lParam) == assignmentId)
            {
                ListView_SetItemState(state->listView, row, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
                ListView_EnsureVisible(state->listView, row, FALSE);
                break;
            }
        }
    }

    SetFocus(state->listView);
}

HWND createModMatrixView(HWND parent)
{
    ensureModMatrixWindowClass();
    gModMatrixWindow=CreateWindowExW(WS_EX_CONTROLPARENT,kModMatrixWindowClassName,L"Mod Matrix",
        WS_CHILD|WS_VISIBLE|WS_CLIPCHILDREN,0,0,420,440,parent,nullptr,GetModuleHandle(nullptr),nullptr);
    auto* state=getWindowState(gModMatrixWindow);
    if(state)state->embedded=true;
    bindModMatrixView(getActiveSequencerTrackId());
    return gModMatrixWindow;
}
void bindModMatrixView(int trackId)
{
    auto* state=gModMatrixWindow?getWindowState(gModMatrixWindow):nullptr;
    if(!state)return;
    if(GetFocus()==state->amountEdit){updateAssignmentAmountFromEdit(state);SetFocus(state->listView);}
    state->boundTrack=trackId;state->selectedAssignmentId=0;
    repopulateAssignmentList(state);loadAssignmentIntoControls(state,state->selectedAssignmentId);
    EnableWindow(state->lfoButton,trackId>0);
}
