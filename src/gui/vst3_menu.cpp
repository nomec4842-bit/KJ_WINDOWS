#include "gui/vst3_menu.h"
#include "gui/editing_panel.h"
#include "hosting/TrackVST3.h"
#include "hosting/VST3Host.h"
#include "core/audio_engine.h"
#include "core/audio_recording.h"
#include "core/sequencer.h"
#include <algorithm>
#include <filesystem>
#include <stdexcept>
#include <shobjidl.h>
#include <wrl/client.h>


void appendVst3Menu(HMENU parent) {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, kMenuVst3Refresh, L"&Refresh VST3 List");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuVst3Rack, L"&Instrument / Effects Rack...");
    AppendMenuW(menu, MF_STRING, kMenuVst3Instrument, L"Load &Instrument on Selected Track...");
    AppendMenuW(menu, MF_STRING, kMenuVst3Effect, L"Load &Effect on Selected Track...");
    AppendMenuW(menu, MF_STRING, kMenuVst3EditInstrument, L"Open Instrument Editor");
    AppendMenuW(menu, MF_STRING, kMenuVst3EditEffect, L"Open Effect Editor");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuVst3UnloadInstrument, L"Unload Instrument");
    AppendMenuW(menu, MF_STRING, kMenuVst3UnloadEffect, L"Remove Effect...");
    AppendMenuW(parent, MF_POPUP, reinterpret_cast<UINT_PTR>(menu), L"VST&3");
}
namespace {
std::vector<std::string> pluginPaths;
bool pluginPathsReady=false;
const std::vector<std::string>& refreshPluginPaths() {
    auto paths=VST3::Hosting::Module::getModulePaths();
    std::sort(paths.begin(),paths.end());
    paths.erase(std::unique(paths.begin(),paths.end()),paths.end());
    pluginPaths=std::move(paths);
    pluginPathsReady=true;
    return pluginPaths;
}
const std::vector<std::string>& availablePluginPaths() {
    if(!pluginPathsReady)refreshPluginPaths();
    return pluginPaths;
}
int choose(HWND owner, const std::vector<std::wstring>& labels) {
    HMENU menu = CreatePopupMenu();
    for (size_t i = 0; i < labels.size(); ++i) {
        auto label = labels[i];
        size_t pos = 0;
        while ((pos = label.find(L'&', pos)) != std::wstring::npos) { label.insert(pos, 1, L'&'); pos += 2; }
        AppendMenuW(menu, MF_STRING, i + 1, label.c_str());
    }
    POINT point; GetCursorPos(&point);
    int selected = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY,
                                 point.x, point.y, 0, owner, nullptr);
    DestroyMenu(menu);
    return selected - 1;
}
std::string browsePlugin(HWND owner,bool folder) {
    using Microsoft::WRL::ComPtr;
    ComPtr<IFileOpenDialog> dialog;
    if(FAILED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog))))
        throw std::runtime_error("Could not open the Windows plugin browser");
    DWORD options=0;dialog->GetOptions(&options);
    dialog->SetOptions(options|FOS_FORCEFILESYSTEM|FOS_PATHMUSTEXIST|(folder?FOS_PICKFOLDERS:FOS_FILEMUSTEXIST));
    dialog->SetTitle(folder?L"Select a .vst3 bundle folder":L"Select a VST3 plugin");
    if(!folder){
        const COMDLG_FILTERSPEC filter[]={ {L"VST3 plugins (*.vst3)",L"*.vst3"} };
        dialog->SetFileTypes(1,filter);
    }
    HRESULT result=dialog->Show(owner);
    if(result==HRESULT_FROM_WIN32(ERROR_CANCELLED))return {};
    if(FAILED(result))throw std::runtime_error("The Windows plugin browser could not open");
    ComPtr<IShellItem> item;PWSTR selected=nullptr;
    if(FAILED(dialog->GetResult(&item))||FAILED(item->GetDisplayName(SIGDN_FILESYSPATH,&selected)))
        throw std::runtime_error("Could not read the selected plugin path");
    std::filesystem::path path(selected);CoTaskMemFree(selected);
    // A file picker may enter a bundle. Load its outer bundle, preserving resources.
    for(auto parent=path.parent_path();!parent.empty();parent=parent.parent_path()){
        if(_wcsicmp(parent.extension().c_str(),L".vst3")==0)path=parent;
        if(parent==parent.root_path())break;
    }
    if(_wcsicmp(path.extension().c_str(),L".vst3")!=0)
        throw std::runtime_error("Select a .vst3 plugin file or a .vst3 bundle folder");
    return path.u8string();
}
std::string choosePlugin(HWND owner) {
    auto paths=availablePluginPaths();
    std::vector<std::wstring> names{L"Browse for a .vst3 file...",L"Browse for a .vst3 bundle folder..."};
    for(const auto& path:paths)names.push_back(std::filesystem::u8path(path).filename().wstring());
    int selected=choose(owner,names);
    if(selected<0)return {};
    if(selected<2)return browsePlugin(owner,selected==1);
    return paths[selected-2];
}

}
void handleVst3Menu(HWND owner, int command, int id) {
    if(command==kMenuVst3Refresh) {
        try {
            auto count=refreshPluginPaths().size();
            std::wstring message=L"VST3 list refreshed. Found "+std::to_wstring(count)+L" plugin modules in the standard installation folders.";
            MessageBoxW(owner,message.c_str(),L"Refresh VST3 List",MB_OK|MB_ICONINFORMATION);
        } catch(const std::exception& error) {
            MessageBoxA(owner,error.what(),"Refresh VST3 List",MB_OK|MB_ICONERROR);
        }
        return;
    }
    if (id <= 0) return;
    if (command == kMenuVst3Rack) { revealEditingPanel(EditingPage::Fx, id); return; }
    if (command == kMenuVst3EditInstrument || command == kMenuVst3EditEffect) {
        try {
            if (command == kMenuVst3EditEffect) { revealEditingPanel(EditingPage::Fx, id); return; }
            kj::openTrackVst3Editor(id, true, owner);
        }
        catch (const std::exception& error) { MessageBoxA(owner, error.what(), "VST3 editor", MB_OK | MB_ICONERROR); }
        return;
    }
    bool loaded = false;
    std::string loadedSlot;
    const bool instrument = command == kMenuVst3Instrument || command == kMenuVst3UnloadInstrument;
    try {
        if (command == kMenuVst3UnloadInstrument || command == kMenuVst3UnloadEffect) {
            if (!instrument) { revealEditingPanel(EditingPage::Fx, id); return; }
            kj::unloadTrackVst3(id, instrument);
        } else {
            auto path=choosePlugin(owner);
            if(path.empty())return;
            std::vector<std::wstring> names;
            auto classes = kj::VST3Host::scan(path);
            classes.erase(std::remove_if(classes.begin(), classes.end(), [instrument](const auto& info) {
                const auto& categories = info.subCategories();
                return (std::find(categories.begin(), categories.end(), "Instrument") != categories.end()) != instrument;
            }), classes.end());
            if (classes.empty()) throw std::runtime_error(instrument ? "This module contains no VST3 instruments" : "This module contains no VST3 effects");
            int classIndex = 0;
            if (classes.size() > 1) {
                names.clear();
                for (const auto& info : classes) names.push_back(std::filesystem::u8path(info.name()).wstring());
                classIndex = choose(owner, names);
            }
            if (classIndex >= 0) {
                loadedSlot = kj::addTrackVst3(id, instrument, path, classes[classIndex].ID().toString(), getAudioSampleRate());
                if (!instrument) { auto order = trackGetFxOrder(id); order.push_back(loadedSlot); trackSetFxOrder(id, std::move(order)); }
                loaded = true;
                if (instrument) trackSetType(id, TrackType::Vst3);
            }
        }
        if (loaded) kj::openTrackVst3SlotEditor(id, loadedSlot, owner);
    } catch (const std::exception& error) {
        MessageBoxA(owner, error.what(), "VST3", MB_OK | MB_ICONERROR);
    }
}
void serviceVst3MainThread(HWND owner) {
    kj::serviceTrackVst3Controllers();
    if (const double rate = kj::takeTrackVst3RateRequest(); rate > 0) {
        const bool wasPlaying = isPlaying.load(std::memory_order_relaxed);
        shutdownAudio();
        try {
            kj::prepareTrackVst3(rate);
            initAudio(false);
            if (wasPlaying) { requestSequencerReset(); isPlaying.store(true, std::memory_order_relaxed); }
        } catch (const std::exception& error) {
            initAudio(false);
            MessageBoxA(owner, error.what(), "VST3 device reconfiguration failed", MB_OK | MB_ICONERROR);
        }
    }
}
void handleVst3SlotAction(HWND owner, int track, const std::string& id, Vst3SlotAction action) {
    try {
        auto slots = kj::getTrackVst3Slots(track);
        auto slot = std::find_if(slots.begin(), slots.end(), [&](const auto& s) { return s.id == id; });
        if (slot == slots.end()) return;
        if (action == Vst3SlotAction::Editor) { kj::openTrackVst3SlotEditor(track, id, owner); return; }
        std::string path;
        if (action == Vst3SlotAction::Locate) { path = choosePlugin(owner); if (path.empty()) return; }
        switch (action) {
        case Vst3SlotAction::Up:
        case Vst3SlotAction::Down: {
            auto order = trackGetFxOrder(track); auto it = std::find(order.begin(), order.end(), id);
            if (it != order.end()) {
                int index = static_cast<int>(it - order.begin()), next = index + (action == Vst3SlotAction::Up ? -1 : 1);
                if (next >= 0 && next < static_cast<int>(order.size())) {
                    if (order[next].compare(0, 3, "kj:") != 0) kj::moveTrackVst3(track, id, action == Vst3SlotAction::Up ? -1 : 1);
                    std::swap(order[index], order[next]); trackSetFxOrder(track, std::move(order));
                }
            }
            break;
        }
        case Vst3SlotAction::Bypass: kj::bypassTrackVst3(track, id, !slot->bypass); break;
        case Vst3SlotAction::Remove: {
            kj::removeTrackVst3(track, id);
            auto order = trackGetFxOrder(track); order.erase(std::remove(order.begin(), order.end(), id), order.end()); trackSetFxOrder(track, std::move(order));
            break;
        }
        case Vst3SlotAction::Locate: kj::locateTrackVst3(track, id, path, getAudioSampleRate()); break;
        default: break;
        }
    } catch (const std::exception& e) {
        MessageBoxA(owner, e.what(), "VST3 rack", MB_OK | MB_ICONERROR);
    }
    InvalidateRect(owner, nullptr, FALSE);
}
