#include "screen_ocr.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <optional>
#include <vector>

namespace offline_translator {
namespace {

constexpr wchar_t kOverlayClassName[] = L"TLingScreenRegionOverlay";
constexpr int kMinSelectionSize = 4;

HBITMAP create_top_down_dib(int width, int height, void** bits) {
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;  // top-down
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    *bits = nullptr;
    return CreateDIBSection(
        nullptr, &info, DIB_RGB_COLORS, bits, nullptr, 0);
}

struct OverlayState {
    int origin_x{0};
    int origin_y{0};
    int width{0};
    int height{0};
    int scale_hint{1};

    HDC capture_dc{nullptr};
    HBITMAP capture_bitmap{nullptr};
    std::uint8_t* capture_bits{nullptr};

    HDC composite_dc{nullptr};
    HBITMAP composite_bitmap{nullptr};
    std::uint8_t* composite_bits{nullptr};

    HWND window{nullptr};
    bool dragging{false};
    bool has_selection{false};
    POINT start{};
    POINT current{};
    std::optional<ScreenImage> result;
    bool finished{false};
};

OverlayState* g_overlay = nullptr;

// Приложение не объявляет DPI-awareness, поэтому на масштабировании Windows
// растягивает окно оверлея и захват получается «приближённым». На время
// выделения включаем Per-Monitor V2 только для текущего потока.
using SetThreadDpiAwarenessContextFn = void*(WINAPI*)(void*);

SetThreadDpiAwarenessContextFn dpi_awareness_fn() {
    static SetThreadDpiAwarenessContextFn fn =
        reinterpret_cast<SetThreadDpiAwarenessContextFn>(GetProcAddress(
            GetModuleHandleW(L"user32.dll"),
            "SetThreadDpiAwarenessContext"));
    return fn;
}

class ThreadDpiGuard {
public:
    ThreadDpiGuard() {
        const auto fn = dpi_awareness_fn();
        if (fn != nullptr) {
            // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 == (HANDLE)-4.
            previous_ = fn(reinterpret_cast<void*>(static_cast<std::intptr_t>(-4)));
            active_ = previous_ != nullptr;
        }
    }
    ~ThreadDpiGuard() {
        if (active_) {
            const auto fn = dpi_awareness_fn();
            if (fn != nullptr) {
                fn(previous_);
            }
        }
    }
    ThreadDpiGuard(const ThreadDpiGuard&) = delete;
    ThreadDpiGuard& operator=(const ThreadDpiGuard&) = delete;

private:
    void* previous_{nullptr};
    bool active_{false};
};

RECT selection_rect(const OverlayState& state) {
    RECT rect{};
    const int left = std::min(state.start.x, state.current.x);
    const int top = std::min(state.start.y, state.current.y);
    const int right = std::max(state.start.x, state.current.x);
    const int bottom = std::max(state.start.y, state.current.y);
    rect.left = std::clamp(left, 0, state.width);
    rect.top = std::clamp(top, 0, state.height);
    rect.right = std::clamp(right, 0, state.width);
    rect.bottom = std::clamp(bottom, 0, state.height);
    return rect;
}

void composite_frame(OverlayState& state) {
    const std::size_t pixels =
        static_cast<std::size_t>(state.width) * state.height;
    if (state.capture_bits != nullptr && state.composite_bits != nullptr) {
        auto* src = state.capture_bits;
        auto* dst = state.composite_bits;
        for (std::size_t i = 0; i < pixels; ++i) {
            dst[i * 4 + 0] = static_cast<std::uint8_t>(src[i * 4 + 0] / 2);
            dst[i * 4 + 1] = static_cast<std::uint8_t>(src[i * 4 + 1] / 2);
            dst[i * 4 + 2] = static_cast<std::uint8_t>(src[i * 4 + 2] / 2);
            dst[i * 4 + 3] = 255;
        }
    }
    if (state.has_selection && state.capture_bits != nullptr &&
        state.composite_bits != nullptr) {
        RECT rect = selection_rect(state);
        const int width = rect.right - rect.left;
        const int height = rect.bottom - rect.top;
        if (width > 0 && height > 0) {
            for (int y = 0; y < height; ++y) {
                const std::size_t offset =
                    (static_cast<std::size_t>(rect.top + y) * state.width +
                     rect.left) *
                    4;
                std::memcpy(
                    state.composite_bits + offset,
                    state.capture_bits + offset,
                    static_cast<std::size_t>(width) * 4);
            }
        }
    }
}

void paint_overlay(OverlayState& state, HDC dc) {
    composite_frame(state);
    BitBlt(
        dc,
        0,
        0,
        state.width,
        state.height,
        state.composite_dc,
        0,
        0,
        SRCCOPY);
    if (state.has_selection) {
        RECT rect = selection_rect(state);
        if (rect.right > rect.left && rect.bottom > rect.top) {
            HPEN pen = CreatePen(PS_SOLID, 2, RGB(0, 160, 255));
            HGDIOBJ old_pen = SelectObject(dc, pen);
            HGDIOBJ old_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
            Rectangle(dc, rect.left, rect.top, rect.right, rect.bottom);
            SelectObject(dc, old_brush);
            SelectObject(dc, old_pen);
            DeleteObject(pen);
        }
    }
}

void finish_overlay(OverlayState& state, int scale) {
    RECT rect = selection_rect(state);
    if (rect.right - rect.left >= kMinSelectionSize &&
        rect.bottom - rect.top >= kMinSelectionSize) {
        const int src_w = rect.right - rect.left;
        const int src_h = rect.bottom - rect.top;
        const int out_w = src_w * scale;
        const int out_h = src_h * scale;
        void* out_bits = nullptr;
        HBITMAP out_bitmap = create_top_down_dib(out_w, out_h, &out_bits);
        if (out_bitmap != nullptr && out_bits != nullptr) {
            HDC out_dc = CreateCompatibleDC(nullptr);
            HGDIOBJ old = SelectObject(out_dc, out_bitmap);
            SetStretchBltMode(out_dc, HALFTONE);
            SetBrushOrgEx(out_dc, 0, 0, nullptr);
            StretchBlt(
                out_dc,
                0,
                0,
                out_w,
                out_h,
                state.capture_dc,
                rect.left,
                rect.top,
                src_w,
                src_h,
                SRCCOPY);
            GdiFlush();
            ScreenImage image;
            image.width = out_w;
            image.height = out_h;
            const auto* begin = static_cast<const std::uint8_t*>(out_bits);
            image.bgra.assign(
                begin,
                begin + static_cast<std::size_t>(out_w) * out_h * 4);
            state.result = std::move(image);
            SelectObject(out_dc, old);
            DeleteDC(out_dc);
        }
        if (out_bitmap != nullptr) {
            DeleteObject(out_bitmap);
        }
    }
    state.finished = true;
    if (state.window != nullptr) {
        DestroyWindow(state.window);
    }
}

LRESULT CALLBACK overlay_proc(
    HWND window,
    UINT message,
    WPARAM w_param,
    LPARAM l_param) {
    OverlayState* state = g_overlay;
    switch (message) {
        case WM_PAINT: {
            PAINTSTRUCT paint{};
            HDC dc = BeginPaint(window, &paint);
            if (state != nullptr) {
                paint_overlay(*state, dc);
            }
            EndPaint(window, &paint);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_SETCURSOR:
            SetCursor(LoadCursorA(nullptr, IDC_CROSS));
            return TRUE;
        case WM_LBUTTONDOWN:
            if (state != nullptr) {
                state->dragging = true;
                state->has_selection = false;
                state->start.x = GET_X_LPARAM(l_param);
                state->start.y = GET_Y_LPARAM(l_param);
                state->current = state->start;
                SetCapture(window);
            }
            return 0;
        case WM_MOUSEMOVE:
            if (state != nullptr && state->dragging) {
                state->current.x = GET_X_LPARAM(l_param);
                state->current.y = GET_Y_LPARAM(l_param);
                state->has_selection = true;
                InvalidateRect(window, nullptr, FALSE);
            }
            return 0;
        case WM_LBUTTONUP:
            if (state != nullptr && state->dragging) {
                state->dragging = false;
                ReleaseCapture();
                state->current.x = GET_X_LPARAM(l_param);
                state->current.y = GET_Y_LPARAM(l_param);
                state->has_selection = true;
                finish_overlay(*state, state->scale_hint);
            }
            return 0;
        case WM_RBUTTONDOWN:
        case WM_MBUTTONDOWN:
            if (state != nullptr) {
                state->finished = true;
                DestroyWindow(window);
            }
            return 0;
        case WM_KEYDOWN:
            if (w_param == VK_ESCAPE && state != nullptr) {
                state->finished = true;
                DestroyWindow(window);
            }
            return 0;
        case WM_DESTROY:
            if (state != nullptr) {
                state->window = nullptr;
            }
            return 0;
        default:
            break;
    }
    return DefWindowProcW(window, message, w_param, l_param);
}

void register_overlay_class(HINSTANCE instance) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = overlay_proc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorA(nullptr, IDC_CROSS);
    wc.lpszClassName = kOverlayClassName;
    wc.style = CS_HREDRAW | CS_VREDRAW;
    RegisterClassExW(&wc);
}

}  // namespace

std::optional<ScreenImage> select_screen_region_image(int scale) {
    ThreadDpiGuard dpi_guard;
    if (scale < 1) {
        scale = 1;
    }
    const int origin_x = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int origin_y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (width <= 0 || height <= 0) {
        return std::nullopt;
    }

    OverlayState state;
    state.origin_x = origin_x;
    state.origin_y = origin_y;
    state.width = width;
    state.height = height;
    state.scale_hint = scale;

    state.capture_bitmap = create_top_down_dib(
        width, height, reinterpret_cast<void**>(&state.capture_bits));
    state.composite_bitmap = create_top_down_dib(
        width, height, reinterpret_cast<void**>(&state.composite_bits));
    if (state.capture_bitmap == nullptr || state.composite_bitmap == nullptr) {
        if (state.capture_bitmap != nullptr) {
            DeleteObject(state.capture_bitmap);
        }
        if (state.composite_bitmap != nullptr) {
            DeleteObject(state.composite_bitmap);
        }
        return std::nullopt;
    }
    state.capture_dc = CreateCompatibleDC(nullptr);
    state.composite_dc = CreateCompatibleDC(nullptr);
    HGDIOBJ old_capture = SelectObject(state.capture_dc, state.capture_bitmap);
    HGDIOBJ old_composite =
        SelectObject(state.composite_dc, state.composite_bitmap);

    HDC screen = GetDC(nullptr);
    if (screen == nullptr) {
        SelectObject(state.capture_dc, old_capture);
        SelectObject(state.composite_dc, old_composite);
        DeleteDC(state.capture_dc);
        DeleteDC(state.composite_dc);
        DeleteObject(state.capture_bitmap);
        DeleteObject(state.composite_bitmap);
        return std::nullopt;
    }
    BitBlt(
        state.capture_dc,
        0,
        0,
        width,
        height,
        screen,
        origin_x,
        origin_y,
        SRCCOPY);
    GdiFlush();
    ReleaseDC(nullptr, screen);

    HINSTANCE instance = GetModuleHandleW(nullptr);
    register_overlay_class(instance);
    g_overlay = &state;
    state.window = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        kOverlayClassName,
        L"",
        WS_POPUP,
        origin_x,
        origin_y,
        width,
        height,
        nullptr,
        nullptr,
        instance,
        nullptr);
    if (state.window == nullptr) {
        g_overlay = nullptr;
        SelectObject(state.capture_dc, old_capture);
        SelectObject(state.composite_dc, old_composite);
        DeleteDC(state.capture_dc);
        DeleteDC(state.composite_dc);
        DeleteObject(state.capture_bitmap);
        DeleteObject(state.composite_bitmap);
        return std::nullopt;
    }
    ShowWindow(state.window, SW_SHOW);
    SetForegroundWindow(state.window);
    SetFocus(state.window);

    MSG message{};
    while (!state.finished) {
        if (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) {
                break;
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        } else {
            WaitMessage();
        }
    }

    g_overlay = nullptr;
    SelectObject(state.capture_dc, old_capture);
    SelectObject(state.composite_dc, old_composite);
    DeleteDC(state.capture_dc);
    DeleteDC(state.composite_dc);
    DeleteObject(state.capture_bitmap);
    DeleteObject(state.composite_bitmap);
    return state.result;
}

}  // namespace offline_translator
