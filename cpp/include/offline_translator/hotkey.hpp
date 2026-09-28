#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace offline_translator {

// Горячая клавиша. vk — виртуальный код Windows (например 'T' или 0x70 для F1).
struct Hotkey {
    bool control{false};
    bool alt{false};
    bool shift{false};
    bool win{false};
    unsigned vk{0};
};

inline constexpr std::string_view kDefaultTranslateHotkey = "Ctrl+Shift+T";

std::optional<Hotkey> parse_hotkey(std::string_view spec);
std::string format_hotkey(const Hotkey& hotkey);

// Битовая маска для RegisterHotKey (MOD_ALT/CONTROL/SHIFT/WIN).
unsigned hotkey_win32_modifiers(const Hotkey& hotkey);

}  // пространство имён offline_translator
