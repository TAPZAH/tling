#include "offline_translator/clipboard.hpp"

#include <cstring>
#include <stdexcept>
#include <string>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace offline_translator {

ClipboardRestorer::ClipboardRestorer(Clipboard& clipboard)
    : clipboard_(clipboard) {
    try {
        previous_ = clipboard_.get_text();
    } catch (...) {
        previous_.clear();
    }
}

ClipboardRestorer::~ClipboardRestorer() {
    if (!armed_) {
        return;
    }
    try {
        clipboard_.set_text(previous_);
    } catch (...) {
        // Восстановление не должно ронять вызывающий код.
    }
}

const std::wstring& ClipboardRestorer::previous_text() const {
    return previous_;
}

void ClipboardRestorer::disarm() noexcept {
    armed_ = false;
}

bool ClipboardRestorer::armed() const noexcept {
    return armed_;
}

std::wstring capture_selected_text(
    Clipboard& clipboard,
    const std::function<void()>& copy_selection,
    std::wstring_view sentinel,
    bool keep_in_clipboard) {
    ClipboardRestorer restorer(clipboard);
    try {
        clipboard.set_text(sentinel);
        if (copy_selection) {
            copy_selection();
        }
        std::wstring selected;
        try {
            selected = clipboard.get_text();
        } catch (...) {
            selected.clear();
        }
        while (!selected.empty() &&
               (selected.back() == L' ' || selected.back() == L'\t' ||
                selected.back() == L'\r' || selected.back() == L'\n')) {
            selected.pop_back();
        }
        std::size_t begin = 0;
        while (begin < selected.size() &&
               (selected[begin] == L' ' || selected[begin] == L'\t' ||
                selected[begin] == L'\r' || selected[begin] == L'\n')) {
            ++begin;
        }
        if (begin > 0) {
            selected.erase(0, begin);
        }
        if (selected.empty() || selected == sentinel) {
            return {};
        }
        std::wstring current;
        try {
            current = clipboard.get_text();
        } catch (...) {
            current.clear();
        }
        // Пользователь успел скопировать другой текст — не затираем его.
        if (!current.empty() && current != selected && current != sentinel &&
            current != restorer.previous_text()) {
            restorer.disarm();
        }
        if (keep_in_clipboard) {
            restorer.disarm();
        }
        return selected;
    } catch (...) {
        return {};
    }
}

#ifdef _WIN32

Win32Clipboard::Win32Clipboard(void* owner_hwnd)
    : owner_hwnd_(owner_hwnd) {}

namespace {

HWND clipboard_owner(void* owner_hwnd) {
    return owner_hwnd == nullptr ? nullptr : static_cast<HWND>(owner_hwnd);
}

bool open_clipboard_retry(HWND owner) {
    // Дольше ждём буфер: его может держать другая программа (менеджеры
    // буфера, Telegram и т.п.), тогда как раньше сдавались через 80 мс.
    for (int attempt = 0; attempt < 20; ++attempt) {
        if (OpenClipboard(owner)) {
            return true;
        }
        Sleep(25);
    }
    return false;
}

}  // анонимное пространство имён

std::wstring Win32Clipboard::get_text() {
    if (!open_clipboard_retry(clipboard_owner(owner_hwnd_))) {
        return {};
    }
    struct Guard {
        ~Guard() {
            CloseClipboard();
        }
    } guard;
    HANDLE handle = GetClipboardData(CF_UNICODETEXT);
    if (!handle) {
        return {};
    }
    const auto* data = static_cast<const wchar_t*>(GlobalLock(handle));
    if (!data) {
        return {};
    }
    std::wstring result(data);
    GlobalUnlock(handle);
    return result;
}

void Win32Clipboard::set_text(std::wstring_view text) {
    if (!open_clipboard_retry(clipboard_owner(owner_hwnd_))) {
        throw std::runtime_error("Не удалось открыть буфер обмена");
    }
    struct Guard {
        ~Guard() {
            CloseClipboard();
        }
    } guard;
    if (!EmptyClipboard()) {
        throw std::runtime_error("Не удалось очистить буфер обмена");
    }
    const std::size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL handle = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!handle) {
        throw std::runtime_error("Не удалось выделить память буфера обмена");
    }
    auto* dest = static_cast<wchar_t*>(GlobalLock(handle));
    if (!dest) {
        GlobalFree(handle);
        throw std::runtime_error("Не удалось записать буфер обмена");
    }
    if (!text.empty()) {
        memcpy(dest, text.data(), text.size() * sizeof(wchar_t));
    }
    dest[text.size()] = L'\0';
    GlobalUnlock(handle);
    if (!SetClipboardData(CF_UNICODETEXT, handle)) {
        GlobalFree(handle);
        throw std::runtime_error("Не удалось положить текст в буфер обмена");
    }
}

