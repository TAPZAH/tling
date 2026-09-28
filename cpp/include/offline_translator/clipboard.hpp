#pragma once

#include <functional>
#include <string>
#include <string_view>

namespace offline_translator {

// Абстрактный буфер обмена: живой Win32 или подделка для тестов.
class Clipboard {
public:
    virtual ~Clipboard() = default;
    virtual std::wstring get_text() = 0;
    virtual void set_text(std::wstring_view text) = 0;
};

class MemoryClipboard : public Clipboard {
public:
    std::wstring text;

    std::wstring get_text() override {
        return text;
    }

    void set_text(std::wstring_view value) override {
        text.assign(value.begin(), value.end());
    }
};

#ifdef _WIN32
class Win32Clipboard : public Clipboard {
public:
    explicit Win32Clipboard(void* owner_hwnd = nullptr);
    std::wstring get_text() override;
    void set_text(std::wstring_view text) override;

private:
    void* owner_hwnd_;
};
#endif

// Восстанавливает прежний текст буфера в деструкторе.
class ClipboardRestorer {
public:
    explicit ClipboardRestorer(Clipboard& clipboard);
    ~ClipboardRestorer();
    ClipboardRestorer(const ClipboardRestorer&) = delete;
    ClipboardRestorer& operator=(const ClipboardRestorer&) = delete;

    const std::wstring& previous_text() const;
    void disarm() noexcept;
    bool armed() const noexcept;

private:
    Clipboard& clipboard_;
    std::wstring previous_;
    bool armed_{true};
};

// Копирует выделение через callback (в GUI — SendInput Ctrl+C), затем
// возвращает прежний буфер, если keep_in_clipboard == false.
std::wstring capture_selected_text(
    Clipboard& clipboard,
    const std::function<void()>& copy_selection,
    std::wstring_view sentinel = L"__ot_sel_test__",
    bool keep_in_clipboard = false);

#ifdef _WIN32
void send_copy_keyboard_shortcut();
// Если Ctrl уже нажат пользователем, отпускать его нельзя — ломает Ctrl+V.
void send_copy_keyboard_shortcut(bool ctrl_already_down);
void send_paste_keyboard_shortcut();
void send_paste_keyboard_shortcut(bool ctrl_already_down);
std::wstring capture_selected_text_win32(
    void* owner_hwnd = nullptr,
    bool keep_in_clipboard = false);

// Ожидание внутри capture_selected_text_win32. GUI подменяет на вариант,
// который прокачивает сообщения: иначе инжектированный Ctrl+C не будет
// доставлен сфокусированному окну, пока поток спит.
void set_capture_wait_hook(void (*hook)(std::uint32_t milliseconds));
#endif

}  // пространство имён offline_translator
