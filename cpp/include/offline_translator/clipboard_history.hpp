#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace offline_translator {

inline constexpr int kDefaultClipboardHistoryLimit = 10;
inline constexpr int kMinClipboardHistoryLimit = 1;
inline constexpr int kMaxClipboardHistoryLimit = 50;

int normalize_clipboard_history_limit(int value);

std::filesystem::path default_clipboard_history_path();

std::vector<std::string> load_clipboard_history();
std::vector<std::string> load_clipboard_history(const std::filesystem::path& path);

void save_clipboard_history(const std::vector<std::string>& items);
void save_clipboard_history(
    const std::filesystem::path& path,
    const std::vector<std::string>& items,
    int limit);

void add_clipboard_history_item(std::string text);
void add_clipboard_history_item(
    const std::filesystem::path& path,
    std::string text,
    int limit);

void clear_clipboard_history();
void clear_clipboard_history(const std::filesystem::path& path);

std::string clipboard_history_preview(
    std::string_view text,
    std::size_t max_chars = 48);

}  // пространство имён offline_translator
