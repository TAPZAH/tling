#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace offline_translator::fs_utils {

bool directory_has_entries(const std::filesystem::path& directory);
void remove_tree(const std::filesystem::path& path);
void atomic_replace_directory(
    const std::filesystem::path& staging,
    const std::filesystem::path& installed);
std::filesystem::path user_home();
std::string getenv_string(const char* name);
void write_text_file(const std::filesystem::path& path, std::string_view text);
void write_bytes(
    const std::filesystem::path& path,
    const void* data,
    std::size_t size);
std::uintmax_t file_size_or_zero(const std::filesystem::path& path);
bool is_regular_file(const std::filesystem::path& path);

}  // пространство имён offline_translator::fs_utils
