#include "offline_translator/hotkey.hpp"

#include <cctype>
#include <sstream>
#include <vector>

namespace offline_translator {
namespace {

std::string trim_copy(std::string_view text) {
    std::size_t begin = 0;
    while (begin < text.size() &&
           std::isspace(static_cast<unsigned char>(text[begin]))) {
        ++begin;
    }
    std::size_t end = text.size();
    while (end > begin &&
           std::isspace(static_cast<unsigned char>(text[end - 1]))) {
        --end;
    }
    return std::string(text.substr(begin, end - begin));
}

std::string to_lower(std::string text) {
    for (char& ch : text) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return text;
}

std::vector<std::string> split_plus(std::string_view spec) {
    std::vector<std::string> parts;
    std::string current;
    for (char ch : spec) {
        if (ch == '+') {
            parts.push_back(trim_copy(current));
            current.clear();
        } else {
            current.push_back(ch);
        }
    }
    parts.push_back(trim_copy(current));
    return parts;
}

std::optional<unsigned> parse_key(const std::string& token) {
    if (token.empty()) {
        return std::nullopt;
    }
    if (token.size() == 1) {
        const unsigned char ch = static_cast<unsigned char>(token[0]);
        if (std::isalnum(ch)) {
            return static_cast<unsigned>(std::toupper(ch));
        }
        return std::nullopt;
    }
    if (token[0] == 'f' || token[0] == 'F') {
        // Номер F-клавиши разбираем вручную: std::stoi бросил бы
        // std::out_of_range на «F999999999», а это значение приходит из
        // settings.json и приводило к сбросу всех настроек.
        if (token.size() >= 2 && token.size() <= 3) {
            int number = 0;
            bool digits = true;
            for (std::size_t i = 1; i < token.size(); ++i) {
                if (!std::isdigit(static_cast<unsigned char>(token[i]))) {
                    digits = false;
                    break;
                }
                number = number * 10 + (token[i] - '0');
            }
            if (digits && number >= 1 && number <= 24) {
                return 0x70u + static_cast<unsigned>(number - 1);
            }
        }
    }
    return std::nullopt;
}

}  // анонимное пространство имён

std::optional<Hotkey> parse_hotkey(std::string_view spec) {
    const auto parts = split_plus(spec);
    Hotkey hotkey;
    bool have_key = false;
    for (const auto& part : parts) {
        if (part.empty()) {
            return std::nullopt;
        }
        const std::string lower = to_lower(part);
        if (lower == "ctrl" || lower == "control") {
            hotkey.control = true;
            continue;
        }
        if (lower == "alt") {
            hotkey.alt = true;
            continue;
        }
        if (lower == "shift") {
            hotkey.shift = true;
            continue;
        }
        if (lower == "win" || lower == "windows" || lower == "meta") {
            hotkey.win = true;
            continue;
        }
        if (have_key) {
            return std::nullopt;
        }
        const auto vk = parse_key(part);
        if (!vk) {
            return std::nullopt;
        }
        hotkey.vk = *vk;
        have_key = true;
    }
    if (!have_key) {
        return std::nullopt;
    }
    return hotkey;
}

std::string format_hotkey(const Hotkey& hotkey) {
    std::ostringstream out;
    bool first = true;
    auto append = [&](std::string_view token) {
        if (!first) {
            out << '+';
        }
        first = false;
        out << token;
    };
    if (hotkey.win) {
        append("Win");
    }
    if (hotkey.control) {
        append("Ctrl");
    }
    if (hotkey.alt) {
        append("Alt");
    }
    if (hotkey.shift) {
        append("Shift");
    }
    if (hotkey.vk >= 0x70 && hotkey.vk <= 0x87) {
        append("F" + std::to_string(hotkey.vk - 0x70 + 1));
    } else if (hotkey.vk >= 'A' && hotkey.vk <= 'Z') {
        append(std::string(1, static_cast<char>(hotkey.vk)));
    } else if (hotkey.vk >= '0' && hotkey.vk <= '9') {
        append(std::string(1, static_cast<char>(hotkey.vk)));
    } else if (hotkey.vk != 0) {
        append("Vk" + std::to_string(hotkey.vk));
    }
    return out.str();
}

unsigned hotkey_win32_modifiers(const Hotkey& hotkey) {
    unsigned modifiers = 0;
    if (hotkey.alt) {
        modifiers |= 0x0001;  // MOD_ALT
    }
    if (hotkey.control) {
        modifiers |= 0x0002;  // MOD_CONTROL
    }
    if (hotkey.shift) {
        modifiers |= 0x0004;  // MOD_SHIFT
    }
    if (hotkey.win) {
        modifiers |= 0x0008;  // MOD_WIN
    }
    return modifiers;
}

}  // пространство имён offline_translator