void send_keyboard_shortcut(WORD vk, bool ctrl_already_down) {
    if (ctrl_already_down) {
        INPUT inputs[2]{};
        inputs[0].type = INPUT_KEYBOARD;
        inputs[0].ki.wVk = vk;
        inputs[1].type = INPUT_KEYBOARD;
        inputs[1].ki.wVk = vk;
        inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(2, inputs, sizeof(INPUT));
        return;
    }
    INPUT inputs[4]{};
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = VK_CONTROL;
    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = vk;
    inputs[2].type = INPUT_KEYBOARD;
    inputs[2].ki.wVk = vk;
    inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;
    inputs[3].type = INPUT_KEYBOARD;
    inputs[3].ki.wVk = VK_CONTROL;
    inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(4, inputs, sizeof(INPUT));
}

void send_copy_keyboard_shortcut() {
    const bool ctrl_already_down =
        (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
    send_copy_keyboard_shortcut(ctrl_already_down);
}

void send_copy_keyboard_shortcut(bool ctrl_already_down) {
    send_keyboard_shortcut('C', ctrl_already_down);
}

void send_paste_keyboard_shortcut() {
    const bool ctrl_already_down =
        (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
    send_paste_keyboard_shortcut(ctrl_already_down);
}

void send_paste_keyboard_shortcut(bool ctrl_already_down) {
    send_keyboard_shortcut('V', ctrl_already_down);
}

namespace {
void (*g_capture_wait_hook)(std::uint32_t) = nullptr;

void capture_wait(std::uint32_t milliseconds) {
    if (g_capture_wait_hook) {
        g_capture_wait_hook(milliseconds);
    } else {
        Sleep(milliseconds);
    }
}
}  // namespace

void set_capture_wait_hook(void (*hook)(std::uint32_t milliseconds)) {
    g_capture_wait_hook = hook;
}

std::wstring capture_selected_text_win32(
    void* owner_hwnd,
    bool keep_in_clipboard) {
    Win32Clipboard clipboard(owner_hwnd);
    ClipboardRestorer restorer(clipboard);
    const bool copy_or_paste_now =
        (GetAsyncKeyState('C') & 0x8000) != 0 ||
        (GetAsyncKeyState('V') & 0x8000) != 0;
    if (copy_or_paste_now) {
        restorer.disarm();
        return {};
    }
    const std::wstring sentinel =
        L"__ot_sel_" + std::to_wstring(GetTickCount64()) + L"__";
    clipboard.set_text(sentinel);
    send_copy_keyboard_shortcut();
    // Приложение копирует выделение асинхронно: опрашиваем буфер,
    // пока текст не сменится, но не дольше ~0.6 с.
    std::wstring selected;
    for (int attempt = 0; attempt < 12; ++attempt) {
        capture_wait(50);
        try {
            selected = clipboard.get_text();
        } catch (...) {
            selected.clear();
        }
        if (!selected.empty() && selected != sentinel) {
            break;
        }
    }
    while (!selected.empty() &&
           (selected.back() == L' ' || selected.back() == L'\t' ||
            selected.back() == L'\r' || selected.back() == L'\n')) {
        selected.pop_back();
    }
    std::size_t begin = 0;
    while (begin < selected.size() &&
           (selected[begin] == L' ' || selected[begin] == L'\t' ||
            selected[begin] == L'\r' || selected[begin] == L'\n')) {
        ++begin;
    }
    if (begin > 0) {
        selected.erase(0, begin);
    }
    if (selected.empty() || selected == sentinel) {
        // Неудачный захват: не оставляем служебную метку в буфере
        // пользователя — возвращаем прежнее содержимое.
        try {
            clipboard.set_text(restorer.previous_text());
        } catch (...) {
        }
        return {};
    }
    // Если пользователь в этот момент копирует, оставляем его буфер.
    if ((GetAsyncKeyState('C') & 0x8000) != 0 || keep_in_clipboard) {
        restorer.disarm();
    }
    return selected;
}

#endif

}  // пространство имён offline_translator
