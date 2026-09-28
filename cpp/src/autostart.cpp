#include "offline_translator/autostart.hpp"

#include <stdexcept>
#include <string>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace offline_translator {
namespace {

#ifdef _WIN32
std::wstring to_wide(std::wstring_view text) {
    return std::wstring(text.begin(), text.end());
}

HKEY open_run_key(REGSAM access) {
    HKEY key = nullptr;
    const LONG status = RegOpenKeyExW(
        HKEY_CURRENT_USER,
        kAutostartRunKey.data(),
        0,
        access,
        &key);
    if (status != ERROR_SUCCESS) {
        throw std::runtime_error(
            "Не удалось открыть ключ автозагрузки Windows");
    }
    return key;
}
#endif

}  // анонимное пространство имён

std::filesystem::path current_executable_path() {
#ifdef _WIN32
    std::wstring buffer(MAX_PATH, L'\0');
    DWORD length = GetModuleFileNameW(nullptr, buffer.data(), MAX_PATH);
    if (length == 0) {
        throw std::runtime_error("Не удалось определить путь к exe");
    }
    if (length >= MAX_PATH) {
        buffer.assign(32768, L'\0');
        length = GetModuleFileNameW(
            nullptr,
            buffer.data(),
            static_cast<DWORD>(buffer.size()));
        if (length == 0 || length >= buffer.size()) {
            throw std::runtime_error("Не удалось определить путь к exe");
        }
    }
    buffer.resize(length);
    return std::filesystem::path(buffer);
#else
    throw std::runtime_error("Автозагрузка поддерживается только в Windows");
#endif
}

std::wstring autostart_command_for_exe(const std::filesystem::path& exe) {
    return L"\"" + exe.wstring() + L"\" --minimized";
}

std::wstring default_autostart_command() {
    return autostart_command_for_exe(current_executable_path());
}

std::optional<std::wstring> get_run_value(std::wstring_view value_name) {
#ifdef _WIN32
    HKEY key = nullptr;
    const LONG opened = RegOpenKeyExW(
        HKEY_CURRENT_USER,
        kAutostartRunKey.data(),
        0,
        KEY_QUERY_VALUE,
        &key);
    if (opened != ERROR_SUCCESS) {
        return std::nullopt;
    }
    const std::wstring name = to_wide(value_name);
    DWORD type = 0;
    DWORD bytes = 0;
    LONG status = RegQueryValueExW(
        key,
        name.c_str(),
        nullptr,
        &type,
        nullptr,
        &bytes);
    if (status != ERROR_SUCCESS || type != REG_SZ || bytes < sizeof(wchar_t)) {
        RegCloseKey(key);
        return std::nullopt;
    }
    std::wstring value(bytes / sizeof(wchar_t), L'\0');
    status = RegQueryValueExW(
        key,
        name.c_str(),
        nullptr,
        &type,
        reinterpret_cast<LPBYTE>(value.data()),
        &bytes);
    RegCloseKey(key);
    if (status != ERROR_SUCCESS) {
        return std::nullopt;
    }
    if (!value.empty() && value.back() == L'\0') {
        value.pop_back();
    }
    while (!value.empty() && value.back() == L'\0') {
        value.pop_back();
    }
    if (value.empty()) {
        return std::nullopt;
    }
    return value;
#else
    (void)value_name;
    return std::nullopt;
#endif
}

void set_run_value(std::wstring_view value_name, std::wstring_view command) {
#ifdef _WIN32
    HKEY key = open_run_key(KEY_SET_VALUE);
    const std::wstring name = to_wide(value_name);
    const std::wstring value = to_wide(command);
    const LONG status = RegSetValueExW(
        key,
        name.c_str(),
        0,
        REG_SZ,
        reinterpret_cast<const BYTE*>(value.c_str()),
        static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    if (status != ERROR_SUCCESS) {
        throw std::runtime_error("Не удалось записать автозагрузку");
    }
#else
    (void)value_name;
    (void)command;
    throw std::runtime_error("Автозагрузка поддерживается только в Windows");
#endif
}

void delete_run_value(std::wstring_view value_name) {
#ifdef _WIN32
    HKEY key = nullptr;
    const LONG opened = RegOpenKeyExW(
        HKEY_CURRENT_USER,
        kAutostartRunKey.data(),
        0,
        KEY_SET_VALUE,
        &key);
    if (opened != ERROR_SUCCESS) {
        return;
    }
    const std::wstring name = to_wide(value_name);
    const LONG status = RegDeleteValueW(key, name.c_str());
    RegCloseKey(key);
    if (status != ERROR_SUCCESS && status != ERROR_FILE_NOT_FOUND) {
        throw std::runtime_error("Не удалось удалить автозагрузку");
    }
#else
    (void)value_name;
#endif
}

bool is_autostart_enabled(std::wstring_view value_name) {
    return get_run_value(value_name).has_value();
}

void set_autostart(
    bool enabled,
    std::wstring_view value_name,
    std::wstring_view command) {
    if (enabled) {
        set_run_value(value_name, command);
        return;
    }
    delete_run_value(value_name);
}

void set_app_autostart(bool enabled) {
    set_autostart(enabled, kAutostartValueName, default_autostart_command());
}

bool is_app_autostart_enabled() {
    return is_autostart_enabled(kAutostartValueName);
}

}  // пространство имён offline_translator
