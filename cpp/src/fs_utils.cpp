#include "fs_utils.hpp"

#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <string>
#ifdef _WIN32
#include <stdlib.h>
#endif

namespace offline_translator::fs_utils {

bool directory_has_entries(const std::filesystem::path& directory) {
    try {
        if (!std::filesystem::is_directory(directory)) {
            return false;
        }
        return std::filesystem::directory_iterator(directory) !=
               std::filesystem::directory_iterator();
    } catch (const std::filesystem::filesystem_error&) {
        return false;
    }
}

void remove_tree(const std::filesystem::path& path) {
    try {
        if (std::filesystem::exists(path)) {
            std::filesystem::remove_all(path);
        }
    } catch (const std::filesystem::filesystem_error& error) {
        throw std::runtime_error(
            std::string("Не удалось удалить «") + path.string() + "»: " +
            error.what());
    }
}

void atomic_replace_directory(
    const std::filesystem::path& staging,
    const std::filesystem::path& installed) {
    try {
        if (std::filesystem::exists(installed)) {
            std::filesystem::remove_all(installed);
        }
        std::filesystem::create_directories(installed.parent_path());
        std::filesystem::rename(staging, installed);
    } catch (const std::filesystem::filesystem_error& error) {
        throw std::runtime_error(
            std::string("Не удалось атомарно установить «") +
            installed.string() + "»: " + error.what());
    }
}

std::string getenv_string(const char* name) {
#ifdef _WIN32
    char* buffer = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&buffer, &length, name) != 0 || buffer == nullptr) {
        return {};
    }
    std::string value(buffer);
    free(buffer);
    return value;
#else
    const char* value = std::getenv(name);
    return value == nullptr ? std::string{} : std::string(value);
#endif
}

std::filesystem::path user_home() {
#ifdef _WIN32
    const auto profile = getenv_string("USERPROFILE");
#else
    const auto profile = getenv_string("HOME");
#endif
    if (profile.empty()) {
        throw std::runtime_error("Не удалось определить домашний каталог");
    }
    return std::filesystem::path(profile);
}

void write_text_file(const std::filesystem::path& path, std::string_view text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        throw std::runtime_error("Не удалось записать файл: " + path.string());
    }
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!out) {
        throw std::runtime_error("Ошибка записи файла: " + path.string());
    }
}

void write_bytes(
    const std::filesystem::path& path,
    const void* data,
    std::size_t size) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        throw std::runtime_error("Не удалось записать файл: " + path.string());
    }
    if (size > 0) {
        out.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
    }
    if (!out) {
        throw std::runtime_error("Ошибка записи файла: " + path.string());
    }
}

std::uintmax_t file_size_or_zero(const std::filesystem::path& path) {
    try {
        if (std::filesystem::is_regular_file(path)) {
            return std::filesystem::file_size(path);
        }
    } catch (const std::filesystem::filesystem_error&) {
    }
    return 0;
}

bool is_regular_file(const std::filesystem::path& path) {
    try {
        return std::filesystem::is_regular_file(path);
    } catch (const std::filesystem::filesystem_error&) {
        return false;
    }
}

}  // пространство имён offline_translator::fs_utils
