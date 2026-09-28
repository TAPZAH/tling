#include "offline_translator/clipboard_history.hpp"
#include "offline_translator/app_settings.hpp"

#include "fs_utils.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>

namespace offline_translator {
namespace {

std::string compact_preview(std::string_view text) {
    std::string compact;
    bool pending_space = false;
    for (char ch : text) {
        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
            pending_space = !compact.empty();
            continue;
        }
        if (pending_space) {
            compact.push_back(' ');
            pending_space = false;
        }
        compact.push_back(ch);
    }
    return compact;
}

nlohmann::json read_json_object(const std::filesystem::path& path) {
    nlohmann::json data = nlohmann::json::object();
    try {
        if (!std::filesystem::is_regular_file(path)) {
            return data;
        }
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            return data;
        }
        nlohmann::json parsed = nlohmann::json::parse(in, nullptr, false);
        if (parsed.is_discarded() || !parsed.is_object()) {
            return data;
        }
        return parsed;
    } catch (...) {
        return nlohmann::json::object();
    }
}

std::vector<std::string> read_items(const nlohmann::json& data) {
    std::vector<std::string> items;
    try {
        if (!data.contains("items") || !data["items"].is_array()) {
            return items;
        }
        for (const auto& item : data["items"]) {
            if (!item.is_string()) {
                continue;
            }
            auto text = item.get<std::string>();
            if (text.empty()) {
                continue;
            }
            items.push_back(std::move(text));
        }
    } catch (...) {
        return {};
    }
    return items;
}

}  // анонимное пространство имён

int normalize_clipboard_history_limit(int value) {
    if (value < kMinClipboardHistoryLimit) {
        return kMinClipboardHistoryLimit;
    }
    if (value > kMaxClipboardHistoryLimit) {
        return kMaxClipboardHistoryLimit;
    }
    return value;
}

std::filesystem::path default_clipboard_history_path() {
    return default_data_root() / "clipboard_history.json";
}

std::vector<std::string> load_clipboard_history() {
    return load_clipboard_history(default_clipboard_history_path());
}

std::vector<std::string> load_clipboard_history(
    const std::filesystem::path& path) {
    return read_items(read_json_object(path));
}

void save_clipboard_history(const std::vector<std::string>& items) {
    const auto settings = load_settings();
    save_clipboard_history(
        default_clipboard_history_path(),
        items,
        normalize_clipboard_history_limit(settings.clipboard_history_limit));
}

void save_clipboard_history(
    const std::filesystem::path& path,
    const std::vector<std::string>& items,
    int limit) {
    const int normalized = normalize_clipboard_history_limit(limit);
    nlohmann::json data = nlohmann::json::object();
    data["items"] = nlohmann::json::array();
    int stored = 0;
    for (const auto& item : items) {
        if (item.empty()) {
            continue;
        }
        data["items"].push_back(item);
        ++stored;
        if (stored >= normalized) {
            break;
        }
    }
    fs_utils::write_text_file(path, data.dump(2) + "\n");
}

void add_clipboard_history_item(std::string text) {
    if (text.empty()) {
        return;
    }
    const auto settings = load_settings();
    add_clipboard_history_item(
        default_clipboard_history_path(),
        std::move(text),
        normalize_clipboard_history_limit(settings.clipboard_history_limit));
}

void add_clipboard_history_item(
    const std::filesystem::path& path,
    std::string text,
    int limit) {
    if (text.empty()) {
        return;
    }
    auto items = load_clipboard_history(path);
    items.erase(
        std::remove(items.begin(), items.end(), text),
        items.end());
    items.insert(items.begin(), std::move(text));
    save_clipboard_history(path, items, limit);
}

void clear_clipboard_history() {
    clear_clipboard_history(default_clipboard_history_path());
}

void clear_clipboard_history(const std::filesystem::path& path) {
    save_clipboard_history(path, {}, kDefaultClipboardHistoryLimit);
}

std::string clipboard_history_preview(
    std::string_view text,
    std::size_t max_chars) {
    auto compact = compact_preview(text);
    if (compact.empty()) {
        return "(пусто)";
    }
    if (max_chars == 0 || compact.size() <= max_chars) {
        return compact;
    }
    if (max_chars == 1) {
        return "…";
    }
    std::size_t cut = max_chars - 1;
    while (cut > 0 &&
           (static_cast<unsigned char>(compact[cut]) & 0xC0) == 0x80) {
        --cut;
    }
    compact.resize(cut);
    compact += "…";
    return compact;
}

}  // пространство имён offline_translator
