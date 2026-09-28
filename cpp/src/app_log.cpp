#include "offline_translator/app_log.hpp"
#include "offline_translator/app_settings.hpp"

#include "fs_utils.hpp"

#include <chrono>
#include <cstdint>
#include <ctime>
#include <fstream>
#include <mutex>
#include <string>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace offline_translator {
namespace {

constexpr std::uintmax_t kMaxLogBytes = 2 * 1024 * 1024;

std::mutex g_log_mutex;
std::filesystem::path g_log_path;
bool g_log_enabled = false;

std::string format_timestamp() {
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &time);
#else
    localtime_r(&time, &local);
#endif
    char stamp[32]{};
    std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &local);
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(
                            now.time_since_epoch())
                            .count() %
        1000;
    std::string result(stamp);
    result.push_back('.');
    if (millis < 100) {
        result.push_back('0');
    }
    if (millis < 10) {
        result.push_back('0');
    }
    result += std::to_string(static_cast<int>(millis));
    return result;
}

void rotate_if_needed() {
    if (fs_utils::file_size_or_zero(g_log_path) < kMaxLogBytes) {
        return;
    }
    auto backup = g_log_path;
    backup += ".1";
    std::error_code error;
    std::filesystem::remove(backup, error);
    std::filesystem::rename(g_log_path, backup, error);
}

bool append_line(const std::filesystem::path& path, const std::string& line) {
#ifdef _WIN32
    HANDLE file = CreateFileW(
        path.c_str(),
        FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD written = 0;
    const BOOL ok = WriteFile(
        file,
        line.data(),
        static_cast<DWORD>(line.size()),
        &written,
        nullptr);
    CloseHandle(file);
    return ok != FALSE;
#else
    std::ofstream out(path, std::ios::binary | std::ios::app);
    if (!out) {
        return false;
    }
    out << line;
    out.flush();
    return static_cast<bool>(out);
#endif
}

}  // анонимное пространство имён

void init_app_log(const std::filesystem::path& path) {
    std::lock_guard lock(g_log_mutex);
    try {
        g_log_path = path.empty() ? default_data_root() / "app.log" : path;
        std::filesystem::create_directories(g_log_path.parent_path());
        g_log_enabled = true;
    } catch (...) {
        g_log_enabled = false;
        g_log_path.clear();
    }
}

void shutdown_app_log() {
    std::lock_guard lock(g_log_mutex);
    g_log_enabled = false;
}

void app_log(std::string_view level, std::string_view message) {
    std::lock_guard lock(g_log_mutex);
    if (!g_log_enabled || g_log_path.empty()) {
        return;
    }
    try {
        rotate_if_needed();
        const std::string line =
            format_timestamp() + " [" + std::string(level) + "] " +
            std::string(message) + "\n";
        append_line(g_log_path, line);
    } catch (...) {
    }
}

void app_log_info(std::string_view message) {
    app_log("info", message);
}

void app_log_warn(std::string_view message) {
    app_log("warn", message);
}

void app_log_error(std::string_view message) {
    app_log("error", message);
}

std::filesystem::path app_log_path() {
    std::lock_guard lock(g_log_mutex);
    return g_log_path;
}

bool app_log_enabled() {
    std::lock_guard lock(g_log_mutex);
    return g_log_enabled;
}

}  // пространство имён offline_translator
