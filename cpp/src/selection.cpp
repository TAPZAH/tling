#include "offline_translator/selection.hpp"

#include <map>
#include <string>

namespace offline_translator {
namespace {

char32_t decode_utf8(std::string_view text, std::size_t& index) {
    if (index >= text.size()) {
        return 0;
    }
    const auto lead = static_cast<unsigned char>(text[index]);
    auto next = [&](int extra, char32_t value) -> char32_t {
        ++index;
        for (int i = 0; i < extra; ++i) {
            if (index >= text.size()) {
                return value;
            }
            const auto cont = static_cast<unsigned char>(text[index]);
            if ((cont & 0xC0) != 0x80) {
                return value;
            }
            value = (value << 6) | (cont & 0x3F);
            ++index;
        }
        return value;
    };
    if (lead < 0x80) {
        ++index;
        return lead;
    }
    if ((lead & 0xE0) == 0xC0) {
        return next(1, lead & 0x1F);
    }
    if ((lead & 0xF0) == 0xE0) {
        return next(2, lead & 0x0F);
    }
    if ((lead & 0xF8) == 0xF0) {
        return next(3, lead & 0x07);
    }
    ++index;
    return lead;
}

bool is_ukrainian(char32_t ch) {
    return ch == U'І' || ch == U'і' || ch == U'Ї' || ch == U'ї' ||
           ch == U'Є' || ch == U'є' || ch == U'Ґ' || ch == U'ґ';
}

bool is_cyrillic(char32_t ch) {
    return (ch >= U'А' && ch <= U'я') || ch == U'Ё' || ch == U'ё' ||
           is_ukrainian(ch);
}

bool is_latin(char32_t ch) {
    return (ch >= U'A' && ch <= U'Z') || (ch >= U'a' && ch <= U'z') ||
           (ch >= U'À' && ch <= U'ÿ');
}

}  // анонимное пространство имён

std::string normalize_popup_modifier(std::string_view modifier) {
    if (modifier == kPopupModifierCtrl || modifier == kPopupModifierAlt ||
        modifier == kPopupModifierShift || modifier == kPopupModifierNone) {
        return std::string(modifier);
    }
    return std::string{kPopupModifierNone};
}

std::string popup_modifier_from_legacy(bool requires_ctrl) {
    return requires_ctrl ? std::string{kPopupModifierCtrl}
                         : std::string{kPopupModifierNone};
}

bool popup_modifier_held(
    std::string_view modifier,
    bool ctrl_pressed,
    bool alt_pressed,
    bool shift_pressed) {
    const auto normalized = normalize_popup_modifier(modifier);
    if (normalized == kPopupModifierCtrl) {
        return ctrl_pressed;
    }
    if (normalized == kPopupModifierAlt) {
        return alt_pressed;
    }
    if (normalized == kPopupModifierShift) {
        return shift_pressed;
    }
    return true;
}

bool should_show_selection_button(
    std::string_view modifier,
    bool ctrl_pressed,
    bool alt_pressed,
    bool shift_pressed) {
    return popup_modifier_held(
        modifier, ctrl_pressed, alt_pressed, shift_pressed);
}

bool should_show_selection_button(bool requires_ctrl, bool ctrl_pressed) {
    return should_show_selection_button(
        popup_modifier_from_legacy(requires_ctrl),
        ctrl_pressed,
        false,
        false);
}

bool should_skip_selection_copy(bool c_pressed, bool v_pressed) {
    return c_pressed || v_pressed;
}

bool should_trigger_double_ctrl_c(
    double previous_press_time,
    double current_press_time,
    bool ctrl_pressed,
    bool enabled) {
    if (!enabled || !ctrl_pressed || previous_press_time <= 0) {
        return false;
    }
    const double elapsed = current_press_time - previous_press_time;
    return elapsed > 0 && elapsed <= kDoubleCtrlCSeconds;
}

bool should_capture_selection(
    bool press_is_client,
    bool window_moved,
    double drag_distance,
    double drag_duration,
    bool is_double_click) {
    if (!press_is_client || window_moved) {
        return false;
    }
    if (drag_distance >= kDragThresholdPx &&
        drag_duration >= kMinDragDurationSeconds) {
        return true;
    }
    return is_double_click;
}

std::string detect_script_language(std::string_view text) {
    std::size_t index = 0;
    int cyrillic_count = 0;
    int latin_count = 0;
    int cjk_count = 0;
    int arabic_count = 0;
    int hangul_count = 0;
    int japanese_count = 0;
    bool has_ukrainian = false;
    while (index < text.size()) {
        const char32_t ch = decode_utf8(text, index);
        if (ch >= 0xAC00 && ch <= 0xD7AF) {
            ++hangul_count;
        } else if (ch >= 0x3040 && ch <= 0x30FF) {
            ++japanese_count;
        } else if (ch >= 0x4E00 && ch <= 0x9FFF) {
            ++cjk_count;
        } else if (ch >= 0x0600 && ch <= 0x06FF) {
            ++arabic_count;
        } else if (is_cyrillic(ch)) {
            ++cyrillic_count;
            if (is_ukrainian(ch)) {
                has_ukrainian = true;
            }
        } else if (is_latin(ch)) {
            ++latin_count;
        }
    }
    const int letter_count = cyrillic_count + latin_count + cjk_count +
        arabic_count + hangul_count + japanese_count;
    if (letter_count == 0) {
        return {};
    }
    if (hangul_count > letter_count * 0.2) {
        return "ko";
    }
    if (japanese_count > letter_count * 0.15) {
        return "ja";
    }
    if (cjk_count > letter_count * 0.3) {
        return "zh";
    }
    if (arabic_count > letter_count * 0.3) {
        return "ar";
    }
    if (cyrillic_count >= latin_count && cyrillic_count > 0) {
        return has_ukrainian ? "uk" : "ru";
    }
    if (latin_count > 0) {
        return "en";
    }
    return {};
}

std::pair<std::string, std::string> choose_selection_direction(
    std::string_view text,
    const std::set<std::string>* installed_codes) {
    const std::string detected = detect_script_language(text);
    if (detected == "ru") {
        return {"ru", "en"};
    }
    std::string source = detected.empty() ? "en" : detected;
    if (installed_codes && !installed_codes->empty() &&
        installed_codes->find(source) == installed_codes->end()) {
        source = "en";
    }
    return {source, "ru"};
}

std::string language_display_name(std::string_view code) {
    static const std::map<std::string, std::string> names{
        {"auto", "Авто"},
        {"en", "Английский"},
        {"ru", "Русский"},
        {"de", "Немецкий"},
        {"fr", "Французский"},
        {"es", "Испанский"},
        {"uk", "Украинский"},
        {"zh", "Китайский"},
        {"ja", "Японский"},
        {"ko", "Корейский"},
        {"ar", "Арабский"},
    };
    const auto found = names.find(std::string(code));
    if (found == names.end()) {
        return std::string(code);
    }
    return found->second;
}

}  // пространство имён offline_translator
