#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace offline_translator {

// Боевое имя значения Run. Тесты обязаны использовать kAutostartTestValueName.
inline constexpr std::wstring_view kAutostartValueName = L"TLing";
inline constexpr std::wstring_view kAutostartTestValueName =
    L"TLingTest";
inline constexpr std::wstring_view kAutostartRunKey =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

std::filesystem::path current_executable_path();
std::wstring autostart_command_for_exe(const std::filesystem::path& exe);
std::wstring default_autostart_command();

std::optional<std::wstring> get_run_value(std::wstring_view value_name);
void set_run_value(std::wstring_view value_name, std::wstring_view command);
void delete_run_value(std::wstring_view value_name);

bool is_autostart_enabled(std::wstring_view value_name = kAutostartValueName);
void set_autostart(
    bool enabled,
    std::wstring_view value_name,
    std::wstring_view command);

// Боевой автозапуск текущего exe. Не вызывать из тестов.
void set_app_autostart(bool enabled);
bool is_app_autostart_enabled();

}  // пространство имён offline_translator
