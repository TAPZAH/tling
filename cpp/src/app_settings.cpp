#include "offline_translator/app_settings.hpp"
#include "offline_translator/app_language.hpp"
#include "offline_translator/clipboard_history.hpp"
#include "offline_translator/hotkey.hpp"
#include "offline_translator/selection.hpp"

#include "fs_utils.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
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

std::filesystem::path executable_directory() {
#ifdef _WIN32
    wchar_t buffer[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(
        nullptr,
        buffer,
        MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return {};
    }
    std::filesystem::path path(std::wstring(buffer, length));
    return path.parent_path();
#else
    return {};
#endif
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

std::string read_string(
    const nlohmann::json& data,
    const char* key,
    const std::string& fallback) {
    try {
        if (!data.contains(key) || !data[key].is_string()) {
            return fallback;
        }
        return data[key].get<std::string>();
    } catch (...) {
        return fallback;
    }
}

int read_int(const nlohmann::json& data, const char* key, int fallback) {
    try {
        if (!data.contains(key) || !data[key].is_number()) {
            return fallback;
        }
        return data[key].get<int>();
    } catch (...) {
        return fallback;
    }
}

bool read_bool(const nlohmann::json& data, const char* key, bool fallback) {
    try {
        if (!data.contains(key) || !data[key].is_boolean()) {
            return fallback;
        }
        return data[key].get<bool>();
    } catch (...) {
        return fallback;
    }
}

std::string read_engine(const nlohmann::json& data) {
    const auto engine = read_string(data, "engine", "argos");
    if (engine == "nllb" || engine == "argos" || engine == "firefox" ||
        engine == "marian") {
        return engine;
    }
    return "argos";
}

std::string read_architecture(const nlohmann::json& data) {
    const auto architecture = read_string(data, "architecture", "tiny");
    if (architecture == "tiny" || architecture == "base") {
        return architecture;
    }
    return "tiny";
}

std::string read_ui_language(const nlohmann::json& data) {
    const auto language = read_string(data, "ui_language", "auto");
    if (language == "auto") {
        return language;
    }
    for (const auto& option : ui_language_options()) {
        if (language == option.code) {
            return language;
        }
    }
    return "auto";
}

std::string read_result_window_mode(const nlohmann::json& data) {
    const auto mode = read_string(
        data,
        "result_window_mode",
        std::string{kResultWindowClickToClose});
    if (mode == kResultWindowClickToClose ||
        mode == kResultWindowSelectable) {
        return mode;
    }
    return std::string{kResultWindowClickToClose};
}

std::string read_translate_hotkey(const nlohmann::json& data) {
    const auto spec = read_string(
        data,
        "translate_hotkey",
        std::string{kDefaultTranslateHotkey});
    if (const auto parsed = parse_hotkey(spec)) {
        return format_hotkey(*parsed);
    }
    return std::string{kDefaultTranslateHotkey};
}

std::string read_ui_theme(const nlohmann::json& data) {
    return normalize_ui_theme(read_string(data, "ui_theme", "light"));
}

std::string read_popup_modifier(const nlohmann::json& data) {
    const auto spec = read_string(data, "popup_modifier", "");
    const auto normalized = normalize_popup_modifier(spec);
    if (!spec.empty() && spec == normalized) {
        return normalized;
    }
    if (read_bool(data, "popup_requires_ctrl", false)) {
        return std::string{kPopupModifierCtrl};
    }
    return std::string{kPopupModifierNone};
}

}  // анонимное пространство имён

std::filesystem::path default_data_root() {
    const auto env_home = fs_utils::getenv_string("OFFLINE_TRANSLATOR_HOME");
    if (!env_home.empty()) {
        return std::filesystem::path(env_home);
    }
    const auto portable = executable_directory() / "data";
    try {
        if (std::filesystem::is_directory(portable)) {
            return portable;
        }
    } catch (const std::filesystem::filesystem_error&) {
    }
    return fs_utils::user_home() / ".local" / "share" / "offline-translator";
}

std::filesystem::path default_settings_path() {
    return default_data_root() / "settings.json";
}

AppSettings load_settings() {
    return load_settings(default_settings_path());
}

AppSettings load_settings(const std::filesystem::path& path) {
    AppSettings settings;
    try {
        const auto data = read_json_object(path);
        settings.engine = read_engine(data);
        settings.source_language = read_string(data, "source_language", "en");
        settings.target_language = read_string(data, "target_language", "ru");
        if (settings.source_language.empty()) {
            settings.source_language = "en";
        }
        if (settings.target_language.empty()) {
            settings.target_language = "ru";
        }
        settings.window_width = read_int(data, "window_width", 0);
        settings.window_height = read_int(data, "window_height", 0);
        if (settings.window_width < 0) {
            settings.window_width = 0;
        }
        if (settings.window_height < 0) {
            settings.window_height = 0;
        }
        settings.popup_modifier = read_popup_modifier(data);
        settings.popup_requires_ctrl =
            settings.popup_modifier == kPopupModifierCtrl;
        settings.double_ctrl_c_translation =
            read_bool(data, "double_ctrl_c_translation", false);
        settings.selection_popup_enabled =
            read_bool(data, "selection_popup_enabled", true);
        if (!settings.selection_popup_enabled) {
            settings.double_ctrl_c_translation = true;
        }
        settings.result_window_mode = read_result_window_mode(data);
        settings.translate_hotkey = read_translate_hotkey(data);
        settings.architecture = read_architecture(data);
        settings.ui_language = read_ui_language(data);
        settings.ui_theme = read_ui_theme(data);
        settings.auto_copy_selection =
            read_bool(data, "auto_copy_selection", false);
        settings.turbo_translation =
            read_bool(data, "turbo_translation", false);
        settings.clipboard_history_limit = normalize_clipboard_history_limit(
            read_int(
                data,
                "clipboard_history_limit",
                kDefaultClipboardHistoryLimit));
    } catch (...) {
        return AppSettings{};
    }
    return settings;
}

void save_settings(const AppSettings& settings) {
    save_settings(default_settings_path(), settings);
}

void save_settings(
    const std::filesystem::path& path,
    const AppSettings& settings) {
    nlohmann::json data = read_json_object(path);
    data["engine"] = settings_engine_name(
        engine_kind_from_settings(settings.engine));
    data["source_language"] = settings.source_language;
    data["target_language"] = settings.target_language;
    if (settings.window_width > 0) {
        data["window_width"] = settings.window_width;
    }
    if (settings.window_height > 0) {
        data["window_height"] = settings.window_height;
    }
    const auto popup_modifier =
        normalize_popup_modifier(settings.popup_modifier);
    const bool popup_requires_ctrl = popup_modifier == kPopupModifierCtrl;
    const bool selection_popup_enabled = settings.selection_popup_enabled;
    const bool double_ctrl_c_translation =
        selection_popup_enabled ? settings.double_ctrl_c_translation : true;
    data["popup_requires_ctrl"] = popup_requires_ctrl;
    data["popup_modifier"] = popup_modifier;
    data["double_ctrl_c_translation"] = double_ctrl_c_translation;
    data["selection_popup_enabled"] = selection_popup_enabled;
    data["result_window_mode"] = settings.result_window_mode;
    data["translate_hotkey"] = settings.translate_hotkey;
    if (settings.architecture == "tiny" || settings.architecture == "base") {
        data["architecture"] = settings.architecture;
    }
    data["ui_language"] = settings.ui_language;
    data["ui_theme"] = normalize_ui_theme(settings.ui_theme);
    data["auto_copy_selection"] = settings.auto_copy_selection;
    data["turbo_translation"] = settings.turbo_translation;
    data["clipboard_history_limit"] =
        normalize_clipboard_history_limit(settings.clipboard_history_limit);
    fs_utils::write_text_file(path, data.dump(2) + "\n");
}

EngineKind engine_kind_from_settings(std::string_view engine) {
    if (engine == "nllb") {
        return EngineKind::nllb;
    }
    if (engine == "firefox") {
        return EngineKind::firefox;
    }
    if (engine == "marian") {
        return EngineKind::marian;
    }
    return EngineKind::argos;
}

std::string normalize_ui_theme(std::string_view theme) {
    if (theme == kUiThemeDark) {
        return std::string{kUiThemeDark};
    }
    return std::string{kUiThemeLight};
}

std::string settings_engine_name(EngineKind engine_kind) {
    if (engine_kind == EngineKind::nllb) {
        return "nllb";
    }
    if (engine_kind == EngineKind::argos) {
        return "argos";
    }
    if (engine_kind == EngineKind::firefox) {
        return "firefox";
    }
    if (engine_kind == EngineKind::marian) {
        return "marian";
    }
    throw std::invalid_argument("Неизвестный тип движка");
}

}  // пространство имён offline_translator
