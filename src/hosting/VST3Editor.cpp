#include "hosting/VST3Editor.h"
#include "pluginterfaces/base/funknownimpl.h"
#include "pluginterfaces/gui/iplugviewcontentscalesupport.h"
#include <windows.h>
#include <stdexcept>

namespace kj {
using namespace Steinberg;
struct VST3Editor::Impl : U::Implements<U::Directly<IPlugFrame>> {
    IPtr<IPlugView> view;
    HWND window = nullptr;
    bool attached = false, resizing = false;
    explicit Impl(IPtr<IPlugView> v) : view(std::move(v)) {}
    ~Impl() { close(); }
    void detach() {
        if (attached) { attached = false; view->removed(); }
        if (view) view->setFrame(nullptr);
    }
    void close() {
        detach();
        if (window) DestroyWindow(window);
    }
    tresult PLUGIN_API resizeView(IPlugView* sender, ViewRect* size) override {
        if (sender != view.get() || !window || !size || size->getWidth() <= 0 ||
            size->getHeight() <= 0 || size->getWidth() > 16384 || size->getHeight() > 16384)
            return kInvalidArgument;
        RECT frame {0, 0, size->getWidth(), size->getHeight()};
        AdjustWindowRectEx(&frame, static_cast<DWORD>(GetWindowLongPtrW(window, GWL_STYLE)), FALSE,
                          static_cast<DWORD>(GetWindowLongPtrW(window, GWL_EXSTYLE)));
        resizing = true;
        SetWindowPos(window, nullptr, 0, 0, frame.right - frame.left, frame.bottom - frame.top,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        auto result = view->onSize(size);
        resizing = false;
        return result;
    }
    void scale() {
        if (auto scaling = U::cast<IPlugViewContentScaleSupport>(view))
            scaling->setContentScaleFactor(static_cast<float>(GetDpiForWindow(window)) / 96.f);
    }
    static LRESULT CALLBACK proc(HWND hwnd, UINT message, WPARAM w, LPARAM l) {
        auto* self = reinterpret_cast<Impl*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            self = static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            self->window = hwnd;
        }
        if (!self) return DefWindowProcW(hwnd, message, w, l);
        switch (message) {
        case WM_CLOSE: self->close(); return 0;
        case WM_DESTROY: self->detach(); return 0;
        case WM_NCDESTROY:
            self->window = nullptr;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            break;
        case WM_SIZE:
            if (self->attached && !self->resizing && w != SIZE_MINIMIZED) {
                ViewRect size {0, 0, LOWORD(l), HIWORD(l)};
                self->view->onSize(&size);
            }
            return 0;
        case WM_SIZING: {
            auto* outer = reinterpret_cast<RECT*>(l);
            RECT border {};
            AdjustWindowRectEx(&border, static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_STYLE)), FALSE, 0);
            const int dx = border.right - border.left, dy = border.bottom - border.top;
            ViewRect size {0, 0, outer->right - outer->left - dx, outer->bottom - outer->top - dy};
            self->view->checkSizeConstraint(&size);
            if (w == WMSZ_LEFT || w == WMSZ_TOPLEFT || w == WMSZ_BOTTOMLEFT)
                outer->left = outer->right - size.getWidth() - dx;
            else outer->right = outer->left + size.getWidth() + dx;
            if (w == WMSZ_TOP || w == WMSZ_TOPLEFT || w == WMSZ_TOPRIGHT)
                outer->top = outer->bottom - size.getHeight() - dy;
            else outer->bottom = outer->top + size.getHeight() + dy;
            return TRUE;
        }
        case WM_DPICHANGED: {
            self->scale();
            ViewRect size;
            if (self->view->getSize(&size) == kResultOk) self->resizeView(self->view, &size);
            return 0;
        }
        case WM_SETFOCUS: self->view->onFocus(true); return 0;
        case WM_KILLFOCUS: self->view->onFocus(false); return 0;
        }
        return DefWindowProcW(hwnd, message, w, l);
    }
};
VST3Editor::VST3Editor(IPtr<IPlugView> view, void* owner, bool visible)
    : impl(std::make_unique<Impl>(std::move(view))) {
    auto& h = *impl;
    if (!h.view || h.view->isPlatformTypeSupported(kPlatformTypeHWND) != kResultOk)
        throw std::runtime_error("Plugin does not provide a Windows editor");
    WNDCLASSW wc {};
    wc.lpfnWndProc = Impl::proc; wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW); wc.lpszClassName = L"KJVST3Editor";
    if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        throw std::runtime_error("Cannot register VST3 editor window");
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    if (h.view->canResize() == kResultTrue) style |= WS_THICKFRAME | WS_MAXIMIZEBOX;
    if (!CreateWindowExW(0, wc.lpszClassName, L"VST3 Plugin Editor", style,
                         CW_USEDEFAULT, CW_USEDEFAULT, 640, 480, static_cast<HWND>(owner),
                         nullptr, wc.hInstance, &h))
        throw std::runtime_error("Cannot create VST3 editor window");
    if (h.view->setFrame(&h) != kResultOk) throw std::runtime_error("Plugin rejected editor frame");
    if (h.view->attached(h.window, kPlatformTypeHWND) != kResultOk)
        throw std::runtime_error("Plugin editor attachment failed");
    h.attached = true;
    h.scale();
    ViewRect size;
    if (h.view->getSize(&size) != kResultOk || h.resizeView(h.view, &size) != kResultOk)
        throw std::runtime_error("Plugin editor size negotiation failed");
    if (visible) show();
}
VST3Editor::~VST3Editor() = default;
void VST3Editor::show() { if (impl->window) { ShowWindow(impl->window, SW_SHOWNORMAL); SetForegroundWindow(impl->window); } }
bool VST3Editor::isOpen() const { return impl->window != nullptr; }
void* VST3Editor::nativeWindow() const { return impl->window; }
}
