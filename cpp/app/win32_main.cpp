#include "offline_translator/app_log.hpp"
#include "offline_translator/app_language.hpp"
#include "offline_translator/app_settings.hpp"
#include "offline_translator/app_update.hpp"
#include "offline_translator/app_version.hpp"
#include "offline_translator/argos_model_manager.hpp"
#include "offline_translator/autostart.hpp"
#include "offline_translator/clipboard.hpp"
#include "offline_translator/clipboard_history.hpp"
#include "offline_translator/document_export.hpp"
#include "offline_translator/document_layout.hpp"
#include "offline_translator/excel_cells.hpp"
#include "offline_translator/firefox_model_manager.hpp"
#include "offline_translator/marian_model_manager.hpp"
#include "offline_translator/hotkey.hpp"
#include "offline_translator/language_store.hpp"
#include "offline_translator/nllb_model_manager.hpp"
#include "offline_translator/language_detect.hpp"
#include "offline_translator/ocr_engine.hpp"
#include "offline_translator/paddle_ocr.hpp"
#include "offline_translator/route_planner.hpp"
#include "offline_translator/selection.hpp"
#include "offline_translator/translation_application.hpp"
#include "offline_translator/window_policy.hpp"

#include "screen_ocr.hpp"
#include "excel_com.hpp"
#include "image_file.hpp"
#include "image_render.hpp"
#include "pdf_file.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef EM_SETCUEBANNER
#define EM_SETCUEBANNER 0x1501
#endif
#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>
#include <gdiplus.h>
#include <dwmapi.h>
#include <uxtheme.h>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "uxtheme.lib")

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cwchar>
#include <exception>
#include <filesystem>
#include <memory>
#include <numeric>
#include <mutex>
#include <optional>
#include <set>
#include <unordered_map>
#include <stdexcept>
#include <fstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

constexpr UINT kTranslateMessage = WM_APP + 1;
constexpr UINT kStatusMessage = WM_APP + 2;
constexpr UINT kPackageProgressMessage = WM_APP + 3;
constexpr UINT kPackageDoneMessage = WM_APP + 4;
constexpr UINT kPackageIndexMessage = WM_APP + 5;
constexpr UINT kTrayMessage = WM_APP + 10;
constexpr UINT kSelectionResultMessage = WM_APP + 11;
constexpr UINT kRecoverInputMessage = WM_APP + 20;
constexpr UINT kDropdownOpenedMessage = WM_APP + 24;
constexpr UINT kUpdateCheckDoneMessage = WM_APP + 21;
constexpr UINT kUpdateInstallDoneMessage = WM_APP + 22;
constexpr UINT kStartupUpdateCheckDoneMessage = WM_APP + 23;
constexpr UINT kExcelResultMessage = WM_APP + 25;
constexpr UINT kImageResultMessage = WM_APP + 26;
constexpr UINT kSelectionPollTimer = 1;
constexpr UINT kSelectionButtonHideTimer = 2;
constexpr UINT kStartupUpdateTimer = 3;
constexpr int kEngineCombo = 1001;
constexpr int kSourceEdit = 1002;
constexpr int kTranslateButton = 1003;
constexpr int kResultEdit = 1004;
constexpr int kSourceLanguageCombo = 1005;
constexpr int kTargetLanguageCombo = 1006;
constexpr int kFallbackTargetCombo = 1011;
constexpr int kPackagesButton = 1007;
constexpr int kStatusLabel = 1008;
constexpr int kSettingsButton = 1009;
constexpr int kPackageList = 1101;
constexpr int kPackageInstallButton = 1102;
constexpr int kPackageUninstallButton = 1103;
constexpr int kPackageCloseButton = 1104;
constexpr int kPackageSearchEdit = 1105;
constexpr int kPackageArchCombo = 1106;
constexpr int kSettingsPopupCtrl = 1201;
constexpr int kSettingsDoubleCtrlC = 1202;
constexpr int kSettingsClickToClose = 1203;
constexpr int kSettingsSelectable = 1204;
constexpr int kSettingsAutostart = 1205;
constexpr int kSettingsSave = 1207;
constexpr int kSettingsCancel = 1208;
constexpr int kSettingsPopupModifier = 1209;
constexpr int kSettingsOnlyCtrlCC = 1210;
constexpr int kSettingsThemeLight = 1211;
constexpr int kSettingsThemeDark = 1212;
constexpr int kSettingsAutoCopy = 1213;
constexpr int kSettingsHistoryLimit = 1214;
constexpr int kSettingsCheckUpdate = 1215;
constexpr int kSettingsInstallUpdate = 1216;
constexpr int kSettingsOpenReleases = 1217;
constexpr int kSettingsNavLanguages = 1218;
constexpr int kSettingsNavBehavior = 1219;
constexpr int kSettingsNavThemes = 1220;
constexpr int kSettingsNavLanguage = 1221;
constexpr int kSettingsNavAbout = 1222;
constexpr int kSettingsPackagesButton = 1226;
constexpr int kSettingsEngineCombo = 1227;
constexpr int kSettingsUiLanguage = 1228;
constexpr int kSettingsGoogleKey = 1229;
constexpr int kSettingsCheckUpdatesStartup = 1230;
constexpr int kSettingsAppendSeparator = 1231;
constexpr int kSettingsExcelEnabled = 1232;
constexpr int kSettingsExcelHotkey = 1233;
constexpr int kSettingsExcelMethod = 1234;
constexpr int kSettingsExcelTrigger = 1235;
constexpr int kSettingsOcrEngine = 1236;
constexpr int kSettingsOcrOnly = 1237;
constexpr int kResultCopyButton = 1302;
constexpr int kResultCloseButton = 1303;
constexpr int kResultReplaceButton = 1304;
constexpr int kResultAddButton = 1305;
constexpr int kResultOriginalButton = 1306;
constexpr int kUpdateYesButton = 1401;
constexpr int kUpdateNoButton = 1402;
constexpr int kScreenOcrButton = 1010;
constexpr int kImageButton = 1012;
constexpr int kScreenOcrHotkey = 2002;
constexpr int kExcelHotkey = 2003;
constexpr int kTrayOpen = 3001;
constexpr int kTrayAutostart = 3002;
constexpr int kTrayExit = 3003;
constexpr int kTrayCheckUpdate = 3004;
constexpr int kTrayAutoCopy = 3005;
constexpr int kTrayTurbo = 3006;
constexpr int kTrayScreenOcr = 3007;
constexpr int kTrayImageFile = 3008;
constexpr int kTrayImageRegion = 3009;
constexpr int kTrayFloatButton = 3010;
constexpr int kTrayOcrOnly = 3011;
constexpr int kTrayHistoryRoot = 3097;
constexpr int kTrayHistoryEmpty = 3098;
constexpr int kTrayHistoryClear = 3099;
constexpr int kTrayHistoryFirst = 3100;
constexpr int kTrayHistoryLast = 3149;
constexpr int kTrayHistoryTranslateFirst = 3200;
constexpr int kTrayHistoryTranslateLast = 3249;
constexpr int kTrayHistoryPreviewFirst = 3300;
constexpr int kDefaultWindowWidth = 780;
constexpr int kDefaultWindowHeight = 450;
constexpr int kMinWindowWidth = 750;
constexpr int kMinWindowHeight = 400;

HWND g_engine_combo = nullptr;
HWND g_source_edit = nullptr;
HWND g_translate_button = nullptr;
HWND g_screen_ocr_button = nullptr;
HWND g_image_button = nullptr;
HWND g_float_button = nullptr;
HWND g_float_tooltip = nullptr;
bool g_float_button_uses_icon = false;
HWND g_result_edit = nullptr;
HWND g_source_language_combo = nullptr;
HWND g_target_language_combo = nullptr;
HWND g_fallback_target_language_combo = nullptr;
HWND g_fallback_target_label = nullptr;
HWND g_both_directions_label = nullptr;
HWND g_packages_button = nullptr;
HWND g_settings_button = nullptr;
HWND g_status_label = nullptr;
HWND g_package_list = nullptr;
HWND g_package_install = nullptr;
HWND g_package_uninstall = nullptr;
HWND g_package_status = nullptr;
HWND g_package_search = nullptr;
std::wstring g_pending_package_filter;
HWND g_packages_parent = nullptr;
HWND g_package_arch_caption = nullptr;
HWND g_package_caption = nullptr;
HWND g_package_arch_combo = nullptr;
HWND g_settings_popup_ctrl = nullptr;
HWND g_settings_popup_modifier = nullptr;
HWND g_settings_only_ctrl_c_c = nullptr;
HWND g_settings_double_ctrl_c = nullptr;
HWND g_settings_click_to_close = nullptr;
HWND g_settings_selectable = nullptr;
HWND g_settings_autostart = nullptr;
HWND g_settings_theme_light = nullptr;
HWND g_settings_theme_dark = nullptr;
HWND g_settings_auto_copy = nullptr;
HWND g_settings_history_limit = nullptr;
HWND g_settings_check_update = nullptr;
HWND g_settings_install_update = nullptr;
HWND g_settings_open_releases = nullptr;
HWND g_settings_update_status = nullptr;
HWND g_settings_engine_combo = nullptr;
HWND g_settings_behavior_summary = nullptr;
HWND g_settings_ui_language = nullptr;
HWND g_settings_google_key = nullptr;
HWND g_settings_check_updates_startup = nullptr;
HWND g_settings_append_separator = nullptr;
HWND g_settings_excel_enabled = nullptr;
HWND g_settings_excel_hotkey = nullptr;
HWND g_settings_excel_method = nullptr;
HWND g_settings_excel_trigger = nullptr;
HWND g_settings_ocr_engine = nullptr;
HWND g_settings_ocr_only = nullptr;
bool g_ocr_only_session = false;
std::wstring g_hotkey_capture_backup;
std::atomic<bool> g_update_busy{false};
std::optional<offline_translator::UpdateInfo> g_pending_update;
HWND g_selection_button = nullptr;
HWND g_result_popup = nullptr;
HWND g_popup_result_edit = nullptr;
HWND g_popup_copy_button = nullptr;
HWND g_popup_replace_button = nullptr;
HWND g_popup_add_button = nullptr;
HWND g_popup_original_button = nullptr;
HWND g_popup_header = nullptr;
std::wstring g_popup_source_text;
std::wstring g_popup_translation_text;
bool g_popup_showing_original = false;
HWND g_update_popup = nullptr;
HWND g_update_popup_text = nullptr;
HWND g_update_popup_yes = nullptr;
HWND g_update_popup_no = nullptr;
bool g_update_popup_disabled_owner = false;
std::wstring g_last_clipboard_error;
// Режим OCR: в попапе скрывается кнопка «Замена» (нет исходного окна).
bool g_screen_ocr_mode = false;
HMENU g_tray_history_popup = nullptr;
std::vector<std::wstring> g_tray_history_items;
std::vector<std::wstring> g_tray_history_labels;
bool g_button_uses_icon = false;
constexpr int kSelectionIconSize = 40;

struct UiTheme {
    COLORREF background;
    COLORREF border;
    COLORREF text;
    COLORREF header;
    COLORREF button_face;
    COLORREF button_hover;
    COLORREF button_text;
};

bool is_dark_theme();
UiTheme current_ui_theme();
HBRUSH theme_background_brush();

bool g_smoke_mode = false;
bool g_start_minimized = false;
bool g_tray_added = false;
HICON g_tray_icon = nullptr;
NOTIFYICONDATAW g_tray_data{};
std::atomic<std::uint64_t> g_last_poll_tick{0};
std::atomic<std::uint64_t> g_last_heartbeat_tick{0};
std::uint64_t g_app_start_tick = 0;

struct StatusPayload {
    std::wstring text;
    bool failed{false};
    std::string source_code;
    std::string target_code;
};

struct UpdateCheckPayload {
    std::wstring text;
    std::optional<offline_translator::UpdateInfo> info;
    bool failed{false};
};

struct UpdateInstallPayload {
    std::wstring text;
    bool failed{false};
    bool quit{false};
};

void sel_log(const std::string& line);
void hide_update_popup();
void show_tray_balloon(
    const std::wstring& title,
    const std::wstring& text,
    bool warning);
// Онлайн-движок (Google) не использует модели.
bool is_online_engine(offline_translator::EngineKind kind);
std::string runtime_flags();
void store_runtime_settings(const offline_translator::AppSettings& settings);
template <typename Fn>
void update_runtime_settings(Fn&& mutate);

struct PackageRow {
    bool nllb{false};
    bool firefox{false};
    bool marian{false};
    std::string architecture;
    std::string from_code;
    std::string to_code;
    std::wstring title;
    bool installed{false};
    bool incomplete{false};
};

struct GuiRuntime {
    std::mutex mutex;
    std::mutex settings_mutex;
    offline_translator::TranslationSession session;
    offline_translator::AppSettings settings;
    std::atomic<bool> busy{false};
    std::atomic<bool> closing{false};
    std::atomic<bool> packages_busy{false};
    std::atomic<bool> selection_busy{false};
    std::atomic<bool> excel_busy{false};
    std::atomic<bool> image_busy{false};
    HWND main_window{nullptr};
    HWND packages_window{nullptr};
    HWND settings_window{nullptr};
    std::vector<PackageRow> package_rows;
    std::vector<PackageRow> package_rows_all;
};

struct SelectionMonitor {
    bool press_active{false};
    bool was_pressed{false};
    bool was_c_pressed{false};
    bool press_is_client{false};
    bool gesture_invalid{false};
    int press_x{0};
    int press_y{0};
    int last_up_x{0};
    int last_up_y{0};
    HWND press_hwnd{nullptr};
    HWND last_up_hwnd{nullptr};
    POINT press_origin{};
    double press_time{0};
    double last_up_time{0};
    double last_capture_time{0};
    double last_ctrl_c_time{0};
    bool pending_ctrl_c_c{false};
    double ctrl_c_c_ready_at{0};
    std::wstring selected_text;
    HWND replace_hwnd{nullptr};
};

SelectionMonitor g_selection;

std::shared_ptr<GuiRuntime> g_runtime;

std::string to_utf8(const std::wstring& text) {
    if (text.empty()) {
        return {};
    }
    const int size = WideCharToMultiByte(
        CP_UTF8,
        0,
        text.data(),
        static_cast<int>(text.size()),
        nullptr,
        0,
        nullptr,
        nullptr);
    std::string result(size, '\0');
    WideCharToMultiByte(
        CP_UTF8,
        0,
        text.data(),
        static_cast<int>(text.size()),
        result.data(),
        size,
        nullptr,
        nullptr);
    return result;
}

std::wstring from_utf8(const std::string& text) {
    if (text.empty()) {
        return {};
    }
    const int size = MultiByteToWideChar(
        CP_UTF8,
        0,
        text.data(),
        static_cast<int>(text.size()),
        nullptr,
        0);
    std::wstring result(size, L'\0');
    MultiByteToWideChar(
        CP_UTF8,
        0,
        text.data(),
        static_cast<int>(text.size()),
        result.data(),
        size);
    return result;
}

std::wstring control_text(HWND control) {
    const int length = GetWindowTextLengthW(control);
    std::wstring text(length + 1, L'\0');
    GetWindowTextW(control, text.data(), length + 1);
    text.resize(length);
    return text;
}

std::wstring user_profile() {
    wchar_t buffer[MAX_PATH]{};
    const DWORD length = GetEnvironmentVariableW(
        L"USERPROFILE",
        buffer,
        static_cast<DWORD>(std::size(buffer)));
    return length == 0 ? L"." : std::wstring(buffer, length);
}

std::wstring executable_directory() {
    std::array<wchar_t, MAX_PATH> buffer{};
    const DWORD length = GetModuleFileNameW(
        nullptr,
        buffer.data(),
        static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) {
        return {};
    }
    std::wstring path(buffer.data(), length);
    const auto separator = path.find_last_of(L"\\/");
    return separator == std::wstring::npos ? L"." : path.substr(0, separator);
}

std::filesystem::path executable_path() {
    std::array<wchar_t, MAX_PATH> buffer{};
    const DWORD length = GetModuleFileNameW(
        nullptr,
        buffer.data(),
        static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) {
        return {};
    }
    return std::filesystem::path(std::wstring(buffer.data(), length));
}

std::wstring model_root(bool use_nllb) {
    const std::wstring portable_root = executable_directory() + L"\\data\\" +
        (use_nllb ? L"nllb-200" : L"argos-packages");
    if (GetFileAttributesW(portable_root.c_str()) != INVALID_FILE_ATTRIBUTES) {
        return portable_root;
    }
    const std::wstring profile = user_profile();
    return use_nllb
        ? profile + L"\\.local\\share\\offline-translator\\nllb-200"
        : profile + L"\\.local\\share\\argos-translate\\packages";
}

std::filesystem::path model_root_path(bool use_nllb) {
    return std::filesystem::path(model_root(use_nllb));
}

std::wstring firefox_model_root() {
    const std::wstring portable_root =
        executable_directory() + L"\\data\\firefox-models";
    if (GetFileAttributesW(portable_root.c_str()) != INVALID_FILE_ATTRIBUTES) {
        return portable_root;
    }
    const std::wstring profile = user_profile();
    return profile + L"\\.local\\share\\offline-translator\\firefox-models";
}

std::wstring marian_model_root() {
    const std::wstring portable_data = executable_directory() + L"\\data";
    if (GetFileAttributesW(portable_data.c_str()) != INVALID_FILE_ATTRIBUTES) {
        return portable_data + L"\\marian-models";
    }
    const std::wstring profile = user_profile();
    return profile + L"\\.local\\share\\offline-translator\\marian-models";
}

std::filesystem::path model_root_for_kind(
    offline_translator::EngineKind kind) {
    switch (kind) {
        case offline_translator::EngineKind::nllb:
            return model_root_path(true);
        case offline_translator::EngineKind::firefox:
            return std::filesystem::path(firefox_model_root());
        case offline_translator::EngineKind::marian:
            return std::filesystem::path(marian_model_root());
        case offline_translator::EngineKind::google:
            // Онлайн-движку модели не нужны.
            return {};
        case offline_translator::EngineKind::argos:
            break;
    }
    return model_root_path(false);
}

// Ключ онлайн-сервиса для выбранного движка (пусто для офлайн-движков).
std::string online_api_key_for_kind(offline_translator::EngineKind kind) {
    if (!g_runtime) {
        return {};
    }
    if (kind == offline_translator::EngineKind::google) {
        return g_runtime->settings.google_api_key;
    }
    return {};
}

std::wstring clipboard_text() {
    offline_translator::Win32Clipboard clipboard;
    return clipboard.get_text();
}

// Языки, осмысленные для выбранного движка: для парных движков — только
// те, для которых есть модели; у NLLB одна многоязычная модель, поэтому
// доступен весь список. Используется как ограничение для автоопределения,
// чтобы короткая фраза не «угадывалась» как язык без моделей.
std::vector<std::string> available_language_codes(
    offline_translator::EngineKind engine_kind) {
    std::vector<std::string> codes;
    try {
        if (engine_kind == offline_translator::EngineKind::nllb ||
            is_online_engine(engine_kind)) {
            // NLLB — одна многоязычная модель, онлайн-сервисы — без моделей:
            // доступны все языки списка.
            for (const auto& entry : offline_translator::supported_languages()) {
                codes.push_back(entry.code);
            }
            return codes;
        }
        const auto root = model_root_for_kind(engine_kind);
        std::vector<offline_translator::PackageInfo> packages;
        if (engine_kind == offline_translator::EngineKind::argos) {
            packages =
                offline_translator::ArgosModelManager::installed_packages(root);
        } else if (engine_kind == offline_translator::EngineKind::firefox) {
            packages = offline_translator::FirefoxModelManager::
                           installed_packages(root);
        } else if (engine_kind == offline_translator::EngineKind::marian) {
            packages = offline_translator::MarianModelManager::
                           installed_packages(root);
        }
        for (const auto& package : packages) {
            if (!package.from_code.empty()) {
                codes.push_back(package.from_code);
            }
            if (!package.to_code.empty()) {
                codes.push_back(package.to_code);
            }
        }
    } catch (const std::exception&) {
    }
    if (codes.empty()) {
        for (const auto& entry : offline_translator::supported_languages()) {
            codes.push_back(entry.code);
        }
    }
    return codes;
}

// Автоопределение языка текста: сначала настоящий детектор (whatlang через
// fxbridge), при неудаче — определение по письму. Пустой результат означает,
// что язык определить не удалось.
std::string detect_language_optional(
    const std::string& text,
    offline_translator::EngineKind engine_kind) {
    std::string detected = offline_translator::detect_language_code(
        text,
        available_language_codes(engine_kind));
    if (detected.empty()) {
        detected = offline_translator::detect_script_language(text);
    }
    return detected;
}

std::string detect_source_language(
    const std::string& text,
    offline_translator::EngineKind engine_kind) {
    std::string detected = detect_language_optional(text, engine_kind);
    if (detected.empty()) {
        detected = "en";
    }
    return detected;
}

void hide_result_popup();

void copy_text_to_clipboard(HWND owner, const std::wstring& text) {
    if (text.empty()) {
        return;
    }
    // Владелец буфера — живое главное окно: если владельцем сделать попап,
    // Windows очистит буфер при закрытии попапа, и вставка не сработает.
    const HWND clipboard_owner = (g_runtime && g_runtime->main_window)
        ? g_runtime->main_window
        : owner;
    g_last_clipboard_error.clear();
    try {
        offline_translator::Win32Clipboard clipboard(clipboard_owner);
        clipboard.set_text(text);
        std::wstring check;
        try {
            check = clipboard.get_text();
        } catch (const std::exception&) {
        }
        offline_translator::app_log_info(
            "clipboard: set len=" + std::to_string(text.size()) +
            " readback=" + std::to_string(check.size()) +
            (check == text ? " ok" : " mismatch"));
        if (check != text) {
            g_last_clipboard_error =
                from_utf8("содержимое буфера не совпало после записи");
        }
    } catch (const std::exception& error) {
        g_last_clipboard_error = from_utf8(error.what());
        offline_translator::app_log_info(
            std::string("clipboard: ошибка записи: ") + error.what());
    }
    try {
        offline_translator::add_clipboard_history_item(to_utf8(text));
    } catch (const std::exception&) {
    }
}

void focus_foreign_window(HWND hwnd) {
    if (!hwnd || !IsWindow(hwnd)) {
        return;
    }
    AllowSetForegroundWindow(ASFW_ANY);
    const DWORD current_thread = GetCurrentThreadId();
    DWORD target_pid = 0;
    const DWORD target_thread = GetWindowThreadProcessId(hwnd, &target_pid);
    bool attached = false;
    if (target_thread && target_thread != current_thread) {
        attached = AttachThreadInput(current_thread, target_thread, TRUE) != FALSE;
    }
    if (IsIconic(hwnd)) {
        ShowWindow(hwnd, SW_RESTORE);
    }
    BringWindowToTop(hwnd);
    SetForegroundWindow(hwnd);
    // Ждём фактической активации, иначе инжектированный Ctrl+V уйдёт
    // не в то окно и вставки не будет.
    const HWND target_root = GetAncestor(hwnd, GA_ROOT);
    const ULONGLONG deadline = GetTickCount64() + 500;
    while (GetTickCount64() < deadline) {
        const HWND foreground = GetForegroundWindow();
        if (foreground == hwnd ||
            (foreground && GetAncestor(foreground, GA_ROOT) == target_root)) {
            break;
        }
        Sleep(20);
    }
    if (attached) {
        AttachThreadInput(current_thread, target_thread, FALSE);
    }
}

void replace_source_with_translation(HWND owner, const std::wstring& text) {
    if (text.empty() || text.rfind(L"Ошибка:", 0) == 0) {
        return;
    }
    const HWND target = g_selection.replace_hwnd;
    // Сначала убираем попап: буфер обмена, записанный владельцем-попапом,
    // очистился бы вместе с окном. Копируем после закрытия, владельцем —
    // живое главное окно.
    hide_result_popup();
    copy_text_to_clipboard(
        g_runtime ? g_runtime->main_window : owner, text);
    offline_translator::app_log_info(
        "replace: target=" + std::to_string(
            reinterpret_cast<uintptr_t>(target)) +
        " len=" + std::to_string(text.size()));
    if (target && IsWindow(target)) {
        focus_foreign_window(target);
    }
    Sleep(120);
    // Классическим полям (Edit/RichEdit/Scintilla) вставляем напрямую
    // через WM_PASTE в сфокусированный контрол целевого потока — это не
    // зависит от активации окна. Остальным (браузеры и т.п.) — Ctrl+V.
    bool pasted = false;
    {
        const HWND foreground = GetForegroundWindow();
        const DWORD thread = foreground
            ? GetWindowThreadProcessId(foreground, nullptr)
            : 0;
        GUITHREADINFO info{sizeof(GUITHREADINFO)};
        if (thread && GetGUIThreadInfo(thread, &info) && info.hwndFocus &&
            info.hwndFocus != foreground) {
            wchar_t class_name[64]{};
            GetClassNameW(info.hwndFocus, class_name, 64);
            const bool classic_edit =
                _wcsicmp(class_name, L"Edit") == 0 ||
                _wcsicmp(class_name, L"Scintilla") == 0 ||
                _wcsnicmp(class_name, L"RichEdit", 8) == 0;
            if (classic_edit) {
                SendMessageW(info.hwndFocus, WM_PASTE, 0, 0);
                pasted = true;
            }
        }
    }
    if (!pasted) {
        try {
            offline_translator::send_paste_keyboard_shortcut();
        } catch (const std::exception&) {
        }
    }
    Sleep(400);
    {
        const bool ctrl_down =
            (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
        const HWND foreground = GetForegroundWindow();
        const DWORD thread = GetWindowThreadProcessId(foreground, nullptr);
        GUITHREADINFO info{sizeof(GUITHREADINFO)};
        GetGUIThreadInfo(thread, &info);
        std::wstring clip;
        try {
            clip = clipboard_text();
        } catch (const std::exception&) {
        }
        offline_translator::app_log_info(
            "replace: pasted=" + std::to_string(pasted) +
            " ctrl_down=" + std::to_string(ctrl_down) +
            " fg=" + std::to_string(
                reinterpret_cast<uintptr_t>(foreground)) +
            " focus=" + std::to_string(
                reinterpret_cast<uintptr_t>(info.hwndFocus)) +
            " focus_class=" + [&] {
                wchar_t cls[64]{};
                if (info.hwndFocus) {
                    GetClassNameW(info.hwndFocus, cls, 64);
                }
                return to_utf8(cls);
            }() +
            " clip_len=" + std::to_string(clip.size()));
    }
}

std::wstring trim_trailing_whitespace(std::wstring text) {
    while (!text.empty() &&
           (text.back() == L' ' || text.back() == L'\n' ||
            text.back() == L'\r' || text.back() == L'\t')) {
        text.pop_back();
    }
    return text;
}

// «Добавить»: к оригиналу приписывается перевод через выбранный в
// настройках разделитель, готовый текст вставляется поверх выделения —
// так же, как «Замена».
void add_source_with_translation(
    HWND owner,
    const std::wstring& original,
    const std::wstring& translation,
    const std::wstring& separator) {
    if (translation.empty() || translation.rfind(L"Ошибка:", 0) == 0) {
        return;
    }
    std::wstring combined = trim_trailing_whitespace(original);
    if (combined.empty()) {
        return;
    }
    combined += separator;
    combined += translation;
    offline_translator::app_log_info(
        "add: len=" + std::to_string(combined.size()));
    replace_source_with_translation(owner, combined);
}

double monotonic_seconds() {
    using clock = std::chrono::steady_clock;
    static const auto epoch = clock::now();
    return std::chrono::duration<double>(clock::now() - epoch).count();
}

int popup_modifier_combo_index(std::string_view modifier) {
    if (modifier == offline_translator::kPopupModifierCtrl) {
        return 1;
    }
    if (modifier == offline_translator::kPopupModifierAlt) {
        return 2;
    }
    if (modifier == offline_translator::kPopupModifierShift) {
        return 3;
    }
    return 0;
}

std::string popup_modifier_from_combo(HWND combo) {
    const LRESULT index = SendMessageW(combo, CB_GETCURSEL, 0, 0);
    if (index == 1) {
        return std::string{offline_translator::kPopupModifierCtrl};
    }
    if (index == 2) {
        return std::string{offline_translator::kPopupModifierAlt};
    }
    if (index == 3) {
        return std::string{offline_translator::kPopupModifierShift};
    }
    return std::string{offline_translator::kPopupModifierNone};
}

void fill_popup_modifier_combo(HWND combo, std::string_view selected) {
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Не требуется"));
    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Ctrl"));
    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Alt"));
    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Shift"));
    SendMessageW(
        combo,
        CB_SETCURSEL,
        popup_modifier_combo_index(selected),
        0);
}

int append_separator_combo_index(std::string_view separator) {
    return separator == offline_translator::kAppendSeparatorDash ? 1 : 0;
}

std::string append_separator_from_combo(HWND combo) {
    const LRESULT index = SendMessageW(combo, CB_GETCURSEL, 0, 0);
    if (index == 1) {
        return std::string{offline_translator::kAppendSeparatorDash};
    }
    return std::string{offline_translator::kAppendSeparatorSlash};
}

void fill_append_separator_combo(HWND combo, std::string_view selected) {
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L" / "));
    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L" - "));
    SendMessageW(
        combo,
        CB_SETCURSEL,
        append_separator_combo_index(selected),
        0);
}

bool key_down(int virtual_key) {
    return (GetAsyncKeyState(virtual_key) & 0x8000) != 0;
}

int ocr_engine_combo_index(std::string_view engine) {
    if (engine == offline_translator::kOcrEngineTesseract) {
        return 1;
    }
    if (engine == offline_translator::kOcrEnginePaddle) {
        return 2;
    }
    return 0;
}

std::string ocr_engine_from_combo(HWND combo) {
    const LRESULT index = SendMessageW(combo, CB_GETCURSEL, 0, 0);
    if (index == 1) {
        return std::string{offline_translator::kOcrEngineTesseract};
    }
    if (index == 2) {
        return std::string{offline_translator::kOcrEnginePaddle};
    }
    return std::string{offline_translator::kOcrEngineAuto};
}

void fill_ocr_engine_combo(HWND combo, std::string_view selected) {
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    SendMessageW(
        combo,
        CB_ADDSTRING,
        0,
        reinterpret_cast<LPARAM>(L"Автоматически (лучший доступный)"));
    SendMessageW(
        combo,
        CB_ADDSTRING,
        0,
        reinterpret_cast<LPARAM>(L"Tesseract"));
    SendMessageW(
        combo,
        CB_ADDSTRING,
        0,
        reinterpret_cast<LPARAM>(L"PaddleOCR (PP-OCR)"));
    SendMessageW(combo, CB_SETCURSEL, ocr_engine_combo_index(selected), 0);
}

void apply_ocr_backend_from_settings(
    const offline_translator::AppSettings& settings) {
    const std::filesystem::path ppocr_root =
        offline_translator::default_ppocr_root();
    offline_translator::OcrBackend backend =
        offline_translator::OcrBackend::Tesseract;
    if (settings.ocr_engine == offline_translator::kOcrEnginePaddle) {
        backend = offline_translator::OcrBackend::Paddle;
    } else if (
        settings.ocr_engine == offline_translator::kOcrEngineAuto &&
        offline_translator::ppocr_installed(ppocr_root)) {
        backend = offline_translator::OcrBackend::Paddle;
    }
    offline_translator::set_ocr_backend(backend, ppocr_root);
    offline_translator::app_log_info(
        std::string("OCR backend: ") +
        (backend == offline_translator::OcrBackend::Paddle ? "paddle"
                                                           : "tesseract") +
        " mode=" + settings.ocr_engine +
        " root=" + ppocr_root.string());
}

std::size_t language_count() {
    return offline_translator::supported_languages().size();
}

// Порядок отображения языков: по алфавиту русских названий, чтобы нужный
// язык легко находился (в таблице коды идут по популярности).
const std::vector<std::size_t>& language_display_order() {
    static const std::vector<std::size_t> order = [] {
        const auto& languages = offline_translator::supported_languages();
        std::vector<std::size_t> result(languages.size());
        std::iota(result.begin(), result.end(), std::size_t{0});
        std::sort(
            result.begin(),
            result.end(),
            [&languages](std::size_t left, std::size_t right) {
                return languages[left].name < languages[right].name;
            });
        return result;
    }();
    return order;
}

const std::vector<std::wstring>& combo_language_names() {
    static const std::vector<std::wstring> names = [] {
        const auto& languages = offline_translator::supported_languages();
        std::vector<std::wstring> result;
        result.reserve(languages.size());
        for (const auto index : language_display_order()) {
            result.push_back(from_utf8(languages[index].name));
        }
        return result;
    }();
    return names;
}

int language_index(std::string_view code) {
    const auto& languages = offline_translator::supported_languages();
    const auto& order = language_display_order();
    for (std::size_t position = 0; position < order.size(); ++position) {
        if (languages[order[position]].code == code) {
            return static_cast<int>(position);
        }
    }
    return -1;
}

std::string selected_language(HWND combo) {
    const LRESULT index = SendMessageW(combo, CB_GETCURSEL, 0, 0);
    if (index < 0 || index >= static_cast<LRESULT>(language_count())) {
        throw std::runtime_error("Не выбран язык");
    }
    return offline_translator::supported_languages()[
        language_display_order()[static_cast<std::size_t>(index)]].code;
}

offline_translator::EngineKind selected_engine() {
    const LRESULT index = SendMessageW(g_engine_combo, CB_GETCURSEL, 0, 0);
    if (index == 1) {
        return offline_translator::EngineKind::nllb;
    }
    if (index == 2) {
        return offline_translator::EngineKind::firefox;
    }
    if (index == 3) {
        return offline_translator::EngineKind::marian;
    }
    if (index == 4) {
        return offline_translator::EngineKind::google;
    }
    return offline_translator::EngineKind::argos;
}

// Онлайн-движки не используют модели: их «пакеты» не нужны.
bool is_online_engine(offline_translator::EngineKind kind) {
    return kind == offline_translator::EngineKind::google;
}

int engine_combo_index(offline_translator::EngineKind kind) {
    switch (kind) {
        case offline_translator::EngineKind::nllb:
            return 1;
        case offline_translator::EngineKind::firefox:
            return 2;
        case offline_translator::EngineKind::marian:
            return 3;
        case offline_translator::EngineKind::google:
            return 4;
        case offline_translator::EngineKind::argos:
            break;
    }
    return 0;
}

void select_language(HWND combo, const std::string& code, int fallback) {
    const int index = language_index(code);
    SendMessageW(
        combo,
        CB_SETCURSEL,
        index >= 0 ? index : fallback,
        0);
}

// «С языка»: первым пунктом идёт «Авто» — язык определяется по тексту.
constexpr std::string_view kAutoSourceLanguage = "auto";
bool g_auto_source_language = false;

int source_language_index(std::string_view code) {
    if (code == kAutoSourceLanguage) {
        return 0;
    }
    const int index = language_index(code);
    return index >= 0 ? index + 1 : -1;
}

std::string selected_source_language(HWND combo) {
    const LRESULT index = SendMessageW(combo, CB_GETCURSEL, 0, 0);
    if (index == 0) {
        return std::string(kAutoSourceLanguage);
    }
    if (index < 0 ||
        index > static_cast<LRESULT>(language_count())) {
        throw std::runtime_error("Не выбран язык");
    }
    return offline_translator::supported_languages()[
        language_display_order()[static_cast<std::size_t>(index) - 1]].code;
}

void select_source_language(HWND combo, const std::string& code) {
    const int index = source_language_index(code);
    SendMessageW(combo, CB_SETCURSEL, index >= 0 ? index : 0, 0);
}

void set_status(const std::wstring& text) {
    if (g_status_label) {
        const std::wstring shown = offline_translator::tr(text);
        SetWindowTextW(g_status_label, shown.c_str());
    }
}

// Локализация интерфейса: запоминаем исходные (русские) подписи контролов
// и подставляем перевод текущего языка. Повторный вызов переводит уже
// переведённые окна из запомненных оригиналов.
extern HWND g_settings_panel;
extern HWND g_settings_nav[];

std::unordered_map<HWND, std::wstring> g_ui_originals;
std::unordered_map<HWND, std::vector<std::wstring>> g_ui_combo_originals;

// Обрезка пробелов по краям (для ключей API из полей ввода).
std::string trim_spaces(std::string text) {
    while (!text.empty() &&
           (text.front() == ' ' || text.front() == '\t' ||
            text.front() == '\r' || text.front() == '\n')) {
        text.erase(text.begin());
    }
    while (!text.empty() &&
           (text.back() == ' ' || text.back() == '\t' ||
            text.back() == '\r' || text.back() == '\n')) {
        text.pop_back();
    }
    return text;
}

void update_behavior_summary() {
    if (!g_settings_behavior_summary || !IsWindow(g_settings_behavior_summary)) {
        return;
    }
    const auto checked = [](HWND control) {
        return control &&
            SendMessageW(control, BM_GETCHECK, 0, 0) == BST_CHECKED;
    };
    const bool only_hotkey = checked(g_settings_only_ctrl_c_c);
    const bool double_ctrl_c = checked(g_settings_double_ctrl_c);
    std::wstring hold = L"без клавиши удержания";
    if (g_settings_popup_modifier) {
        const std::string modifier =
            popup_modifier_from_combo(g_settings_popup_modifier);
        if (modifier == offline_translator::kPopupModifierCtrl) {
            hold = L"если удерживать Ctrl";
        } else if (modifier == offline_translator::kPopupModifierAlt) {
            hold = L"если удерживать Alt";
        } else if (modifier == offline_translator::kPopupModifierShift) {
            hold = L"если удерживать Shift";
        }
    }
    std::vector<std::wstring> keys;
    if (only_hotkey) {
        keys.push_back(L"Кнопка у курсора после выделения выключена");
        keys.push_back(L"Перевод запускается только по Ctrl+C+C");
    } else {
        keys.push_back(
            L"Кнопка у курсора после выделения: " + hold);
        if (double_ctrl_c || only_hotkey) {
            keys.push_back(L"Дополнительно работает перевод по Ctrl+C+C");
        } else {
            keys.push_back(L"Перевод по Ctrl+C+C выключен");
        }
    }
    const bool turbo = g_runtime && g_runtime->settings.turbo_translation;
    keys.push_back(
        turbo ? L"Турбо перевод включён" : L"Турбо перевод выключен");
    if (checked(g_settings_selectable)) {
        keys.push_back(L"Окно результата: можно выделить текст, закрыть кнопкой");
    } else {
        keys.push_back(L"Окно результата: закрывается нажатием по окну");
    }
    keys.push_back(
        checked(g_settings_autostart)
            ? L"Запуск вместе с Windows включён"
            : L"Запуск вместе с Windows выключен");

    // Строки собираем из ключей и переводим по одной: так работает
    // переключение языка интерфейса без пересоздания окна.
    std::wstring russian;
    std::wstring visible;
    for (std::size_t index = 0; index < keys.size(); ++index) {
        if (index > 0) {
            russian += L"\r\n";
            visible += L"\r\n";
        }
        russian += keys[index];
        visible += L"• " + offline_translator::tr(keys[index]);
    }
    g_ui_originals[g_settings_behavior_summary] = russian;
    SetWindowTextW(g_settings_behavior_summary, visible.c_str());
}

void localize_control_text(HWND control) {
    if (!control || !IsWindow(control)) {
        return;
    }
    wchar_t class_name[32]{};
    GetClassNameW(control, class_name, 32);
    if (_wcsicmp(class_name, L"ComboBox") == 0) {
        auto& originals = g_ui_combo_originals[control];
        const LRESULT count = SendMessageW(control, CB_GETCOUNT, 0, 0);
        if (originals.empty() && count > 0) {
            for (LRESULT index = 0; index < count; ++index) {
                const LRESULT length =
                    SendMessageW(control, CB_GETLBTEXTLEN, index, 0);
                if (length < 0) {
                    continue;
                }
                std::wstring item(static_cast<std::size_t>(length) + 1, L'\0');
                SendMessageW(
                    control,
                    CB_GETLBTEXT,
                    index,
                    reinterpret_cast<LPARAM>(item.data()));
                item.resize(static_cast<std::size_t>(length));
                originals.push_back(item);
            }
        }
        if (!originals.empty()) {
            const LRESULT selected = SendMessageW(control, CB_GETCURSEL, 0, 0);
            SendMessageW(control, CB_RESETCONTENT, 0, 0);
            for (const auto& item : originals) {
                const std::wstring translated = offline_translator::tr(item);
                SendMessageW(
                    control,
                    CB_ADDSTRING,
                    0,
                    reinterpret_cast<LPARAM>(translated.c_str()));
            }
            if (selected >= 0) {
                SendMessageW(control, CB_SETCURSEL, selected, 0);
            }
        }
        return;
    }
    const std::wstring text = control_text(control);
    if (text.empty()) {
        return;
    }
    auto found = g_ui_originals.find(control);
    if (found == g_ui_originals.end()) {
        found = g_ui_originals.emplace(control, text).first;
    }
    const std::wstring translated = offline_translator::tr(found->second);
    if (translated != text) {
        SetWindowTextW(control, translated.c_str());
    }
}

void localize_window(HWND window) {
    if (!window || !IsWindow(window)) {
        return;
    }
    const std::wstring title = control_text(window);
    if (!title.empty()) {
        auto found = g_ui_originals.find(window);
        if (found == g_ui_originals.end()) {
            found = g_ui_originals.emplace(window, title).first;
        }
        const std::wstring translated = offline_translator::tr(found->second);
        if (translated != title) {
            SetWindowTextW(window, translated.c_str());
        }
    }
    EnumChildWindows(
        window,
        [](HWND child, LPARAM) -> BOOL {
            localize_control_text(child);
            return TRUE;
        },
        0);
}

// Переводит все уже созданные окна приложения на текущий язык.
void relocalize_all() {
    if (g_runtime) {
        localize_window(g_runtime->main_window);
        localize_window(g_runtime->settings_window);
        localize_window(g_runtime->packages_window);
        localize_window(g_result_popup);
    }
    if (g_settings_panel) {
        EnumChildWindows(
            g_settings_panel,
            [](HWND child, LPARAM) -> BOOL {
                localize_control_text(child);
                return TRUE;
            },
            0);
    }
    // Сводка поведения собирается из ключей и переводится целиком, поэтому
    // обновляем её после общего прохода локализации.
    update_behavior_summary();
}

LRESULT localized_message_box(
    HWND owner,
    const wchar_t* text,
    const wchar_t* title,
    UINT flags) {
    return MessageBoxW(
        owner,
        offline_translator::tr(text).c_str(),
        offline_translator::tr(title).c_str(),
        flags);
}

void post_payload(HWND window, UINT message, std::wstring text, bool failed) {
    if (!window || !IsWindow(window) || (g_runtime && g_runtime->closing)) {
        return;
    }
    auto* payload = new StatusPayload{std::move(text), failed};
    if (!PostMessageW(
            window,
            message,
            0,
            reinterpret_cast<LPARAM>(payload))) {
        delete payload;
    }
}

void post_status(HWND window, const std::string& text) {
    post_payload(
        window,
        kStatusMessage,
        offline_translator::tr(from_utf8(text)),
        false);
}

void fill_language_combos() {
    SendMessageW(g_source_language_combo, CB_RESETCONTENT, 0, 0);
    SendMessageW(g_target_language_combo, CB_RESETCONTENT, 0, 0);
    if (g_fallback_target_language_combo) {
        SendMessageW(g_fallback_target_language_combo, CB_RESETCONTENT, 0, 0);
    }
    // «Авто» только у языка источника, у цели автоопределение смысла не имеет.
    SendMessageW(
        g_source_language_combo,
        CB_ADDSTRING,
        0,
        reinterpret_cast<LPARAM>(L"Авто"));
    for (const auto& name : combo_language_names()) {
        SendMessageW(
            g_source_language_combo,
            CB_ADDSTRING,
            0,
            reinterpret_cast<LPARAM>(name.c_str()));
        SendMessageW(
            g_target_language_combo,
            CB_ADDSTRING,
            0,
            reinterpret_cast<LPARAM>(name.c_str()));
        if (g_fallback_target_language_combo) {
            SendMessageW(
                g_fallback_target_language_combo,
                CB_ADDSTRING,
                0,
                reinterpret_cast<LPARAM>(name.c_str()));
        }
    }
}

void apply_settings_to_ui(const offline_translator::AppSettings& settings) {
    SendMessageW(
        g_engine_combo,
        CB_SETCURSEL,
        engine_combo_index(
            offline_translator::engine_kind_from_settings(settings.engine)),
        0);
    fill_language_combos();
    g_auto_source_language =
        settings.source_language == kAutoSourceLanguage;
    select_source_language(g_source_language_combo, settings.source_language);
    select_language(
        g_target_language_combo,
        settings.target_language,
        language_count() > 1 ? 1 : 0);
    if (g_fallback_target_language_combo) {
        select_language(
            g_fallback_target_language_combo,
            settings.fallback_target_language,
            language_count() > 0 ? 1 : 0);
    }
}

void collect_window_size(HWND window, offline_translator::AppSettings& settings) {
    RECT bounds{};
    if (GetWindowRect(window, &bounds)) {
        settings.window_width = bounds.right - bounds.left;
        settings.window_height = bounds.bottom - bounds.top;
    }
}

offline_translator::AppSettings current_settings(HWND window) {
    offline_translator::AppSettings settings =
        g_runtime ? g_runtime->settings : offline_translator::AppSettings{};
    settings.engine =
        offline_translator::settings_engine_name(selected_engine());
    settings.source_language = g_auto_source_language
        ? std::string(kAutoSourceLanguage)
        : selected_source_language(g_source_language_combo);
    settings.target_language = selected_language(g_target_language_combo);
    if (g_fallback_target_language_combo) {
        settings.fallback_target_language =
            selected_language(g_fallback_target_language_combo);
    }
    if (g_float_button && IsWindow(g_float_button)) {
        RECT float_bounds{};
        if (GetWindowRect(g_float_button, &float_bounds)) {
            settings.image_float_button_x = float_bounds.left;
            settings.image_float_button_y = float_bounds.top;
        }
    }
    collect_window_size(window, settings);
    return settings;
}

// Когда «С языка» = Авто и определённый язык совпал с выбранным «На язык»,
// переводим на третий (настраиваемый) язык. Если он не задан или совпал с
// целью — прежнее поведение (en-ru).
std::string resolve_fallback_target(
    const std::string& source,
    const std::string& target) {
    std::string fallback;
    if (g_fallback_target_language_combo) {
        try {
            fallback = selected_language(g_fallback_target_language_combo);
        } catch (const std::exception&) {
        }
    }
    if (fallback.empty() && g_runtime) {
        fallback = g_runtime->settings.fallback_target_language;
    }
    if (fallback.empty() || fallback == target || fallback == source) {
        return source == "en" ? "ru" : "en";
    }
    return fallback;
}

void persist_settings(HWND window) {
    if (g_smoke_mode || !g_engine_combo) {
        return;
    }
    try {
        auto settings = current_settings(window);
        offline_translator::save_settings(settings);
        store_runtime_settings(settings);
    } catch (const std::exception&) {
        // Сохранение не должно ронять интерфейс.
    }
}

void set_main_busy(bool busy) {
    EnableWindow(g_translate_button, !busy);
    EnableWindow(g_engine_combo, !busy);
    EnableWindow(g_source_language_combo, !busy);
    EnableWindow(g_target_language_combo, !busy);
    EnableWindow(g_fallback_target_language_combo, !busy);
    EnableWindow(g_packages_button, !busy);
}

void layout_main(HWND window) {
    RECT client{};
    GetClientRect(window, &client);
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;
    const int content_width = width - 32;
    const int status_top = height - 28;
    // Вторая строка языков (третий комбобокс) видна только при «Авто»,
    // поэтому контролы ниже сдвигаются вниз.
    const int lang_shift =
        (g_auto_source_language && g_fallback_target_language_combo) ? 30 : 0;
    const int result_top = 228 + lang_shift;
    int result_height = status_top - result_top - 8;
    if (result_height < 60) {
        result_height = 60;
    }
    if (g_packages_button) {
        SetWindowPos(
            g_packages_button,
            nullptr,
            width - 16 - 110,
            12,
            110,
            28,
            SWP_NOZORDER);
    }
    if (g_settings_button) {
        SetWindowPos(
            g_settings_button,
            nullptr,
            width - 16 - 110 - 8 - 110,
            12,
            110,
            28,
            SWP_NOZORDER);
    }
    if (g_source_edit) {
        SetWindowPos(
            g_source_edit,
            nullptr,
            16,
            84 + lang_shift,
            content_width,
            90,
            SWP_NOZORDER);
    }
    if (g_translate_button) {
        SetWindowPos(
            g_translate_button,
            nullptr,
            16,
            186 + lang_shift,
            120,
            30,
            SWP_NOZORDER);
    }
    if (g_screen_ocr_button) {
        SetWindowPos(
            g_screen_ocr_button,
            nullptr,
            144,
            186 + lang_shift,
            130,
            30,
            SWP_NOZORDER);
    }
    if (g_image_button) {
        SetWindowPos(
            g_image_button,
            nullptr,
            282,
            186 + lang_shift,
            130,
            30,
            SWP_NOZORDER);
    }
    if (g_result_edit) {
        SetWindowPos(
            g_result_edit,
            nullptr,
            16,
            result_top,
            content_width,
            result_height,
            SWP_NOZORDER);
    }
    if (g_status_label) {
        SetWindowPos(
            g_status_label,
            nullptr,
            16,
            status_top,
            content_width,
            20,
            SWP_NOZORDER);
    }
}

// Второй ряд языков виден только при «С языка» = Авто.
void update_fallback_language_row(HWND window) {
    const bool auto_source = g_auto_source_language;
    const bool show =
        auto_source && g_fallback_target_language_combo != nullptr;
    if (g_fallback_target_label) {
        ShowWindow(g_fallback_target_label, show ? SW_SHOW : SW_HIDE);
    }
    if (g_fallback_target_language_combo) {
        ShowWindow(g_fallback_target_language_combo, show ? SW_SHOW : SW_HIDE);
    }
    // «Переводит в обоих направлениях» — только для двух конкретных языков.
    if (g_both_directions_label) {
        ShowWindow(
            g_both_directions_label,
            !auto_source ? SW_SHOW : SW_HIDE);
    }
    if (window && IsWindow(window)) {
        layout_main(window);
    }
}

void start_translation(HWND window) {
    if (!g_runtime || g_runtime->busy.exchange(true)) {
        offline_translator::app_log_warn(
            "перевод окна пропущен: уже выполняется " + runtime_flags());
        return;
    }
    const std::wstring source_text = control_text(g_source_edit);
    const auto engine_kind = selected_engine();
    std::string source_language = selected_source_language(
        g_source_language_combo);
    std::string target_language = selected_language(
        g_target_language_combo);
    const std::string text = to_utf8(source_text);
    const bool auto_source = source_language == kAutoSourceLanguage;
    const std::string combo_source = source_language;
    if (auto_source) {
        // Автоопределение: детектор по тексту (с ограничением по доступным
        // языкам), при неудаче — по письму; список переключаем на найденный
        // язык, режим «Авто» при этом сохраняется.
        source_language = detect_source_language(text, engine_kind);
        select_source_language(g_source_language_combo, source_language);
        offline_translator::app_log_info(
            "автоопределение языка: " + source_language);
        if (source_language == target_language) {
            // Определённый язык совпал с «На язык» — переводим на третий язык.
            target_language =
                resolve_fallback_target(source_language, target_language);
        }
    } else {
        // Ручной язык источника: если введён текст на языке «На язык»,
        // меняем направление на язык источника (С=англ., На=рус., введён
        // русский текст — переводим на английский).
        const std::string detected =
            detect_language_optional(text, engine_kind);
        if (!detected.empty() && detected == target_language &&
            combo_source != target_language) {
            source_language = detected;
            target_language = combo_source;
        }
    }
    offline_translator::app_log_info(
        "перевод окна старт chars=" + std::to_string(text.size()) + " " +
        source_language + ">" + target_language + " " + runtime_flags());
    const auto root = model_root_for_kind(engine_kind);
    const std::string engine_variant =
        engine_kind == offline_translator::EngineKind::firefox
            ? (g_runtime ? g_runtime->settings.architecture : std::string{"tiny"})
            : std::string{};
    const std::string api_key = online_api_key_for_kind(engine_kind);
    persist_settings(window);
    set_main_busy(true);
    set_status(L"Подготовка перевода...");
    auto runtime = g_runtime;
    std::thread(
        [window, text, root, engine_kind, engine_variant, api_key, source_language, target_language, runtime]() {
            auto result = std::make_unique<StatusPayload>();
            const auto started = GetTickCount64();
            try {
                std::lock_guard lock(runtime->mutex);
                if (runtime->closing) {
                    runtime->busy = false;
                    return;
                }
                auto& application =
                    runtime->session.acquire(engine_kind, root, engine_variant, api_key);
                if (!runtime->session.is_loaded()) {
                    offline_translator::app_log_info("загрузка модели окна");
                    post_status(window, "Загрузка модели...");
                } else {
                    post_status(window, "Перевод выполняется...");
                }
                result->text = from_utf8(
                    application.translate(
                        text,
                        source_language,
                        target_language)
                        .text);
                runtime->session.mark_loaded();
                post_status(window, "Готово");
                offline_translator::app_log_info(
                    "перевод окна готов ms=" +
                    std::to_string(GetTickCount64() - started) +
                    " out_chars=" + std::to_string(result->text.size()));
            } catch (const std::exception& error) {
                result->failed = true;
                result->text = from_utf8(error.what());
                post_status(window, "Ошибка перевода");
                offline_translator::app_log_error(
                    std::string("перевод окна ошибка ms=") +
                    std::to_string(GetTickCount64() - started) + " " +
                    error.what());
            }
            runtime->busy = false;
            if (runtime->closing || !IsWindow(window)) {
                return;
            }
            PostMessageW(
                window,
                kTranslateMessage,
                0,
                reinterpret_cast<LPARAM>(result.release()));
        })
        .detach();
}

std::wstring package_status_text(const PackageRow& row) {
    if (row.installed) {
        return offline_translator::tr(L"установлено");
    }
    if (row.incomplete) {
        return offline_translator::tr(L"незавершённая загрузка");
    }
    return offline_translator::tr(L"не установлено");
}

std::vector<PackageRow> collect_package_rows() {
    std::vector<PackageRow> rows;
    // Список показывает пакеты только выбранного движка.
    const auto engine = g_runtime
        ? selected_engine()
        : offline_translator::EngineKind::argos;
    if (engine == offline_translator::EngineKind::nllb) {
        PackageRow row;
        row.nllb = true;
        row.title = L"NLLB-200 Distilled 600M";
        offline_translator::NllbModelManager manager(model_root_path(true));
        row.installed = manager.is_installed();
        row.incomplete = manager.has_incomplete_package();
        rows.push_back(std::move(row));
        return rows;
    }
    if (engine == offline_translator::EngineKind::marian) {
        const auto marian_root = std::filesystem::path(marian_model_root());
        const auto catalog =
            offline_translator::MarianModelManager::available_packages();
        const auto installed =
            offline_translator::MarianModelManager::installed_packages(
                marian_root);
        std::vector<offline_translator::StorePair> marian_pairs;
        std::set<std::pair<std::string, std::string>> seen;
        for (const auto& item : installed) {
            offline_translator::StorePair pair;
            pair.from_code = item.from_code;
            pair.to_code = item.to_code;
            pair.installed = true;
            marian_pairs.push_back(pair);
            seen.emplace(item.from_code, item.to_code);
        }
        for (const auto& item : catalog) {
            if (seen.count({item.from_code, item.to_code}) > 0) {
                continue;
            }
            offline_translator::StorePair pair;
            pair.from_code = item.from_code;
            pair.to_code = item.to_code;
            marian_pairs.push_back(std::move(pair));
        }
        for (auto& pair : marian_pairs) {
            offline_translator::MarianModelManager manager(
                marian_root, pair.from_code, pair.to_code);
            pair.installed = pair.installed || manager.is_installed();
            pair.incomplete = manager.has_incomplete_package();
        }
        offline_translator::sort_store_pairs(marian_pairs);
        for (const auto& pair : marian_pairs) {
            PackageRow row;
            row.marian = true;
            row.from_code = pair.from_code;
            row.to_code = pair.to_code;
            row.title = from_utf8(
                "MarianMT · " + offline_translator::store_pair_label(pair));
            row.installed = pair.installed;
            row.incomplete = pair.incomplete;
            rows.push_back(std::move(row));
        }
        return rows;
    }
    if (engine == offline_translator::EngineKind::firefox) {
        // Пары Firefox Translations для выбранного размера модели.
        std::string architecture = g_runtime->settings.architecture;
        if (!offline_translator::FirefoxModelManager::is_architecture(architecture)) {
            architecture = "tiny";
        }
        const auto firefox_root = std::filesystem::path(firefox_model_root());
        const auto catalog = offline_translator::FirefoxModelManager::available_packages(
            firefox_root, architecture);
        const auto installed =
            offline_translator::FirefoxModelManager::installed_packages(firefox_root);
        std::vector<offline_translator::StorePair> firefox_pairs;
        std::set<std::pair<std::string, std::string>> seen;
        for (const auto& item : installed) {
            if (item.architecture.empty() || item.architecture == architecture) {
                offline_translator::StorePair pair;
                pair.from_code = item.from_code;
                pair.to_code = item.to_code;
                pair.installed = true;
                firefox_pairs.push_back(pair);
                seen.emplace(item.from_code, item.to_code);
            }
        }
        for (const auto& item : catalog) {
            if (seen.count({item.from_code, item.to_code}) > 0) {
                continue;
            }
            offline_translator::StorePair pair;
            pair.from_code = item.from_code;
            pair.to_code = item.to_code;
            firefox_pairs.push_back(std::move(pair));
        }
        for (auto& pair : firefox_pairs) {
            offline_translator::FirefoxModelManager manager(
                firefox_root,
                architecture,
                pair.from_code,
                pair.to_code);
            pair.installed = pair.installed || manager.is_installed();
            pair.incomplete = manager.has_incomplete_package();
        }
        offline_translator::sort_store_pairs(firefox_pairs);
        for (const auto& pair : firefox_pairs) {
            PackageRow row;
            row.firefox = true;
            row.architecture = architecture;
            row.from_code = pair.from_code;
            row.to_code = pair.to_code;
            row.title = from_utf8("Firefox (" + architecture + ") · " +
                                  offline_translator::store_pair_label(pair));
            row.installed = pair.installed;
            row.incomplete = pair.incomplete;
            rows.push_back(std::move(row));
        }
        return rows;
    }
    // Argos: каталог + установленные пары, без строки NLLB.
    const auto argos_root = model_root_path(false);
    const auto store = offline_translator::merge_store_pairs(
        offline_translator::ArgosModelManager::available_packages(),
        offline_translator::ArgosModelManager::installed_packages(argos_root));
    rows.reserve(store.size());
    for (const auto& entry : store) {
        if (entry.nllb) {
            continue;
        }
        PackageRow row;
        row.from_code = entry.from_code;
        row.to_code = entry.to_code;
        offline_translator::ArgosModelManager manager(
            argos_root,
            entry.from_code,
            entry.to_code);
        row.title = L"Argos · " + from_utf8(
            offline_translator::store_pair_label(entry));
        row.installed = manager.is_installed() || entry.installed;
        row.incomplete = manager.has_incomplete_package() || entry.incomplete;
        rows.push_back(std::move(row));
    }
    return rows;
}

void update_packages_status_count() {
    if (!g_package_status || !g_runtime) {
        return;
    }
    const std::size_t shown = g_runtime->package_rows.size();
    const std::size_t total = g_runtime->package_rows_all.size();
    std::wstring text;
    if (shown == total) {
        text = offline_translator::tr(L"Пакетов в списке: ") + std::to_wstring(total);
    } else {
        text = offline_translator::tr(L"Показано ") + std::to_wstring(shown) +
            offline_translator::tr(L" из ") + std::to_wstring(total) +
            offline_translator::tr(L" пакетов");
    }
    SetWindowTextW(g_package_status, text.c_str());
}

bool package_row_matches_search(const PackageRow& row, const std::string& query) {
    offline_translator::StorePair pair;
    pair.nllb = row.nllb;
    pair.from_code = row.from_code;
    pair.to_code = row.to_code;
    pair.installed = row.installed;
    pair.incomplete = row.incomplete;
    return offline_translator::store_pair_matches(pair, query);
}

std::wstring package_row_line(const PackageRow& row) {
    std::wstring line = row.title + L" — " + package_status_text(row);
    return line;
}

void apply_package_filter() {
    if (!g_runtime || !g_package_list) {
        return;
    }
    const std::string query =
        g_package_search ? to_utf8(control_text(g_package_search)) : std::string();
    g_runtime->package_rows.clear();
    SendMessageW(g_package_list, LB_RESETCONTENT, 0, 0);
    for (const auto& row : g_runtime->package_rows_all) {
        if (!package_row_matches_search(row, query)) {
            continue;
        }
        const std::wstring line = package_row_line(row);
        SendMessageW(
            g_package_list,
            LB_ADDSTRING,
            0,
            reinterpret_cast<LPARAM>(line.c_str()));
        g_runtime->package_rows.push_back(row);
    }
    if (!g_runtime->package_rows.empty()) {
        SendMessageW(g_package_list, LB_SETCURSEL, 0, 0);
    }
    update_packages_status_count();
}

void refresh_package_list() {
    if (!g_package_list || !g_runtime) {
        return;
    }
    const bool firefox_mode =
        selected_engine() == offline_translator::EngineKind::firefox;
    if (g_package_caption) {
        static const wchar_t* captions[] = {
            L"Пакеты Argos Translate — каталог и установленные пары:",
            L"Пакет NLLB-200 — одна модель на ~200 языков:",
            L"Пакеты Firefox Translations:",
            L"Пакеты MarianMT — модели OPUS-MT с Hugging Face:",
            L"Онлайн-перевод — модели не нужны:",
        };
        int caption = 0;
        switch (selected_engine()) {
            case offline_translator::EngineKind::nllb:
                caption = 1;
                break;
            case offline_translator::EngineKind::firefox:
                caption = 2;
                break;
            case offline_translator::EngineKind::marian:
                caption = 3;
                break;
            case offline_translator::EngineKind::google:
                caption = 4;
                break;
            case offline_translator::EngineKind::argos:
                break;
        }
        SetWindowTextW(
            g_package_caption,
            offline_translator::tr(captions[caption]).c_str());
    }
    if (g_package_arch_caption) {
        ShowWindow(g_package_arch_caption, firefox_mode ? SW_SHOW : SW_HIDE);
    }
    if (g_package_arch_combo) {
        ShowWindow(g_package_arch_combo, firefox_mode ? SW_SHOW : SW_HIDE);
        const std::string architecture =
            g_runtime->settings.architecture == "base" ? "base" : "tiny";
        SendMessageW(
            g_package_arch_combo,
            CB_SETCURSEL,
            architecture == "base" ? 1 : 0,
            0);
    }
    if (is_online_engine(selected_engine())) {
        // Онлайн-переводчики работают без моделей: списка пакетов нет.
        g_runtime->package_rows_all.clear();
        apply_package_filter();
        if (g_package_status) {
            SetWindowTextW(
                g_package_status,
                offline_translator::tr(
                    L"Онлайн-переводчику модели не нужны — перевод идёт через интернет.")
                    .c_str());
        }
        return;
    }
    g_runtime->package_rows_all = collect_package_rows();
    apply_package_filter();
}

const PackageRow* selected_package_row() {
    if (!g_runtime || !g_package_list) {
        return nullptr;
    }
    const LRESULT index = SendMessageW(g_package_list, LB_GETCURSEL, 0, 0);
    if (index < 0 ||
        static_cast<std::size_t>(index) >= g_runtime->package_rows.size()) {
        return nullptr;
    }
    return &g_runtime->package_rows[static_cast<std::size_t>(index)];
}

void set_packages_busy(bool busy) {
    EnableWindow(g_package_install, !busy);
    EnableWindow(g_package_uninstall, !busy);
    EnableWindow(g_package_list, !busy);
    EnableWindow(g_package_search, !busy);
    EnableWindow(g_package_arch_combo, !busy);
}

void start_package_job(HWND packages_window, bool install) {
    if (!g_runtime) {
        return;
    }
    const PackageRow* selected = selected_package_row();
    if (!selected) {
        localized_message_box(
            packages_window,
            L"Выберите пакет.",
            L"Пакеты",
            MB_ICONINFORMATION | MB_OK);
        return;
    }
    const PackageRow row = *selected;
    if (install && row.installed) {
        localized_message_box(
            packages_window,
            L"Этот пакет уже установлен.",
            L"Пакеты",
            MB_ICONINFORMATION | MB_OK);
        return;
    }
    if (!install && !row.installed && !row.incomplete) {
        localized_message_box(
            packages_window,
            L"Нечего удалять: пакет не установлен.",
            L"Пакеты",
            MB_ICONINFORMATION | MB_OK);
        return;
    }
    if (install && row.nllb) {
        const int answer = localized_message_box(
            packages_window,
            L"Скачать модель NLLB-200 Distilled 600M (около 622 МБ)?\nЭто займёт время и место на диске.",
            L"Установка NLLB",
            MB_ICONWARNING | MB_YESNO);
        if (answer != IDYES) {
            return;
        }
    }
    if (install && row.firefox) {
        const int answer = localized_message_box(
            packages_window,
            (L"Скачать пакет Firefox Translations?\n" + row.title).c_str(),
            L"Установка Firefox",
            MB_ICONINFORMATION | MB_YESNO);
        if (answer != IDYES) {
            return;
        }
    }
    if (install && row.marian) {
        const int answer = localized_message_box(
            packages_window,
            (L"Скачать модель MarianMT (Helsinki-NLP OPUS-MT)?\n" +
             row.title)
                .c_str(),
            L"Установка MarianMT",
            MB_ICONINFORMATION | MB_YESNO);
        if (answer != IDYES) {
            return;
        }
    }
    if (!install && row.nllb) {
        const int answer = localized_message_box(
            packages_window,
            L"Удалить установленную модель NLLB-200?\nЭто единственная модель NLLB; её придётся качать заново.",
            L"Удаление NLLB",
            MB_ICONWARNING | MB_YESNO);
        if (answer != IDYES) {
            return;
        }
    }
    if (!install && !row.nllb) {
        const std::wstring question =
            L"Удалить пакет " + row.title + L"?";
        const int answer = localized_message_box(
            packages_window,
            question.c_str(),
            row.marian ? L"Удаление MarianMT" : L"Удаление Argos",
            MB_ICONQUESTION | MB_YESNO);
        if (answer != IDYES) {
            return;
        }
    }
    if (g_runtime->packages_busy.exchange(true)) {
        return;
    }
    set_packages_busy(true);
    if (g_package_status) {
        SetWindowTextW(
            g_package_status,
            install ? L"Установка пакета..." : L"Удаление пакета...");
    }
    auto runtime = g_runtime;
    std::thread([packages_window, install, row, runtime]() {
        auto done = std::make_unique<StatusPayload>();
        try {
            std::lock_guard lock(runtime->mutex);
            runtime->session.reset();
            const auto progress =
                [packages_window, runtime](
                    std::uint64_t downloaded,
                    std::uint64_t total,
                    std::string_view message) {
                    if (runtime->closing || !IsWindow(packages_window)) {
                        return;
                    }
                    std::string text(message);
                    if (total > 0) {
                        const int percent = static_cast<int>(
                            (downloaded * 100) / total);
                        text += " (" + std::to_string(percent) + "%)";
                    }
                    post_payload(
                        packages_window,
                        kPackageProgressMessage,
                        from_utf8(text),
                        false);
                };
            if (row.nllb) {
                offline_translator::NllbModelManager manager(
                    model_root_path(true));
                if (install) {
                    manager.download_and_install(progress);
                } else {
                    manager.uninstall();
                }
            } else if (row.firefox) {
                offline_translator::FirefoxModelManager manager(
                    std::filesystem::path(firefox_model_root()),
                    row.architecture.empty() ? std::string{"tiny"}
                                             : row.architecture,
                    row.from_code,
                    row.to_code);
                if (install) {
                    manager.download_and_install(progress);
                } else {
                    manager.uninstall();
                }
            } else if (row.marian) {
                offline_translator::MarianModelManager manager(
                    std::filesystem::path(marian_model_root()),
                    row.from_code,
                    row.to_code);
                if (install) {
                    manager.download_and_install(progress);
                } else {
                    manager.uninstall();
                }
            } else {
                offline_translator::ArgosModelManager manager(
                    model_root_path(false),
                    row.from_code,
                    row.to_code);
                if (install) {
                    manager.download_and_install(progress);
                } else {
                    manager.uninstall();
                }
            }
            done->text = install
                ? L"Пакет установлен."
                : L"Пакет удалён.";
        } catch (const std::exception& error) {
            done->failed = true;
            done->text = from_utf8(error.what());
            offline_translator::app_log_error(
                std::string("пакет не установлен: ") + error.what());
        }
        runtime->packages_busy = false;
        if (runtime->closing || !IsWindow(packages_window)) {
            return;
        }
        PostMessageW(
            packages_window,
            kPackageDoneMessage,
            0,
            reinterpret_cast<LPARAM>(done.release()));
    }).detach();
}

HINSTANCE window_instance(HWND window) {
    return reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(window, GWLP_HINSTANCE));
}

bool is_dark_theme() {
    if (!g_runtime) {
        return false;
    }
    return offline_translator::normalize_ui_theme(g_runtime->settings.ui_theme) ==
        offline_translator::kUiThemeDark;
}

UiTheme current_ui_theme() {
    if (is_dark_theme()) {
        return UiTheme{
            RGB(30, 30, 30),
            RGB(72, 72, 72),
            RGB(238, 238, 238),
            RGB(238, 238, 238),
            RGB(48, 48, 48),
            RGB(66, 66, 66),
            RGB(238, 238, 238),
        };
    }
    return UiTheme{
        RGB(255, 255, 255),
        RGB(206, 206, 206),
        RGB(24, 24, 24),
        RGB(24, 24, 24),
        RGB(245, 245, 245),
        RGB(228, 228, 228),
        RGB(24, 24, 24),
    };
}

HBRUSH theme_background_brush() {
    static COLORREF last = RGB(255, 255, 255);
    static HBRUSH brush = CreateSolidBrush(last);
    const COLORREF now = current_ui_theme().background;
    if (now != last) {
        if (brush) {
            DeleteObject(brush);
        }
        brush = CreateSolidBrush(now);
        last = now;
    }
    return brush;
}

// Системный шрифт интерфейса (Segoe UI), уже с учётом DPI. Контролы,
// созданные без WM_SETFONT, получают его при применении темы.
HFONT ui_font() {
    static HFONT font = nullptr;
    if (font == nullptr) {
        NONCLIENTMETRICSW metrics{};
        metrics.cbSize = sizeof(metrics);
        if (SystemParametersInfoW(
                SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0)) {
            font = CreateFontIndirectW(&metrics.lfMessageFont);
        }
        if (font == nullptr) {
            font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        }
    }
    return font;
}

// Перерисовка выпадающего списка (CBS_OWNERDRAWFIXED) в цветах темы:
// иначе стандартный комбобокс всегда светлый и выбивается из тёмной темы.
void draw_owner_combo(const DRAWITEMSTRUCT& draw) {
    const UiTheme theme = current_ui_theme();
    const bool selected = (draw.itemState & ODS_SELECTED) != 0;
    HBRUSH brush =
        CreateSolidBrush(selected ? theme.button_hover : theme.background);
    FillRect(draw.hDC, &draw.rcItem, brush);
    DeleteObject(brush);
    SetBkMode(draw.hDC, TRANSPARENT);
    SetTextColor(draw.hDC, theme.text);
    if (draw.hwndItem != nullptr) {
        SelectObject(draw.hDC, ui_font());
    }
    std::wstring text;
    const LRESULT index = (draw.itemState & ODS_COMBOBOXEDIT) != 0
        ? SendMessageW(draw.hwndItem, CB_GETCURSEL, 0, 0)
        : static_cast<LRESULT>(draw.itemID);
    if (draw.hwndItem != nullptr && index >= 0 &&
        index != static_cast<LRESULT>(CB_ERR)) {
        const LRESULT length =
            SendMessageW(draw.hwndItem, CB_GETLBTEXTLEN, index, 0);
        if (length > 0) {
            text.resize(static_cast<std::size_t>(length));
            SendMessageW(
                draw.hwndItem,
                CB_GETLBTEXT,
                index,
                reinterpret_cast<LPARAM>(text.data()));
        }
    }
    RECT text_rect = draw.rcItem;
    text_rect.left += 7;
    text_rect.right -= 6;
    DrawTextW(
        draw.hDC,
        text.c_str(),
        -1,
        &text_rect,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    // Рамку рисуем только у закрытого поля, не у пунктов списка.
    if ((draw.itemState & ODS_COMBOBOXEDIT) != 0) {
        HBRUSH frame = CreateSolidBrush(theme.border);
        RECT border = draw.rcItem;
        FrameRect(draw.hDC, &border, frame);
        DeleteObject(frame);
    }
}

void measure_owner_combo(MEASUREITEMSTRUCT* measure) {
    if (measure == nullptr) {
        return;
    }
    HDC dc = GetDC(nullptr);
    const HFONT old = static_cast<HFONT>(SelectObject(dc, ui_font()));
    TEXTMETRICW metrics{};
    GetTextMetricsW(dc, &metrics);
    SelectObject(dc, old);
    ReleaseDC(nullptr, dc);
    measure->itemHeight = static_cast<UINT>(metrics.tmHeight + 8);
}

// Плоская кнопка в цветах темы (для кнопок настроек и «Пакетов»).
void draw_flat_button(const DRAWITEMSTRUCT& draw) {
    const UiTheme theme = current_ui_theme();
    HBRUSH brush = CreateSolidBrush(theme.button_face);
    FillRect(draw.hDC, &draw.rcItem, brush);
    DeleteObject(brush);
    SetBkMode(draw.hDC, TRANSPARENT);
    SetTextColor(
        draw.hDC,
        (draw.itemState & ODS_DISABLED) != 0
            ? (is_dark_theme() ? RGB(120, 120, 120) : RGB(165, 165, 165))
            : theme.button_text);
    if (draw.hwndItem != nullptr) {
        SelectObject(draw.hDC, ui_font());
    }
    wchar_t label[128]{};
    if (draw.hwndItem != nullptr) {
        GetWindowTextW(draw.hwndItem, label, 128);
    }
    RECT text_rect = draw.rcItem;
    DrawTextW(
        draw.hDC,
        label,
        -1,
        &text_rect,
        DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    HBRUSH frame = CreateSolidBrush(theme.border);
    RECT border = draw.rcItem;
    FrameRect(draw.hDC, &border, frame);
    DeleteObject(frame);
}

BOOL CALLBACK apply_theme_to_child(HWND child, LPARAM) {
    SetWindowTheme(child, L"", L"");
    if (SendMessageW(child, WM_GETFONT, 0, 0) == 0) {
        SendMessageW(
            child,
            WM_SETFONT,
            reinterpret_cast<WPARAM>(ui_font()),
            TRUE);
    }
    return TRUE;
}

// Номер сборки Windows без манифеста (RtlGetVersion не врёт, в отличие от
// GetVersion/GetVersionEx).
DWORD windows_build_number() {
    struct OsVersionInfo {
        ULONG dwOSVersionInfoSize;
        ULONG dwMajorVersion;
        ULONG dwMinorVersion;
        ULONG dwBuildNumber;
        ULONG dwPlatformId;
        WCHAR szCSDVersion[128];
    };
    using RtlGetVersionFn = LONG(WINAPI*)(OsVersionInfo*);
    static const DWORD build = []() -> DWORD {
        const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
        if (ntdll != nullptr) {
            const auto fn = reinterpret_cast<RtlGetVersionFn>(
                GetProcAddress(ntdll, "RtlGetVersion"));
            if (fn != nullptr) {
                OsVersionInfo info{};
                info.dwOSVersionInfoSize =
                    static_cast<ULONG>(sizeof(OsVersionInfo));
                if (fn(&info) == 0) {
                    return static_cast<DWORD>(info.dwBuildNumber);
                }
            }
        }
        return 0;
    }();
    return build;
}

void apply_dark_title_bar(HWND window) {
    if (!window || !IsWindow(window)) {
        return;
    }
    const DWORD build = windows_build_number();
    // Тёмный/светлый заголовок: 19 — Windows 10 1809–1903, 20 — 2004+ и 11.
    if (build >= 17763) {
        BOOL dark = is_dark_theme() ? TRUE : FALSE;
        const DWORD attribute = build >= 18985 ? 20 : 19;
        DwmSetWindowAttribute(window, attribute, &dark, sizeof(dark));
    }
    // Windows 11: точные цвета заголовка, текста и рамки под тему
    // (на более старых версиях заголовок следует тёмному/светлому режиму).
    if (build >= 22000) {
        const UiTheme theme = current_ui_theme();
        COLORREF caption = theme.background;
        COLORREF caption_text = theme.text;
        COLORREF border = theme.border;
        DwmSetWindowAttribute(window, 34, &border, sizeof(border));
        DwmSetWindowAttribute(window, 35, &caption, sizeof(caption));
        DwmSetWindowAttribute(window, 36, &caption_text, sizeof(caption_text));
    }
}

void apply_theme_to_window(HWND window) {
    if (!window || !IsWindow(window)) {
        return;
    }
    // Только дочерние контролы: им пустая тема нужна, чтобы работали
    // WM_CTLCOLOR-цвета. Верхнеуровневое окно НЕ трогаем — иначе DWM
    // перестаёт применять тёмный/светлый цвет заголовка.
    EnumChildWindows(window, apply_theme_to_child, 0);
    apply_dark_title_bar(window);
    SetWindowPos(
        window,
        nullptr,
        0,
        0,
        0,
        0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE |
            SWP_FRAMECHANGED);
    InvalidateRect(window, nullptr, TRUE);
    RedrawWindow(
        window,
        nullptr,
        nullptr,
        RDW_ERASE | RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN |
            RDW_UPDATENOW);
}

void apply_live_theme() {
    if (!g_runtime) {
        return;
    }
    apply_theme_to_window(g_runtime->main_window);
    apply_theme_to_window(g_runtime->settings_window);
    apply_theme_to_window(g_runtime->packages_window);
}

LRESULT theme_control_color(WPARAM w_param, LPARAM l_param) {
    const HDC dc = reinterpret_cast<HDC>(w_param);
    const UiTheme theme = current_ui_theme();
    SetBkColor(dc, theme.background);
    SetTextColor(dc, theme.text);
    const HWND child = reinterpret_cast<HWND>(l_param);
    wchar_t class_name[32]{};
    GetClassNameW(child, class_name, 32);
    if (lstrcmpiW(class_name, L"EDIT") == 0 ||
        lstrcmpiW(class_name, L"COMBOBOX") == 0) {
        SetBkColor(dc, theme.background);
    }
    return reinterpret_cast<LRESULT>(theme_background_brush());
}

std::wstring find_asset_file(const std::wstring& name) {
    const std::wstring exe_dir = executable_directory();
    std::vector<std::wstring> candidates{
        exe_dir + L"\\assets\\" + name,
        exe_dir + L"\\" + name,
    };
    std::wstring walk = exe_dir;
    for (int step = 0; step < 8; ++step) {
        candidates.push_back(walk + L"\\assets\\" + name);
        const auto separator = walk.find_last_of(L"\\/");
        if (separator == std::wstring::npos) {
            break;
        }
        walk = walk.substr(0, separator);
    }
    for (const auto& path : candidates) {
        if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) {
            return path;
        }
    }
    return {};
}

std::wstring find_app_icon_path() {
    return find_asset_file(L"app.ico");
}

std::wstring find_theme_icon_path() {
    const std::wstring primary =
        is_dark_theme() ? L"icon-dark.png" : L"icon-light.png";
    const std::wstring path = find_asset_file(primary);
    if (!path.empty()) {
        return path;
    }
    return find_asset_file(L"icon.png");
}

HICON create_generated_icon() {
    HDC screen = GetDC(nullptr);
    HDC memory = CreateCompatibleDC(screen);
    HBITMAP color = CreateCompatibleBitmap(screen, 16, 16);
    HBITMAP mask = CreateBitmap(16, 16, 1, 1, nullptr);
    HGDIOBJ old = SelectObject(memory, color);
    HBRUSH brush = CreateSolidBrush(RGB(45, 106, 227));
    RECT bounds{0, 0, 16, 16};
    FillRect(memory, &bounds, brush);
    DeleteObject(brush);
    SelectObject(memory, old);
    DeleteDC(memory);
    ReleaseDC(nullptr, screen);
    ICONINFO info{};
    info.fIcon = TRUE;
    info.hbmMask = mask;
    info.hbmColor = color;
    HICON icon = CreateIconIndirect(&info);
    DeleteObject(color);
    DeleteObject(mask);
    return icon;
}

HICON create_hicon_from_png(const std::wstring& path, int size) {
    std::unique_ptr<Gdiplus::Bitmap> source(
        Gdiplus::Bitmap::FromFile(path.c_str()));
    if (!source || source->GetLastStatus() != Gdiplus::Ok) {
        return nullptr;
    }
    Gdiplus::Bitmap scaled(size, size, PixelFormat32bppARGB);
    if (scaled.GetLastStatus() != Gdiplus::Ok) {
        return nullptr;
    }
    Gdiplus::Graphics graphics(&scaled);
    if (graphics.GetLastStatus() != Gdiplus::Ok) {
        return nullptr;
    }
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
    graphics.Clear(Gdiplus::Color(0, 0, 0, 0));
    graphics.DrawImage(source.get(), 0, 0, size, size);
    HICON icon = nullptr;
    if (scaled.GetHICON(&icon) != Gdiplus::Ok) {
        return nullptr;
    }
    return icon;
}

HICON load_tray_icon() {
    const std::wstring png = find_theme_icon_path();
    if (!png.empty()) {
        if (HICON icon = create_hicon_from_png(png, 16)) {
            return icon;
        }
    }
    const std::wstring path = find_app_icon_path();
    if (!path.empty()) {
        HICON icon = static_cast<HICON>(LoadImageW(
            nullptr,
            path.c_str(),
            IMAGE_ICON,
            16,
            16,
            LR_LOADFROMFILE));
        if (icon) {
            return icon;
        }
    }
    return create_generated_icon();
}

constexpr int kAppIconResourceId = 1;

HICON load_window_icon(int size) {
    if (HMODULE module = GetModuleHandleW(nullptr)) {
        if (HICON icon = static_cast<HICON>(LoadImageW(
                module,
                MAKEINTRESOURCEW(kAppIconResourceId),
                IMAGE_ICON,
                size,
                size,
                0))) {
            return icon;
        }
    }
    const std::wstring path = find_app_icon_path();
    if (!path.empty()) {
        HICON icon = static_cast<HICON>(LoadImageW(
            nullptr,
            path.c_str(),
            IMAGE_ICON,
            size,
            size,
            LR_LOADFROMFILE));
        if (icon) {
            return icon;
        }
    }
    return create_generated_icon();
}

void refresh_tray_icon() {
    HICON next = load_tray_icon();
    if (g_tray_icon) {
        DestroyIcon(g_tray_icon);
        g_tray_icon = nullptr;
    }
    g_tray_icon = next;
    if (g_tray_added) {
        g_tray_data.hIcon = g_tray_icon;
        Shell_NotifyIconW(NIM_MODIFY, &g_tray_data);
    }
}

std::wstring find_selection_icon_path() {
    return find_theme_icon_path();
}

// Повторяет _fill_clickable_disk() из selection_button.py: находит радиус
// знака по непрозрачным пикселям и заливает прозрачные точки внутри круга
// белым, чтобы клик в центр не проваливался сквозь layered-окно.
void fill_clickable_disk(Gdiplus::BitmapData& data, int alpha_limit) {
    const int width = static_cast<int>(data.Width);
    const int height = static_cast<int>(data.Height);
    auto* pixels = static_cast<std::uint32_t*>(data.Scan0);
    const double center_x = (width - 1) / 2.0;
    const double center_y = (height - 1) / 2.0;
    double radius_sq = 0.0;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::uint32_t argb =
                pixels[y * (data.Stride / 4) + x];
            if (((argb >> 24) & 0xFF) < alpha_limit) {
                continue;
            }
            const double dx = x - center_x;
            const double dy = y - center_y;
            radius_sq = std::max(radius_sq, dx * dx + dy * dy);
        }
    }
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            std::uint32_t& argb = pixels[y * (data.Stride / 4) + x];
            if (((argb >> 24) & 0xFF) >= alpha_limit) {
                continue;
            }
            const double dx = x - center_x;
            const double dy = y - center_y;
            if (dx * dx + dy * dy <= radius_sq) {
                argb = 0xFFFFFFFF;
            } else {
                argb = 0;
            }
        }
    }
}

// Готовит 40?40 premultiplied-DIB из тематической PNG для
// UpdateLayeredWindow. Возвращает true, если иконка загружена.
bool compose_selection_icon_bitmap(HBITMAP* out_bitmap) {
    *out_bitmap = nullptr;
    const std::wstring path = find_selection_icon_path();
    if (path.empty()) {
        sel_log("compose: тематическая иконка не найдена");
        return false;
    }
    std::unique_ptr<Gdiplus::Bitmap> source(
        Gdiplus::Bitmap::FromFile(path.c_str()));
    if (!source || source->GetLastStatus() != Gdiplus::Ok) {
        sel_log("compose: Bitmap::FromFile не удался");
        return false;
    }
    std::unique_ptr<Gdiplus::Bitmap> target(
        new Gdiplus::Bitmap(
            kSelectionIconSize,
            kSelectionIconSize,
            PixelFormat32bppPARGB));
    if (!target || target->GetLastStatus() != Gdiplus::Ok) {
        return false;
    }
    Gdiplus::Graphics graphics(target.get());
    if (graphics.GetLastStatus() != Gdiplus::Ok) {
        return false;
    }
    graphics.SetInterpolationMode(
        Gdiplus::InterpolationModeHighQualityBicubic);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
    graphics.DrawImage(
        source.get(),
        Gdiplus::Rect(0, 0, kSelectionIconSize, kSelectionIconSize),
        0,
        0,
        source->GetWidth(),
        source->GetHeight(),
        Gdiplus::UnitPixel);
    Gdiplus::Rect lock_rect(0, 0, kSelectionIconSize, kSelectionIconSize);
    Gdiplus::BitmapData data{};
    if (target->LockBits(
            &lock_rect,
            Gdiplus::ImageLockModeRead | Gdiplus::ImageLockModeWrite,
            PixelFormat32bppPARGB,
            &data) != Gdiplus::Ok) {
        return false;
    }
    fill_clickable_disk(data, 80);

    HDC screen = GetDC(nullptr);
    HDC memory = CreateCompatibleDC(screen);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = kSelectionIconSize;
    info.bmiHeader.biHeight = -kSelectionIconSize;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(
        memory,
        &info,
        DIB_RGB_COLORS,
        &bits,
        nullptr,
        0);
    bool copied = false;
    if (bitmap && bits) {
        for (int y = 0; y < kSelectionIconSize; ++y) {
            const auto* src = static_cast<const std::uint8_t*>(data.Scan0) +
                y * data.Stride;
            auto* dst = static_cast<std::uint8_t*>(bits) +
                y * kSelectionIconSize * 4;
            memcpy(dst, src, static_cast<std::size_t>(
                kSelectionIconSize) * 4);
        }
        copied = true;
    }
    DeleteDC(memory);
    ReleaseDC(nullptr, screen);
    target->UnlockBits(&data);
    if (!copied) {
        if (bitmap) {
            DeleteObject(bitmap);
        }
        return false;
    }
    // Диагностика: дамп скомпонованной иконки в %TEMP%.
    wchar_t dump_flag[8]{};
    if (GetEnvironmentVariableW(
            L"OT_DUMP_BUTTON",
            dump_flag,
            static_cast<DWORD>(std::size(dump_flag))) > 0) {
        CLSID png_clsid{};
        if (CLSIDFromString(
                L"{557CF406-1A04-11D3-9A73-0000F81EF32E}",
                &png_clsid) == S_OK) {
            wchar_t temp_dir[MAX_PATH]{};
            GetTempPathW(MAX_PATH, temp_dir);
            std::wstring dump = std::wstring(temp_dir) + L"fxbutton.png";
            target->Save(dump.c_str(), &png_clsid, nullptr);
        }
    }
    *out_bitmap = bitmap;
    return true;
}

// Показывает иконку как layered-окно в точке (x, y).
bool apply_layered_icon(HWND window, HBITMAP bitmap, int x, int y) {
    HDC screen = GetDC(nullptr);
    HDC memory = CreateCompatibleDC(screen);
    HGDIOBJ old = SelectObject(memory, bitmap);
    BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    SIZE size{kSelectionIconSize, kSelectionIconSize};
    POINT position{x, y};
    POINT zero{0, 0};
    const BOOL ok = UpdateLayeredWindow(
        window,
        screen,
        &position,
        &size,
        memory,
        &zero,
        0,
        &blend,
        ULW_ALPHA);
    SelectObject(memory, old);
    DeleteDC(memory);
    ReleaseDC(nullptr, screen);
    return ok != FALSE;
}

void add_tray_icon(HWND window) {
    if (g_smoke_mode || g_tray_added) {
        return;
    }
    if (!g_tray_icon) {
        g_tray_icon = load_tray_icon();
    }
    ZeroMemory(&g_tray_data, sizeof(g_tray_data));
    g_tray_data.cbSize = sizeof(g_tray_data);
    g_tray_data.hWnd = window;
    g_tray_data.uID = 1;
    g_tray_data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    g_tray_data.uCallbackMessage = kTrayMessage;
    g_tray_data.hIcon = g_tray_icon;
    wcscpy_s(
        g_tray_data.szTip,
        L"TLing");
    if (Shell_NotifyIconW(NIM_ADD, &g_tray_data)) {
        g_tray_added = true;
    }
}

void remove_tray_icon() {
    if (!g_tray_added) {
        return;
    }
    Shell_NotifyIconW(NIM_DELETE, &g_tray_data);
    g_tray_added = false;
}

void restore_from_tray(HWND window) {
    ShowWindow(window, SW_SHOWNORMAL);
    ShowWindow(window, SW_RESTORE);
    SetForegroundWindow(window);
    if (g_source_edit) {
        SetFocus(g_source_edit);
    }
}

void hide_to_tray(HWND window) {
    persist_settings(window);
    ShowWindow(window, SW_HIDE);
    add_tray_icon(window);
}

void destroy_popup_windows() {
    if (g_selection_button) {
        DestroyWindow(g_selection_button);
        g_selection_button = nullptr;
    }
    if (g_result_popup) {
        DestroyWindow(g_result_popup);
        g_result_popup = nullptr;
        g_popup_result_edit = nullptr;
        g_popup_copy_button = nullptr;
        g_popup_replace_button = nullptr;
        g_popup_add_button = nullptr;
        g_popup_original_button = nullptr;
    }
    hide_update_popup();
}

const wchar_t* tray_menu_label(UINT id) {
    switch (id) {
        case kTrayOpen:
            return L"Открыть";
        case kTrayAutostart:
            return L"Запускать вместе с Windows";
        case kTrayAutoCopy:
            return L"Автоматически копировать выделенный текст в буфер обмена";
        case kTrayTurbo:
            return L"Турбо перевод: сразу переводить выделенный текст";
        case kTrayCheckUpdate:
            return L"Проверить обновления";
        case kTrayScreenOcr:
            return L"Перевод области экрана";
        case kTrayImageFile:
            return L"Перевести изображение…";
        case kTrayImageRegion:
            return L"Перевод ячеек Word/Excel";
        case kTrayFloatButton:
            return L"Плавающая кнопка (перетащите файл)";
        case kTrayOcrOnly:
            return L"Только OCR (без перевода, до перезапуска)";
        case kTrayExit:
            return L"Выход";
        case kTrayHistoryRoot:
            return L"История копирований";
        case kTrayHistoryEmpty:
            return L"Пока пусто";
        case kTrayHistoryClear:
            return L"Очистить историю";
        default:
            break;
    }
    if (id >= kTrayHistoryFirst && id <= kTrayHistoryLast) {
        return L"Копировать";
    }
    if (id >= kTrayHistoryTranslateFirst && id <= kTrayHistoryTranslateLast) {
        return L"Перевести";
    }
    if (id >= kTrayHistoryPreviewFirst &&
        id < kTrayHistoryPreviewFirst + g_tray_history_labels.size()) {
        return g_tray_history_labels[id - kTrayHistoryPreviewFirst].c_str();
    }
    return L"";
}

void show_tray_menu(HWND window) {
    HMENU menu = CreatePopupMenu();
    if (!menu) {
        return;
    }
    AppendMenuW(
        menu,
        MF_OWNERDRAW,
        kTrayOpen,
        reinterpret_cast<LPCWSTR>(static_cast<UINT_PTR>(kTrayOpen)));
    HMENU history_menu = CreatePopupMenu();
    g_tray_history_popup = history_menu;
    g_tray_history_items.clear();
    g_tray_history_labels.clear();
    std::vector<std::string> history;
    try {
        history = offline_translator::load_clipboard_history();
    } catch (const std::exception&) {
    }
    const int history_limit = g_runtime
        ? offline_translator::normalize_clipboard_history_limit(
              g_runtime->settings.clipboard_history_limit)
        : offline_translator::kDefaultClipboardHistoryLimit;
    if (static_cast<int>(history.size()) > history_limit) {
        history.resize(static_cast<std::size_t>(history_limit));
    }
    if (history.empty()) {
        AppendMenuW(
            history_menu,
            MF_OWNERDRAW | MF_GRAYED,
            kTrayHistoryEmpty,
            reinterpret_cast<LPCWSTR>(static_cast<UINT_PTR>(kTrayHistoryEmpty)));
    } else {
        const int shown = std::min(
            static_cast<int>(history.size()),
            kTrayHistoryLast - kTrayHistoryFirst + 1);
        for (int index = 0; index < shown; ++index) {
            g_tray_history_items.push_back(
                from_utf8(history[static_cast<std::size_t>(index)]));
            g_tray_history_labels.push_back(
                from_utf8(offline_translator::clipboard_history_preview(
                    history[static_cast<std::size_t>(index)])));
            HMENU item_menu = CreatePopupMenu();
            AppendMenuW(
                item_menu,
                MF_OWNERDRAW,
                kTrayHistoryFirst + index,
                reinterpret_cast<LPCWSTR>(
                    static_cast<UINT_PTR>(kTrayHistoryFirst + index)));
            AppendMenuW(
                item_menu,
                MF_OWNERDRAW,
                kTrayHistoryTranslateFirst + index,
                reinterpret_cast<LPCWSTR>(
                    static_cast<UINT_PTR>(kTrayHistoryTranslateFirst + index)));
            MENUITEMINFOW item_info{};
            item_info.cbSize = sizeof(item_info);
            item_info.fMask = MIIM_FTYPE | MIIM_SUBMENU | MIIM_ID | MIIM_DATA;
            item_info.fType = MFT_OWNERDRAW;
            item_info.wID = kTrayHistoryPreviewFirst + index;
            item_info.hSubMenu = item_menu;
            item_info.dwItemData = kTrayHistoryPreviewFirst + index;
            InsertMenuItemW(
                history_menu,
                GetMenuItemCount(history_menu),
                TRUE,
                &item_info);
        }
        AppendMenuW(history_menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(
            history_menu,
            MF_OWNERDRAW,
            kTrayHistoryClear,
            reinterpret_cast<LPCWSTR>(static_cast<UINT_PTR>(kTrayHistoryClear)));
    }
    MENUITEMINFOW history_item{};
    history_item.cbSize = sizeof(history_item);
    history_item.fMask = MIIM_FTYPE | MIIM_SUBMENU | MIIM_ID | MIIM_DATA;
    history_item.fType = MFT_OWNERDRAW;
    history_item.wID = kTrayHistoryRoot;
    history_item.hSubMenu = history_menu;
    history_item.dwItemData = kTrayHistoryRoot;
    InsertMenuItemW(menu, GetMenuItemCount(menu), TRUE, &history_item);
    UINT autocopy_flags = MF_OWNERDRAW;
    {
        bool autocopy = false;
        try {
            autocopy = g_runtime && g_runtime->settings.auto_copy_selection;
        } catch (const std::exception&) {
        }
        if (autocopy) {
            autocopy_flags |= MF_CHECKED;
        }
    }
    AppendMenuW(
        menu,
        autocopy_flags,
        kTrayAutoCopy,
        reinterpret_cast<LPCWSTR>(static_cast<UINT_PTR>(kTrayAutoCopy)));
    UINT turbo_flags = MF_OWNERDRAW;
    {
        bool turbo = false;
        try {
            turbo = g_runtime && g_runtime->settings.turbo_translation;
        } catch (const std::exception&) {
        }
        if (turbo) {
            turbo_flags |= MF_CHECKED;
        }
    }
    AppendMenuW(
        menu,
        turbo_flags,
        kTrayTurbo,
        reinterpret_cast<LPCWSTR>(static_cast<UINT_PTR>(kTrayTurbo)));
    AppendMenuW(
        menu,
        MF_OWNERDRAW,
        kTrayCheckUpdate,
        reinterpret_cast<LPCWSTR>(static_cast<UINT_PTR>(kTrayCheckUpdate)));
    AppendMenuW(
        menu,
        MF_OWNERDRAW,
        kTrayScreenOcr,
        reinterpret_cast<LPCWSTR>(static_cast<UINT_PTR>(kTrayScreenOcr)));
    AppendMenuW(
        menu,
        MF_OWNERDRAW,
        kTrayImageFile,
        reinterpret_cast<LPCWSTR>(static_cast<UINT_PTR>(kTrayImageFile)));
    AppendMenuW(
        menu,
        MF_OWNERDRAW,
        kTrayImageRegion,
        reinterpret_cast<LPCWSTR>(static_cast<UINT_PTR>(kTrayImageRegion)));
    UINT float_flags = MF_OWNERDRAW;
    if (g_runtime && g_runtime->settings.image_float_button) {
        float_flags |= MF_CHECKED;
    }
    AppendMenuW(
        menu,
        float_flags,
        kTrayFloatButton,
        reinterpret_cast<LPCWSTR>(static_cast<UINT_PTR>(kTrayFloatButton)));
    UINT ocr_only_flags = MF_OWNERDRAW;
    if (g_ocr_only_session ||
        (g_runtime && g_runtime->settings.ocr_only)) {
        ocr_only_flags |= MF_CHECKED;
    }
    AppendMenuW(
        menu,
        ocr_only_flags,
        kTrayOcrOnly,
        reinterpret_cast<LPCWSTR>(static_cast<UINT_PTR>(kTrayOcrOnly)));
    AppendMenuW(
        menu,
        MF_OWNERDRAW,
        kTrayExit,
        reinterpret_cast<LPCWSTR>(static_cast<UINT_PTR>(kTrayExit)));
    SetMenuDefaultItem(menu, kTrayOpen, FALSE);
    MENUINFO menu_info{};
    menu_info.cbSize = sizeof(menu_info);
    menu_info.fMask = MIM_BACKGROUND;
    menu_info.hbrBack = theme_background_brush();
    SetMenuInfo(menu, &menu_info);
    POINT cursor{};
    GetCursorPos(&cursor);
    SetForegroundWindow(window);
    TrackPopupMenu(
        menu,
        TPM_RIGHTBUTTON | TPM_BOTTOMALIGN,
        cursor.x,
        cursor.y,
        0,
        window,
        nullptr);
    DestroyMenu(menu);
}

bool is_over_our_popup(int x, int y) {
    for (HWND candidate : {g_selection_button, g_result_popup}) {
        if (!candidate || !IsWindow(candidate) || !IsWindowVisible(candidate)) {
            continue;
        }
        RECT bounds{};
        GetWindowRect(candidate, &bounds);
        if (x >= bounds.left && x <= bounds.right && y >= bounds.top &&
            y <= bounds.bottom) {
            return true;
        }
    }
    return false;
}

void sel_log(const std::string& line) {
    offline_translator::app_log_info(line);
}

// Сторож и рабочие потоки читают строки настроек из своих потоков, пока
// GUI-поток их меняет. runtime->mutex держится всё время перевода, поэтому
// для настроек нужен отдельный короткий мьютекс.
void store_runtime_settings(const offline_translator::AppSettings& settings) {
    if (!g_runtime) {
        return;
    }
    std::lock_guard lock(g_runtime->settings_mutex);
    g_runtime->settings = settings;
}

template <typename Fn>
void update_runtime_settings(Fn&& mutate) {
    if (!g_runtime) {
        return;
    }
    std::lock_guard lock(g_runtime->settings_mutex);
    mutate(g_runtime->settings);
}

std::string runtime_flags() {
    if (!g_runtime) {
        return "runtime=0";
    }
    std::string engine;
    std::string theme;
    {
        std::lock_guard lock(g_runtime->settings_mutex);
        engine = g_runtime->settings.engine;
        theme = g_runtime->settings.ui_theme;
    }
    return "busy=" + std::to_string(g_runtime->busy.load()) +
        " sel_busy=" + std::to_string(g_runtime->selection_busy.load()) +
        " engine=" + engine +
        " theme=" + theme;
}

void restart_selection_timer(HWND window) {
    KillTimer(window, kSelectionPollTimer);
    if (SetTimer(window, kSelectionPollTimer, 40, nullptr) == 0) {
        offline_translator::app_log_error("SetTimer(poll) не удался");
    }
}

void recover_input_hooks(HWND window, const char* reason) {
    offline_translator::app_log_warn(
        std::string("восстановление ввода: ") + reason + " " + runtime_flags());
    restart_selection_timer(window);
}

void note_poll_tick(HWND window) {
    const auto now = GetTickCount64();
    const auto previous = g_last_poll_tick.exchange(now);
    if (previous != 0 && now > previous + 120000) {
        recover_input_hooks(
            window,
            ("пауза опроса " + std::to_string(now - previous) + " мс").c_str());
    }
    const auto last_beat = g_last_heartbeat_tick.load();
    if (last_beat == 0 || now > last_beat + 5 * 60 * 1000) {
        g_last_heartbeat_tick.store(now);
        const auto uptime_min =
            g_app_start_tick == 0 ? 0 : (now - g_app_start_tick) / 60000;
        offline_translator::app_log_info(
            "пульс uptime_min=" + std::to_string(uptime_min) + " " +
            runtime_flags());
    }
}

void start_watchdog(HWND window) {
    std::thread([window]() {
        while (true) {
            for (int step = 0; step < 60; ++step) {
                if (!g_runtime || g_runtime->closing) {
                    return;
                }
                Sleep(1000);
            }
            if (!g_runtime || g_runtime->closing || !IsWindow(window)) {
                return;
            }
            const auto now = GetTickCount64();
            const auto last_poll = g_last_poll_tick.load();
            const auto gap = last_poll == 0 ? now : now - last_poll;
            offline_translator::app_log_info(
                "сторож poll_gap_ms=" + std::to_string(gap) + " " +
                runtime_flags());
            if (gap > 30000) {
                PostMessageW(window, kRecoverInputMessage, 0, 0);
            }
        }
    }).detach();
}

// Ожидание в capture_selected_text_win32 с прокачкой сообщений:
// инжектированный Ctrl+C доставляется очередью нашему же потоку, и без
// прокачки EDIT никогда его не обработает.
void pump_wait(std::uint32_t milliseconds) {
    const ULONGLONG deadline = GetTickCount64() + milliseconds;
    while (GetTickCount64() < deadline) {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        Sleep(10);
    }
}

bool is_client_hit(int x, int y, HWND hwnd) {
    if (!hwnd) {
        return false;
    }
    const LRESULT hit = SendMessageW(
        hwnd,
        WM_NCHITTEST,
        0,
        MAKELPARAM(static_cast<WORD>(x), static_cast<WORD>(y)));
    return hit == HTCLIENT;
}

bool window_being_moved() {
    HWND foreground = GetForegroundWindow();
    if (!foreground) {
        return false;
    }
    const DWORD thread_id = GetWindowThreadProcessId(foreground, nullptr);
    GUITHREADINFO info{};
    info.cbSize = sizeof(info);
    if (!GetGUIThreadInfo(thread_id, &info)) {
        return false;
    }
    return (info.flags & GUI_INMOVESIZE) != 0 || info.hwndMoveSize != nullptr;
}

void hide_selection_button() {
    if (g_runtime && g_runtime->main_window) {
        KillTimer(g_runtime->main_window, kSelectionButtonHideTimer);
    }
    if (g_selection_button) {
        DestroyWindow(g_selection_button);
        g_selection_button = nullptr;
    }
}

void hide_result_popup() {
    if (g_result_popup) {
        DestroyWindow(g_result_popup);
        g_result_popup = nullptr;
        g_popup_result_edit = nullptr;
        g_popup_copy_button = nullptr;
        g_popup_replace_button = nullptr;
        g_popup_add_button = nullptr;
        g_popup_original_button = nullptr;
        g_popup_header = nullptr;
    }
}

LRESULT CALLBACK selection_button_proc(
    HWND window,
    UINT message,
    WPARAM w_param,
    LPARAM l_param);

LRESULT CALLBACK result_popup_proc(
    HWND window,
    UINT message,
    WPARAM w_param,
    LPARAM l_param);

void start_selection_translation(HWND main_window);

HFONT create_popup_font(int point_size) {
    HDC dc = GetDC(nullptr);
    const int height = -MulDiv(
        point_size,
        GetDeviceCaps(dc, LOGPIXELSY),
        72);
    ReleaseDC(nullptr, dc);
    return CreateFontW(
        height,
        0,
        0,
        0,
        FW_NORMAL,
        FALSE,
        FALSE,
        FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE,
        L"Segoe UI");
}

enum class PopupFont { header, body, button };

HFONT popup_font(PopupFont kind) {
    static HFONT header = create_popup_font(8);
    static HFONT body = create_popup_font(11);
    static HFONT button = create_popup_font(9);
    switch (kind) {
        case PopupFont::header:
            return header;
        case PopupFont::body:
            return body;
        case PopupFont::button:
            return button;
    }
    return body;
}

constexpr wchar_t kHoverPropertyName[] = L"ot_hover";

LRESULT CALLBACK flat_button_subclass(
    HWND handle,
    UINT message,
    WPARAM w_param,
    LPARAM l_param,
    UINT_PTR,
    DWORD_PTR) {
    if (message == WM_MOUSEMOVE) {
        TRACKMOUSEEVENT track{sizeof(TRACKMOUSEEVENT), TME_LEAVE, handle, 0};
        TrackMouseEvent(&track);
        if (!GetPropW(handle, kHoverPropertyName)) {
            SetPropW(handle, kHoverPropertyName, reinterpret_cast<HANDLE>(1));
            InvalidateRect(handle, nullptr, TRUE);
        }
    } else if (message == WM_MOUSELEAVE) {
        RemovePropW(handle, kHoverPropertyName);
        InvalidateRect(handle, nullptr, TRUE);
    } else if (message == WM_NCDESTROY) {
        RemovePropW(handle, kHoverPropertyName);
    }
    return DefSubclassProc(handle, message, w_param, l_param);
}

LRESULT CALLBACK popup_edit_subclass(
    HWND handle,
    UINT message,
    WPARAM w_param,
    LPARAM l_param,
    UINT_PTR,
    DWORD_PTR) {
    if (message == WM_KEYDOWN && w_param == VK_ESCAPE && g_result_popup) {
        hide_result_popup();
        return 0;
    }
    return DefSubclassProc(handle, message, w_param, l_param);
}

int popup_line_height() {
    HDC dc = GetDC(nullptr);
    HGDIOBJ old = SelectObject(dc, popup_font(PopupFont::body));
    RECT bounds{0, 0, 1000, 0};
    DrawTextW(dc, L"Ag", -1, &bounds, DT_CALCRECT | DT_SINGLELINE);
    SelectObject(dc, old);
    ReleaseDC(nullptr, dc);
    return static_cast<int>(std::max(bounds.bottom, 12L));
}

// Высота многострочного текста при переносе по ширине edit-поля.
int measure_text_height(const std::wstring& text, int width_px) {
    HDC dc = GetDC(nullptr);
    HGDIOBJ old = SelectObject(dc, popup_font(PopupFont::body));
    RECT bounds{0, 0, width_px, 0};
    DrawTextW(
        dc,
        text.c_str(),
        -1,
        &bounds,
        DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX | DT_EDITCONTROL);
    SelectObject(dc, old);
    ReleaseDC(nullptr, dc);
    return bounds.bottom;
}

void clamp_point_to_work_area(int width, int height, int& x, int& y) {
    POINT origin{x < 0 ? 0 : x, y < 0 ? 0 : y};
    const HMONITOR monitor = MonitorFromPoint(origin, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info{sizeof(MONITORINFO)};
    if (!GetMonitorInfoW(monitor, &info)) {
        return;
    }
    const RECT& area = info.rcWork;
    if (x + width > area.right) {
        x = area.right - width - 8;
    }
    if (y + height > area.bottom) {
        y = area.bottom - height - 8;
    }
    if (x < area.left) {
        x = area.left;
    }
    if (y < area.top) {
        y = area.top;
    }
}

void show_selection_button(int cursor_x, int cursor_y, HWND main_window) {
    hide_result_popup();
    hide_selection_button();
    g_button_uses_icon = false;
    HBITMAP icon_bitmap = nullptr;
    const bool have_icon = compose_selection_icon_bitmap(&icon_bitmap);
    g_selection_button = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW |
            (have_icon ? WS_EX_LAYERED : 0),
        L"TLingSelectionButton",
        L"Aa",
        WS_POPUP | WS_VISIBLE,
        cursor_x - kSelectionIconSize / 2,
        cursor_y - kSelectionIconSize / 2,
        kSelectionIconSize,
        kSelectionIconSize,
        main_window,
        nullptr,
        window_instance(main_window),
        nullptr);
    if (!g_selection_button) {
        if (icon_bitmap) {
            DeleteObject(icon_bitmap);
        }
        return;
    }
    if (have_icon &&
        apply_layered_icon(
            g_selection_button,
            icon_bitmap,
            cursor_x - kSelectionIconSize / 2,
            cursor_y - kSelectionIconSize / 2)) {
        g_button_uses_icon = true;
    } else {
        // Фолбэк без иконки: обычное окно с рамкой и текстом «Aa».
        SetWindowLongPtrW(
            g_selection_button,
            GWL_EXSTYLE,
            WS_EX_TOPMOST | WS_EX_TOOLWINDOW);
        SetWindowLongPtrW(
            g_selection_button,
            GWL_STYLE,
            WS_POPUP | WS_BORDER | WS_VISIBLE);
    }
    if (icon_bitmap) {
        DeleteObject(icon_bitmap);
    }
    SetTimer(main_window, kSelectionButtonHideTimer, 8000, nullptr);
}

bool ocr_only_active() {
    return g_ocr_only_session ||
        (g_runtime != nullptr && g_runtime->settings.ocr_only);
}

void show_result_popup(
    HWND main_window,
    const std::wstring& text,
    const std::string& source_code,
    const std::string& target_code,
    bool original_only = false) {
    hide_selection_button();
    hide_result_popup();
    // Оригинал для кнопки «Добавить»/«Показать оригинал»: выделенный текст
    // на момент показа окна (позже выделение в чужом окне могло пропасть).
    g_popup_source_text = trim_trailing_whitespace(g_selection.selected_text);
    g_popup_translation_text = text;
    g_popup_showing_original = false;
    POINT cursor{};
    GetCursorPos(&cursor);
    const bool selectable = g_runtime &&
        g_runtime->settings.result_window_mode ==
            offline_translator::kResultWindowSelectable;
    constexpr int border = 1;
    constexpr int pad_x = 16;
    constexpr int pad_y = 14;
    const int width = 420;
    const int content_width = width - 2 * border - 2 * pad_x;

    // Высота текстового поля: 2..12 строк с переносом, как в Python.
    const int line_height = popup_line_height();
    const int text_height = std::max(measure_text_height(text, content_width), line_height);
    int visual_lines = text_height / line_height;
    if (text_height % line_height > line_height / 3) {
        ++visual_lines;
    }
    visual_lines = std::clamp(visual_lines, 2, 12);
    const int edit_height = visual_lines * line_height + 4;

    const int header_height = line_height * 8 / 10 + 6;
    const int button_height = 28;
    const int buttons_y = border + pad_y + header_height + 4 +
        edit_height + 10;
    const int client_height =
        buttons_y + button_height + pad_y + border;

    int x = cursor.x + 12;
    int y = cursor.y + 12;
    clamp_point_to_work_area(width, client_height, x, y);

    g_result_popup = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        L"TLingResultPopup",
        L"Перевод",
        WS_POPUP | WS_VISIBLE,
        x,
        y,
        width,
        client_height,
        main_window,
        nullptr,
        window_instance(main_window),
        nullptr);
    if (!g_result_popup) {
        return;
    }

    const std::wstring header = original_only
        ? from_utf8(offline_translator::language_store_name(source_code))
        : from_utf8(
              offline_translator::language_store_name(source_code) + " > " +
              offline_translator::language_store_name(target_code));
    g_popup_header = CreateWindowW(
        L"STATIC",
        header.c_str(),
        WS_VISIBLE | WS_CHILD,
        border + pad_x,
        border + pad_y,
        content_width,
        header_height,
        g_result_popup,
        nullptr,
        nullptr,
        nullptr);
    SendMessageW(
        g_popup_header,
        WM_SETFONT,
        reinterpret_cast<WPARAM>(popup_font(PopupFont::header)),
        TRUE);
    DWORD edit_style = WS_VISIBLE | WS_CHILD | ES_MULTILINE |
        ES_AUTOVSCROLL | WS_VSCROLL | ES_READONLY;
    g_popup_result_edit = CreateWindowW(
        L"EDIT",
        text.c_str(),
        edit_style,
        border + pad_x,
        border + pad_y + header_height + 4,
        content_width,
        edit_height,
        g_result_popup,
        nullptr,
        nullptr,
        nullptr);
    SendMessageW(
        g_popup_result_edit,
        WM_SETFONT,
        reinterpret_cast<WPARAM>(popup_font(PopupFont::body)),
        TRUE);
    SetWindowSubclass(
        g_popup_result_edit,
        popup_edit_subclass,
        1,
        0);
    const int copy_x = width - border - pad_x - 104;
    g_popup_copy_button = CreateWindowW(
        L"BUTTON",
        L"Копировать",
        WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
        copy_x,
        buttons_y,
        104,
        button_height,
        g_result_popup,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kResultCopyButton)),
        nullptr,
        nullptr);
    SendMessageW(
        g_popup_copy_button,
        WM_SETFONT,
        reinterpret_cast<WPARAM>(popup_font(PopupFont::button)),
        TRUE);
    SetWindowSubclass(
        g_popup_copy_button,
        flat_button_subclass,
        1,
        0);
    const bool show_replace =
        !original_only && !g_screen_ocr_mode &&
        text.rfind(L"Ошибка:", 0) != 0;
    const bool show_add = show_replace && !g_popup_source_text.empty();
    g_popup_replace_button = nullptr;
    g_popup_add_button = nullptr;
    g_popup_original_button = nullptr;
    // Маленькая квадратная кнопка «показ оригинала»: переключает текст в
    // окне между переводом и исходным текстом.
    if (!original_only && !g_popup_source_text.empty() &&
        text.rfind(L"Ошибка:", 0) != 0 &&
        g_popup_source_text != g_popup_translation_text) {
        g_popup_original_button = CreateWindowW(
            L"BUTTON",
            L"О",
            WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX | BS_PUSHLIKE,
            border + pad_x,
            buttons_y,
            28,
            button_height,
            g_result_popup,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kResultOriginalButton)),
            nullptr,
            nullptr);
        SendMessageW(
            g_popup_original_button,
            WM_SETFONT,
            reinterpret_cast<WPARAM>(popup_font(PopupFont::button)),
            TRUE);
    }
    int next_left = copy_x;
    if (show_add) {
        next_left = copy_x - 96 - 8;
        g_popup_add_button = CreateWindowW(
            L"BUTTON",
            L"Добавить",
            WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
            next_left,
            buttons_y,
            96,
            button_height,
            g_result_popup,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kResultAddButton)),
            nullptr,
            nullptr);
        SendMessageW(
            g_popup_add_button,
            WM_SETFONT,
            reinterpret_cast<WPARAM>(popup_font(PopupFont::button)),
            TRUE);
        SetWindowSubclass(
            g_popup_add_button,
            flat_button_subclass,
            1,
            0);
    }
    if (show_replace) {
        next_left = next_left - 88 - 8;
        g_popup_replace_button = CreateWindowW(
            L"BUTTON",
            L"Замена",
            WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
            next_left,
            buttons_y,
            88,
            button_height,
            g_result_popup,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kResultReplaceButton)),
            nullptr,
            nullptr);
        SendMessageW(
            g_popup_replace_button,
            WM_SETFONT,
            reinterpret_cast<WPARAM>(popup_font(PopupFont::button)),
            TRUE);
        SetWindowSubclass(
            g_popup_replace_button,
            flat_button_subclass,
            1,
            0);
    }
    if (selectable) {
        const int close_x = next_left - 84 - 8;
        const HWND close_button = CreateWindowW(
            L"BUTTON",
            L"Закрыть",
            WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
            close_x,
            buttons_y,
            84,
            button_height,
            g_result_popup,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kResultCloseButton)),
            nullptr,
            nullptr);
        SendMessageW(
            close_button,
            WM_SETFONT,
            reinterpret_cast<WPARAM>(popup_font(PopupFont::button)),
            TRUE);
        SetWindowSubclass(
            close_button,
            flat_button_subclass,
            1,
            0);
    }
    SetFocus(g_popup_result_edit);
    localize_window(g_result_popup);
    {
        RECT bounds{};
        GetWindowRect(g_result_popup, &bounds);
        offline_translator::app_log_info(
            "popup: показано hwnd=" + std::to_string(
                reinterpret_cast<uintptr_t>(g_result_popup)) +
            " rect=" + std::to_string(bounds.left) + "," +
            std::to_string(bounds.top) + "," +
            std::to_string(bounds.right - bounds.left) + "x" +
            std::to_string(bounds.bottom - bounds.top) +
            " copy=" + std::to_string(
                reinterpret_cast<uintptr_t>(g_popup_copy_button)) +
            " replace=" + std::to_string(
                reinterpret_cast<uintptr_t>(g_popup_replace_button)) +
            " add=" + std::to_string(
                reinterpret_cast<uintptr_t>(g_popup_add_button)));
    }
    if (!selectable) {
        // Иначе всё поле выделено и Copy может взять чужое выделение.
        SendMessageW(g_popup_result_edit, EM_SETSEL, static_cast<WPARAM>(-1), 0);
    }
}

// Направление перевода выделенного текста. Если в главном окне язык
// выбран вручную (не «Авто»), переводим именно с него и на выбранный
// язык — раньше выбор пользователя игнорировался и по латинице всегда
// угадывался английский. В режиме «Авто» язык определяется по письму,
// а цель берётся из главного окна.
std::pair<std::string, std::string> selection_translation_direction(
    const std::wstring& text) {
    std::string window_target = "ru";
    try {
        if (g_target_language_combo) {
            window_target = selected_language(g_target_language_combo);
        }
    } catch (const std::exception&) {
    }
    if (!g_auto_source_language && g_source_language_combo) {
        try {
            const std::string source =
                selected_source_language(g_source_language_combo);
            if (!source.empty() && source != kAutoSourceLanguage) {
                // Если текст на языке «На язык» — меняем направление.
                const std::string detected = detect_language_optional(
                    to_utf8(text), selected_engine());
                if (!detected.empty() && detected == window_target &&
                    source != window_target) {
                    return {detected, source};
                }
                return {source, window_target};
            }
        } catch (const std::exception&) {
        }
    }
    // «Авто»: определяем язык детектором, цель берём из главного окна.
    const std::string source =
        detect_source_language(to_utf8(text), selected_engine());
    std::string target = window_target;
    if (source == target) {
        // Определённый язык совпал с «На язык»: переводим на третий язык.
        target = resolve_fallback_target(source, target);
    }
    return {source, target};
}

void start_selection_translation(HWND main_window) {
    if (!g_runtime || g_runtime->selection_busy.exchange(true)) {
        offline_translator::app_log_warn(
            "перевод выделения пропущен: уже выполняется " + runtime_flags());
        return;
    }
    std::wstring selected = g_selection.selected_text;
    while (!selected.empty() &&
           (selected.back() == L' ' || selected.back() == L'\n')) {
        selected.pop_back();
    }
    if (selected.size() < 2) {
        g_runtime->selection_busy = false;
        offline_translator::app_log_info("перевод выделения отменён: короткий текст");
        return;
    }
    const std::string text = to_utf8(selected);
    const auto direction = selection_translation_direction(selected);
    offline_translator::app_log_info(
        "перевод выделения старт chars=" + std::to_string(text.size()) + " " +
        direction.first + ">" + direction.second + " " + runtime_flags());
    const auto engine_kind = selected_engine();
    const auto root = model_root_for_kind(engine_kind);
    const std::string engine_variant =
        engine_kind == offline_translator::EngineKind::firefox
            ? (g_runtime ? g_runtime->settings.architecture : std::string{"tiny"})
            : std::string{};
    const std::string api_key = online_api_key_for_kind(engine_kind);
    auto runtime = g_runtime;
    std::thread([main_window, text, direction, engine_kind, engine_variant, api_key, root, runtime]() {
        auto result = std::make_unique<StatusPayload>();
        const auto started = GetTickCount64();
        try {
            std::lock_guard lock(runtime->mutex);
            if (runtime->closing) {
                runtime->selection_busy = false;
                return;
            }
            auto& application =
                runtime->session.acquire(engine_kind, root, engine_variant, api_key);
            result->text = from_utf8(
                application.translate(text, direction.first, direction.second)
                    .text);
            result->source_code = direction.first;
            result->target_code = direction.second;
            runtime->session.mark_loaded();
            offline_translator::app_log_info(
                "перевод выделения готов ms=" +
                std::to_string(GetTickCount64() - started) +
                " out_chars=" + std::to_string(result->text.size()));
        } catch (const std::exception& error) {
            result->failed = true;
            result->text = from_utf8(std::string("Ошибка: ") + error.what());
            offline_translator::app_log_error(
                std::string("перевод выделения ошибка ms=") +
                std::to_string(GetTickCount64() - started) + " " +
                error.what());
        }
        runtime->selection_busy = false;
        if (runtime->closing || !IsWindow(main_window)) {
            return;
        }
        PostMessageW(
            main_window,
            kSelectionResultMessage,
            0,
            reinterpret_cast<LPARAM>(result.release()));
    }).detach();
}

// Горячая клавиша перевода области экрана (Ctrl+Shift+S).
constexpr std::string_view kScreenOcrHotkeySpec = "Ctrl+Shift+S";

bool register_screen_ocr_hotkey(HWND window) {
    UnregisterHotKey(window, kScreenOcrHotkey);
    const auto parsed =
        offline_translator::parse_hotkey(std::string(kScreenOcrHotkeySpec));
    if (!parsed) {
        return false;
    }
    return RegisterHotKey(
               window,
               kScreenOcrHotkey,
               offline_translator::hotkey_win32_modifiers(*parsed),
               parsed->vk) != FALSE;
}

// Языки OCR из главного окна. «Авто»: английский и русский (если данные
// установлены) — иначе русские скриншоты читались бы латиницей.
std::vector<std::string> current_ocr_languages() {
    try {
        if (!g_auto_source_language && g_source_language_combo) {
            const std::string source =
                selected_source_language(g_source_language_combo);
            if (!source.empty() && source != kAutoSourceLanguage) {
                return {source};
            }
        }
    } catch (const std::exception&) {
    }
    if (offline_translator::ocr_paddle_ready()) {
        return {"en", "ru"};
    }
    const auto root = offline_translator::default_tessdata_root();
    std::vector<std::string> codes;
    for (const char* code : {"en", "ru"}) {
        if (offline_translator::tessdata_installed(code, root)) {
            codes.push_back(code);
        }
    }
    if (codes.empty()) {
        codes.push_back("en");
    }
    return codes;
}

// Проверяет данные OCR для языков и при согласии скачивает отсутствующие.
bool ensure_ocr_languages(
    HWND window,
    const std::vector<std::string>& ocr_languages) {
    if (offline_translator::ocr_paddle_ready()) {
        bool all_supported = !ocr_languages.empty();
        for (const auto& code : ocr_languages) {
            if (offline_translator::ppocr_script_for_language(code).empty()) {
                all_supported = false;
                break;
            }
        }
        if (all_supported) {
            return true;
        }
    }
    for (const auto& code : ocr_languages) {
        if (!offline_translator::tesseract_language_supported(code)) {
            const std::wstring message =
                L"Язык «" +
                from_utf8(offline_translator::language_store_name(code)) +
                L"» не поддерживается распознаванием текста (OCR).";
            localized_message_box(
                window,
                message.c_str(),
                L"Перевод области экрана",
                MB_ICONINFORMATION | MB_OK);
            return false;
        }
    }
    const auto tessdata_root = offline_translator::default_tessdata_root();
    std::vector<std::string> missing;
    for (const auto& code : ocr_languages) {
        if (!offline_translator::tessdata_installed(code, tessdata_root)) {
            missing.push_back(code);
        }
    }
    if (missing.empty()) {
        return true;
    }
    std::wstring names;
    for (std::size_t index = 0; index < missing.size(); ++index) {
        if (index > 0) {
            names += L", ";
        }
        names += from_utf8(
            offline_translator::language_store_name(missing[index]));
    }
    const std::wstring question =
        L"Для распознавания текста нужны языковые данные OCR «" + names +
        L"» (примерно 1–8 МБ, скачиваются один раз).\n\n"
        L"Скачать сейчас?";
    const int answer = localized_message_box(
        window,
        question.c_str(),
        L"Перевод области экрана",
        MB_ICONQUESTION | MB_YESNO);
    if (answer != IDYES) {
        return false;
    }
    set_status(L"Скачивание языковых данных OCR…");
    for (const auto& code : missing) {
        try {
            offline_translator::download_tessdata(
                code,
                [window](
                    std::uint64_t downloaded,
                    std::uint64_t total,
                    std::string_view) {
                    const int percent = total > 0
                        ? static_cast<int>(downloaded * 100 / total)
                        : 0;
                    set_status(
                        L"Скачивание языковых данных OCR… " +
                        std::to_wstring(percent) + L"%");
                    MSG message{};
                    while (PeekMessageW(&message, window, 0, 0, PM_REMOVE)) {
                        TranslateMessage(&message);
                        DispatchMessageW(&message);
                    }
                },
                tessdata_root);
        } catch (const std::exception& error) {
            const std::wstring message =
                L"Не удалось скачать языковые данные OCR:\n" +
                from_utf8(error.what());
            localized_message_box(
                window,
                message.c_str(),
                L"Перевод области экрана",
                MB_ICONERROR | MB_OK);
            return false;
        }
    }
    return true;
}

// OCR области экрана: выделение > распознавание > существующий попап.
void start_screen_ocr(HWND window) {
    if (!g_runtime || g_runtime->selection_busy.exchange(true)) {
        offline_translator::app_log_warn(
            "перевод области пропущен: уже выполняется " + runtime_flags());
        return;
    }
    const auto ocr_languages = current_ocr_languages();
    if (!ensure_ocr_languages(window, ocr_languages)) {
        g_runtime->selection_busy = false;
        return;
    }

    set_status(L"Выделите область экрана мышью (Esc — отмена)");
    if (g_status_label) {
        UpdateWindow(g_status_label);
    }
    // На время оверлея останавливаем опрос выделения, иначе он будет
    // реагировать на собственную замороженную картинку.
    if (!g_smoke_mode) {
        KillTimer(window, kSelectionPollTimer);
    }
    const auto tessdata_root = offline_translator::default_tessdata_root();
    std::wstring recognized;
    try {
        const auto image =
            offline_translator::select_screen_region_image();
        if (!g_smoke_mode) {
            SetTimer(window, kSelectionPollTimer, 40, nullptr);
        }
        if (!image) {
            g_runtime->selection_busy = false;
            set_status(L"Отменено");
            return;
        }
        offline_translator::OcrEngine engine(tessdata_root);
        engine.initialize_multi(ocr_languages);
        const auto ocr = engine.recognize(
            image->bgra.data(),
            image->width,
            image->height,
            image->width * 4,
            4);
        recognized = from_utf8(ocr.text);
        while (!recognized.empty() &&
               (recognized.back() == L' ' || recognized.back() == L'\n' ||
                recognized.back() == L'\r')) {
            recognized.pop_back();
        }
    } catch (const std::exception& error) {
        g_runtime->selection_busy = false;
        if (!g_smoke_mode) {
            SetTimer(window, kSelectionPollTimer, 40, nullptr);
        }
        const std::wstring message =
            L"Не удалось распознать текст:\n" + from_utf8(error.what());
        localized_message_box(
            window,
            message.c_str(),
            L"Перевод области экрана",
            MB_ICONERROR | MB_OK);
        return;
    }
    if (recognized.size() < 2) {
        g_runtime->selection_busy = false;
        set_status(L"Текст не распознан");
        return;
    }
    g_selection.selected_text = recognized;
    g_selection.replace_hwnd = nullptr;
    g_screen_ocr_mode = true;
    if (g_source_edit) {
        SetWindowTextW(g_source_edit, recognized.c_str());
    }
    g_runtime->selection_busy = false;
    if (ocr_only_active()) {
        // Режим «только OCR»: показываем распознанный оригинал без перевода.
        // Язык — реально определённый по распознанному тексту.
        const std::string detected =
            detect_source_language(to_utf8(recognized), selected_engine());
        set_status(L"Распознанный текст (без перевода)");
        show_result_popup(
            window,
            recognized,
            detected,
            std::string{},
            /*original_only=*/true);
        return;
    }
    // start_selection_translation сам выставит selection_busy.
    start_selection_translation(window);
}

int excel_method_combo_index(std::string_view method) {
    if (method == offline_translator::kExcelWriteCom) {
        return 1;
    }
    if (method == offline_translator::kExcelWriteAuto) {
        return 2;
    }
    return 0;
}

std::string excel_method_from_combo(HWND combo) {
    const LRESULT index = SendMessageW(combo, CB_GETCURSEL, 0, 0);
    if (index == 1) {
        return std::string{offline_translator::kExcelWriteCom};
    }
    if (index == 2) {
        return std::string{offline_translator::kExcelWriteAuto};
    }
    return std::string{offline_translator::kExcelWriteClipboard};
}

void fill_excel_method_combo(HWND combo, std::string_view selected) {
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    SendMessageW(
        combo,
        CB_ADDSTRING,
        0,
        reinterpret_cast<LPARAM>(L"Буфер обмена"));
    SendMessageW(
        combo,
        CB_ADDSTRING,
        0,
        reinterpret_cast<LPARAM>(L"COM (напрямую в Excel)"));
    SendMessageW(
        combo,
        CB_ADDSTRING,
        0,
        reinterpret_cast<LPARAM>(L"Авто (COM, затем буфер обмена)"));
    SendMessageW(
        combo,
        CB_SETCURSEL,
        excel_method_combo_index(selected),
        0);
}

// Пресеты вызова Excel-перевода: 0 — жест «двойное Ctrl+C»,
// 1..4 — готовые комбинации, 5 — своя (набирается в поле нажатием).
int excel_trigger_combo_index(
    std::string_view trigger,
    std::string_view hotkey) {
    if (trigger == offline_translator::kExcelTriggerDoubleCtrlC) {
        return 0;
    }
    if (hotkey == "Ctrl+Alt+T") {
        return 1;
    }
    if (hotkey == "Ctrl+Shift+E") {
        return 2;
    }
    if (hotkey == "Ctrl+Alt+E") {
        return 3;
    }
    if (hotkey == "Alt+E") {
        return 4;
    }
    return 5;
}

void fill_excel_trigger_combo(
    HWND combo,
    std::string_view trigger,
    std::string_view hotkey) {
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    SendMessageW(
        combo,
        CB_ADDSTRING,
        0,
        reinterpret_cast<LPARAM>(L"Двойное Ctrl+C (как Ctrl+C+C)"));
    SendMessageW(
        combo,
        CB_ADDSTRING,
        0,
        reinterpret_cast<LPARAM>(L"Ctrl+Alt+T"));
    SendMessageW(
        combo,
        CB_ADDSTRING,
        0,
        reinterpret_cast<LPARAM>(L"Ctrl+Shift+E"));
    SendMessageW(
        combo,
        CB_ADDSTRING,
        0,
        reinterpret_cast<LPARAM>(L"Ctrl+Alt+E"));
    SendMessageW(
        combo,
        CB_ADDSTRING,
        0,
        reinterpret_cast<LPARAM>(L"Alt+E"));
    SendMessageW(
        combo,
        CB_ADDSTRING,
        0,
        reinterpret_cast<LPARAM>(L"Своя комбинация…"));
    SendMessageW(
        combo,
        CB_SETCURSEL,
        excel_trigger_combo_index(trigger, hotkey),
        0);
}

void update_excel_hotkey_field_state() {
    if (!g_settings_excel_trigger || !g_settings_excel_hotkey) {
        return;
    }
    const LRESULT index =
        SendMessageW(g_settings_excel_trigger, CB_GETCURSEL, 0, 0);
    EnableWindow(
        g_settings_excel_hotkey,
        index == 0 ? FALSE : TRUE);
}

void apply_excel_trigger_selection() {
    if (!g_settings_excel_trigger || !g_settings_excel_hotkey) {
        return;
    }
    const LRESULT index =
        SendMessageW(g_settings_excel_trigger, CB_GETCURSEL, 0, 0);
    if (index >= 1 && index <= 4) {
        static const wchar_t* const specs[] = {
            nullptr,
            L"Ctrl+Alt+T",
            L"Ctrl+Shift+E",
            L"Ctrl+Alt+E",
            L"Alt+E"};
        SetWindowTextW(g_settings_excel_hotkey, specs[index]);
    }
    update_excel_hotkey_field_state();
}

// Поле горячей клавиши: комбинация записывается нажатием, а не набором.
LRESULT CALLBACK hotkey_capture_subclass(
    HWND handle,
    UINT message,
    WPARAM w_param,
    LPARAM l_param,
    UINT_PTR,
    DWORD_PTR) {
    if (message == WM_GETDLGCODE) {
        return DLGC_WANTALLKEYS | DLGC_WANTCHARS;
    }
    if (message == WM_CHAR) {
        return 0;
    }
    if (message == WM_SETFOCUS) {
        g_hotkey_capture_backup = control_text(handle);
        return DefSubclassProc(handle, message, w_param, l_param);
    }
    if (message == WM_KEYDOWN || message == WM_SYSKEYDOWN) {
        const unsigned key = static_cast<unsigned>(w_param);
        if (key == VK_SHIFT || key == VK_CONTROL || key == VK_MENU ||
            key == VK_LWIN || key == VK_RWIN) {
            return 0;
        }
        if (key == VK_TAB || key == VK_RETURN) {
            return DefSubclassProc(handle, message, w_param, l_param);
        }
        if (key == VK_ESCAPE) {
            if (!g_hotkey_capture_backup.empty()) {
                SetWindowTextW(handle, g_hotkey_capture_backup.c_str());
            }
            return 0;
        }
        offline_translator::Hotkey hotkey;
        hotkey.control = key_down(VK_CONTROL);
        hotkey.alt = key_down(VK_MENU);
        hotkey.shift = key_down(VK_SHIFT);
        hotkey.win = key_down(VK_LWIN) || key_down(VK_RWIN);
        hotkey.vk = key;
        const bool letter_or_digit =
            (key >= 'A' && key <= 'Z') || (key >= '0' && key <= '9');
        const bool function_key = key >= VK_F1 && key <= VK_F24;
        // Без модификатора разрешаем только F-клавиши: одиночная буква
        // перехватывала бы обычный ввод во всей системе.
        if (!function_key &&
            (!letter_or_digit ||
             !(hotkey.control || hotkey.alt || hotkey.shift ||
               hotkey.win))) {
            return 0;
        }
        const std::wstring spec =
            from_utf8(offline_translator::format_hotkey(hotkey));
        SetWindowTextW(handle, spec.c_str());
        SendMessageW(handle, EM_SETSEL, spec.size(), spec.size());
        if (g_settings_excel_trigger) {
            SendMessageW(
                g_settings_excel_trigger,
                CB_SETCURSEL,
                excel_trigger_combo_index(
                    offline_translator::kExcelTriggerHotkey,
                    to_utf8(spec)),
                0);
        }
        return 0;
    }
    return DefSubclassProc(handle, message, w_param, l_param);
}

bool register_excel_hotkey(HWND window, const std::string& spec) {
    UnregisterHotKey(window, kExcelHotkey);
    const auto parsed = offline_translator::parse_hotkey(spec);
    if (!parsed) {
        return false;
    }
    return RegisterHotKey(
               window,
               kExcelHotkey,
               offline_translator::hotkey_win32_modifiers(*parsed),
               parsed->vk) != FALSE;
}

bool excel_is_foreground() {
    const HWND foreground = GetForegroundWindow();
    if (!foreground) {
        return false;
    }
    wchar_t class_name[64]{};
    if (GetClassNameW(foreground, class_name, 64) &&
        _wcsicmp(class_name, L"EXCEL7") == 0) {
        return true;
    }
    const HWND root = GetAncestor(foreground, GA_ROOT);
    return root && GetClassNameW(root, class_name, 64) &&
        _wcsicmp(class_name, L"XLMAIN") == 0;
}

// Хоткей мог быть нажат с зажатыми модификаторами: не шлём Ctrl+C/V, пока
// пользователь их не отпустит, иначе send_* введёт просто «c»/«v».
void wait_for_modifiers_released() {
    for (int attempt = 0; attempt < 40; ++attempt) {
        if (!key_down(VK_CONTROL) && !key_down(VK_MENU) &&
            !key_down(VK_SHIFT) && !key_down(VK_LWIN) &&
            !key_down(VK_RWIN)) {
            return;
        }
        Sleep(25);
    }
}

// Ctrl+C из Excel как есть: TSV с табами и переводами строк, без обрезки.
std::wstring excel_capture_selection_raw() {
    offline_translator::Win32Clipboard clipboard(nullptr);
    std::wstring previous;
    try {
        previous = clipboard.get_text();
    } catch (const std::exception&) {
    }
    const std::wstring sentinel = L"__tling_excel_" +
        std::to_wstring(GetTickCount64()) + L"__";
    try {
        clipboard.set_text(sentinel);
    } catch (const std::exception&) {
        return {};
    }
    offline_translator::send_copy_keyboard_shortcut();
    std::wstring captured;
    for (int attempt = 0; attempt < 20; ++attempt) {
        Sleep(50);
        try {
            captured = clipboard.get_text();
        } catch (const std::exception&) {
            captured.clear();
        }
        if (!captured.empty() && captured != sentinel) {
            break;
        }
    }
    if (captured == sentinel) {
        captured.clear();
    }
    if (captured.empty()) {
        // Неудачный захват: не оставляем служебную метку в буфере.
        try {
            clipboard.set_text(previous);
        } catch (const std::exception&) {
        }
    }
    return captured;
}

bool excel_paste_text(const std::wstring& text, std::wstring* error) {
    try {
        offline_translator::Win32Clipboard clipboard(nullptr);
        clipboard.set_text(text);
    } catch (const std::exception& exception) {
        *error = from_utf8(std::string("буфер обмена: ") + exception.what());
        return false;
    }
    Sleep(80);
    offline_translator::send_paste_keyboard_shortcut();
    Sleep(150);
    return true;
}

// Снимок настроек перевода для фонового потока (GUI-контролы читаем до
// запуска потока).
struct ExcelJobConfig {
    bool manual_source{false};
    std::string source;
    std::string target;
    std::string fallback_target;
    offline_translator::EngineKind engine{
        offline_translator::EngineKind::argos};
    std::filesystem::path models_root;
    std::string engine_variant;
    std::string api_key;
    std::string write_method;
    std::wstring separator;
    // Перевод изображений/PDF: языки OCR и файл результата.
    std::vector<std::string> ocr_languages;
    std::filesystem::path output_path;
    // Источник: PDF/картинка на диске или готовый буфер (область экрана).
    std::filesystem::path source_path;
    std::vector<std::uint8_t> image;
    int image_width{0};
    int image_height{0};
    bool output_png{false};
};

std::pair<std::string, std::string> excel_direction(
    const ExcelJobConfig& config,
    const std::wstring& cell) {
    const std::string text = to_utf8(cell);
    if (!config.manual_source) {
        // Онлайн Google сам определяет язык лучше нашему детектору — иначе
        // турецкий/другие языки уходят как английские и не переводятся.
        if (config.engine == offline_translator::EngineKind::google) {
            return {"auto", config.target};
        }
        const std::string source =
            detect_source_language(text, config.engine);
        std::string target = config.target;
        if (source == target) {
            const std::string fallback = config.fallback_target;
            if (fallback.empty() || fallback == target ||
                fallback == source) {
                target = source == "en" ? "ru" : "en";
            } else {
                target = fallback;
            }
        }
        return {source, target};
    }
    const std::string detected =
        detect_language_optional(text, config.engine);
    if (!detected.empty() && detected == config.target &&
        config.source != config.target) {
        return {detected, config.source};
    }
    return {config.source, config.target};
}

bool translate_text_batch(
    const ExcelJobConfig& config,
    const std::shared_ptr<GuiRuntime>& runtime,
    const std::vector<std::wstring>& inputs,
    std::vector<std::wstring>* outputs,
    std::wstring* message) {
    outputs->clear();
    outputs->reserve(inputs.size());
    try {
        std::lock_guard lock(runtime->mutex);
        if (runtime->closing) {
            *message = L"Программа завершается";
            return false;
        }
        auto& application = runtime->session.acquire(
            config.engine,
            config.models_root,
            config.engine_variant,
            config.api_key);
        for (const auto& input : inputs) {
            const auto direction = excel_direction(config, input);
            const auto translated = application.translate(
                to_utf8(input),
                direction.first,
                direction.second);
            outputs->push_back(from_utf8(translated.text));
        }
        runtime->session.mark_loaded();
        return true;
    } catch (const std::exception& exception) {
        *message = L"Ошибка перевода Excel: " +
            from_utf8(exception.what());
        return false;
    }
}

bool run_excel_clipboard_job(
    const ExcelJobConfig& config,
    const std::shared_ptr<GuiRuntime>& runtime,
    std::wstring* message,
    const std::wstring& preloaded) {
    if (!excel_is_foreground()) {
        *message = L"Откройте Excel и выделите ячейки";
        return false;
    }
    // При вызове по «двойному Ctrl+C» выделение уже скопировано в буфер.
    std::wstring raw = preloaded;
    if (raw.empty()) {
        wait_for_modifiers_released();
        raw = excel_capture_selection_raw();
    }
    if (raw.empty()) {
        *message = L"Не удалось прочитать выделение Excel";
        return false;
    }
    auto grid = offline_translator::parse_excel_tsv(raw);
    const std::size_t cells = grid.cell_count();
    const std::size_t translatable = grid.translatable_count();
    if (!offline_translator::excel_selection_supported(cells, translatable)) {
        *message = cells > offline_translator::kExcelMaxCells ||
                translatable >
                    offline_translator::kExcelMaxTranslatableCells
            ? L"Слишком большой диапазон Excel"
            : L"Нет текста в выделенных ячейках";
        return false;
    }
    std::vector<std::wstring> inputs;
    std::vector<std::pair<std::size_t, std::size_t>> positions;
    for (std::size_t row = 0; row < grid.rows.size(); ++row) {
        for (std::size_t col = 0; col < grid.rows[row].size(); ++col) {
            const std::wstring& cell = grid.rows[row][col];
            if (!offline_translator::excel_cell_translatable(cell)) {
                continue;
            }
            inputs.push_back(cell);
            positions.emplace_back(row, col);
        }
    }
    std::vector<std::wstring> outputs;
    if (!translate_text_batch(config, runtime, inputs, &outputs, message)) {
        return false;
    }
    for (std::size_t index = 0; index < positions.size(); ++index) {
        const auto [row, col] = positions[index];
        grid.rows[row][col] = offline_translator::excel_combined_cell(
            grid.rows[row][col],
            outputs[index],
            config.separator);
    }
    const std::wstring result = offline_translator::serialize_excel_tsv(grid);
    if (!excel_is_foreground()) {
        // Выделение потеряно: перевод не пропадает — остаётся в буфере.
        try {
            offline_translator::Win32Clipboard clipboard(nullptr);
            clipboard.set_text(result);
        } catch (const std::exception&) {
        }
        *message = L"Excel потерял фокус — перевод скопирован в буфер";
        return true;
    }
    wait_for_modifiers_released();
    std::wstring error;
    if (!excel_paste_text(result, &error)) {
        *message = L"Ошибка: " + error;
        return false;
    }
    *message = L"Перевод записан в Excel: ячеек " +
        std::to_wstring(positions.size());
    return true;
}

bool run_excel_com_job(
    const ExcelJobConfig& config,
    const std::shared_ptr<GuiRuntime>& runtime,
    std::wstring* message,
    bool* fallback_allowed) {
    *fallback_allowed = true;
    tling_excel::ExcelSession session;
    std::wstring error;
    if (!session.attach(&error)) {
        *message = L"Не удалось подключиться к Excel (" + error + L")";
        return false;
    }
    tling_excel::SelectionData data;
    if (!session.read_selection(&data, &error)) {
        *message = L"Не удалось прочитать выделение Excel (" + error + L")";
        return false;
    }
    std::vector<std::wstring> inputs;
    std::vector<long> rows;
    std::vector<long> cols;
    for (long row = 0; row < data.rows; ++row) {
        for (long col = 0; col < data.cols; ++col) {
            const std::size_t index =
                static_cast<std::size_t>(row) *
                    static_cast<std::size_t>(data.cols) +
                static_cast<std::size_t>(col);
            if (data.formulas[index]) {
                continue;
            }
            if (!offline_translator::excel_cell_translatable(
                    data.texts[index])) {
                continue;
            }
            inputs.push_back(data.texts[index]);
            rows.push_back(row + 1);
            cols.push_back(col + 1);
        }
    }
    if (inputs.empty()) {
        *message = L"Нет текста в выделенных ячейках";
        return false;
    }
    if (inputs.size() >
        offline_translator::kExcelMaxTranslatableCells) {
        *message = L"Слишком большой диапазон Excel";
        return false;
    }
    std::vector<std::wstring> outputs;
    if (!translate_text_batch(config, runtime, inputs, &outputs, message)) {
        return false;
    }
    // После первой записи откат на буфер обмена уже нельзя: часть ячеек
    // была бы переведена дважды.
    *fallback_allowed = false;
    for (std::size_t index = 0; index < inputs.size(); ++index) {
        const std::wstring combined = offline_translator::excel_combined_cell(
            inputs[index],
            outputs[index],
            config.separator);
        if (!session.write_cell(rows[index], cols[index], combined, &error)) {
            *message = L"Не удалось записать ячейку Excel (" + error + L")";
            return false;
        }
    }
    *message = L"Перевод записан в Excel: ячеек " +
        std::to_wstring(inputs.size());
    return true;
}

// Снимок настроек перевода для фоновых задач (Excel, изображения):
// GUI-контролы читаем в GUI-потоке до запуска потока.
ExcelJobConfig capture_job_config() {
    ExcelJobConfig config;
    if (!g_runtime) {
        return config;
    }
    try {
        config.target = selected_language(g_target_language_combo);
        if (g_fallback_target_language_combo) {
            config.fallback_target =
                selected_language(g_fallback_target_language_combo);
        }
        if (!g_auto_source_language && g_source_language_combo) {
            const std::string source =
                selected_source_language(g_source_language_combo);
            if (!source.empty() && source != kAutoSourceLanguage) {
                config.manual_source = true;
                config.source = source;
            }
        }
    } catch (const std::exception&) {
        config.target = g_runtime->settings.target_language;
        config.fallback_target =
            g_runtime->settings.fallback_target_language;
    }
    if (config.target.empty()) {
        config.target = g_runtime->settings.target_language;
    }
    if (config.fallback_target.empty()) {
        config.fallback_target =
            g_runtime->settings.fallback_target_language;
    }
    config.engine = selected_engine();
    config.models_root = model_root_for_kind(config.engine);
    config.engine_variant =
        config.engine == offline_translator::EngineKind::firefox
            ? g_runtime->settings.architecture
            : std::string{};
    config.api_key = online_api_key_for_kind(config.engine);
    config.write_method = g_runtime->settings.excel_write_method;
    config.separator = from_utf8(g_runtime->settings.append_separator);
    return config;
}

void start_excel_translation(HWND main_window, std::wstring preloaded = {}) {
    if (!g_runtime || !g_runtime->settings.excel_translation) {
        return;
    }
    if (g_runtime->excel_busy.exchange(true)) {
        offline_translator::app_log_warn(
            "excel: перевод пропущен, уже выполняется " + runtime_flags());
        return;
    }
    ExcelJobConfig config = capture_job_config();
    auto runtime = g_runtime;
    offline_translator::app_log_info(
        "excel: старт метод=" + config.write_method + " " + runtime_flags());
    set_status(L"Перевожу ячейки Excel…");
    std::thread([main_window, config, runtime, preloaded = std::move(preloaded)]() {
        auto* payload = new StatusPayload();
        std::wstring message;
        bool success = false;
        const bool try_com =
            config.write_method == offline_translator::kExcelWriteCom ||
            config.write_method == offline_translator::kExcelWriteAuto;
        if (try_com) {
            bool fallback_allowed = false;
            success = run_excel_com_job(
                config,
                runtime,
                &message,
                &fallback_allowed);
            if (!success && fallback_allowed &&
                config.write_method == offline_translator::kExcelWriteAuto) {
                offline_translator::app_log_info(
                    "excel: COM не сработал (" + to_utf8(message) +
                    "), пробую буфер обмена");
                success = run_excel_clipboard_job(
                    config,
                    runtime,
                    &message,
                    preloaded);
            }
        } else {
            success = run_excel_clipboard_job(
                config,
                runtime,
                &message,
                preloaded);
        }
        runtime->excel_busy = false;
        payload->failed = !success;
        payload->text = message;
        if (runtime->closing || !IsWindow(main_window) ||
            !PostMessageW(
                main_window,
                kExcelResultMessage,
                0,
                reinterpret_cast<LPARAM>(payload))) {
            delete payload;
        }
    }).detach();
}

// Выбор файла изображения и пути сохранения результата.
std::optional<std::filesystem::path> choose_image_file(HWND owner) {
    const std::wstring title =
        offline_translator::tr(L"Выберите изображение для перевода");
    // Фильтр: строки разделяются '\0', весь список — двойным '\0'. Собираем
    // вручную, иначе конструктор wstring обрезает по первому нулю.
    std::wstring filter;
    const auto add_filter = [&filter](
        const std::wstring& label,
        const std::wstring& pattern) {
        filter += label;
        filter.push_back(L'\0');
        filter += pattern;
        filter.push_back(L'\0');
    };
    add_filter(
        std::wstring(offline_translator::tr(L"Изображения и PDF")) +
            L" (*.pdf;*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tif;*.tiff)",
        L"*.pdf;*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tif;*.tiff");
    add_filter(L"*.*", L"*.*");
    filter.push_back(L'\0');
    wchar_t file[MAX_PATH]{};
    OPENFILENAMEW options{};
    options.lStructSize = sizeof(options);
    options.hwndOwner = owner;
    options.lpstrFilter = filter.c_str();
    options.nFilterIndex = 1;
    options.lpstrFile = file;
    options.nMaxFile = MAX_PATH;
    options.lpstrTitle = title.c_str();
    options.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    if (!GetOpenFileNameW(&options)) {
        return std::nullopt;
    }
    return std::filesystem::path(file);
}

std::optional<std::filesystem::path> choose_export_path(
    HWND owner,
    const std::wstring& stem,
    const std::filesystem::path& directory) {
    const std::wstring title = offline_translator::tr(L"Сохранить перевод");
    std::wstring filter;
    filter += std::wstring(offline_translator::tr(L"Документ Word")) +
        L" (*.docx)";
    filter.push_back(L'\0');
    filter += L"*.docx";
    filter.push_back(L'\0');
    filter += std::wstring(offline_translator::tr(L"Таблица Excel")) +
        L" (*.xlsx)";
    filter.push_back(L'\0');
    filter += L"*.xlsx";
    filter.push_back(L'\0');
    filter += std::wstring(offline_translator::tr(L"Изображение с переводом")) +
        L" (*.png)";
    filter.push_back(L'\0');
    filter += L"*.png";
    filter.push_back(L'\0');
    filter.push_back(L'\0');
    const std::wstring suggested = stem + L".docx";
    const std::wstring directory_text = directory.wstring();
    wchar_t file[MAX_PATH]{};
    wcsncpy_s(file, suggested.c_str(), MAX_PATH - 1);
    OPENFILENAMEW options{};
    options.lStructSize = sizeof(options);
    options.hwndOwner = owner;
    options.lpstrFilter = filter.c_str();
    options.nFilterIndex = 1;
    options.lpstrFile = file;
    options.nMaxFile = MAX_PATH;
    options.lpstrTitle = title.c_str();
    // Расширение подставляем сами по выбранному фильтру: lpstrDefExt
    // всегда дописывал «.docx», и Excel-экспорт сохранялся как Word.
    if (!directory_text.empty()) {
        options.lpstrInitialDir = directory_text.c_str();
    }
    options.Flags =
        OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    if (!GetSaveFileNameW(&options)) {
        return std::nullopt;
    }
    std::filesystem::path result(file);
    // Имя может содержать точки (дата), поэтому ориентируемся не на
    // пустое расширение, а на то, docx/xlsx/png это или нет.
    const std::wstring extension = result.extension().wstring();
    if (_wcsicmp(extension.c_str(), L".docx") != 0 &&
        _wcsicmp(extension.c_str(), L".xlsx") != 0 &&
        _wcsicmp(extension.c_str(), L".png") != 0) {
        result += options.nFilterIndex == 2
            ? L".xlsx"
            : (options.nFilterIndex == 3 ? L".png" : L".docx");
    }
    return result;
}

// Разметка страницы из растрового изображения через OCR: автоповорот и
// границы колонок по линиям сетки. Если язык документа отличается от
// загруженных языков OCR (например, турецкий), распознаём заново нужным
// языком — иначе таблицы и короткие ячейки читаются мусором.
offline_translator::LayoutPage ocr_raster_page(
    offline_translator::OcrEngine& engine,
    const ExcelJobConfig& config,
    const std::vector<std::uint8_t>& bgra,
    int width,
    int height) {
    const auto build = [&]() {
        int rotation = 0;
        const auto layout = offline_translator::recognize_best_orientation(
            engine,
            bgra.data(),
            width,
            height,
            width * 4,
            4,
            &rotation);
        int rotated_width = width;
        int rotated_height = height;
        std::vector<std::uint8_t> rotated;
        if (rotation != 0) {
            rotated = offline_translator::rotate_bgra_image(
                bgra.data(),
                width,
                height,
                width * 4,
                rotation,
                &rotated_width,
                &rotated_height);
        }
        const std::uint8_t* grid_image =
            rotation != 0 ? rotated.data() : bgra.data();
        const auto grid_columns =
            offline_translator::detect_vertical_grid_lines(
                grid_image,
                rotated_width,
                rotated_height,
                rotated_width * 4);
        return offline_translator::build_layout_page(layout, grid_columns);
    };

    auto page = build();
    if (config.manual_source) {
        return page;
    }
    std::string sample;
    for (const auto& block : page.blocks) {
        for (const auto& line : block.lines) {
            for (const auto& cell : line.cells) {
                if (sample.size() >= 2000) {
                    break;
                }
                sample += to_utf8(cell.text);
                sample += '\n';
            }
        }
    }
    if (sample.empty()) {
        return page;
    }
    const std::string detected =
        detect_language_optional(sample, config.engine);
    if (detected.empty() ||
        std::find(
            config.ocr_languages.begin(),
            config.ocr_languages.end(),
            detected) != config.ocr_languages.end() ||
        !offline_translator::tesseract_language_supported(detected)) {
        return page;
    }
    const auto tessdata_root = offline_translator::default_tessdata_root();
    if (!offline_translator::tessdata_installed(detected, tessdata_root)) {
        try {
            offline_translator::app_log_info(
                "документ: скачиваю OCR-данные " + detected);
            offline_translator::download_tessdata(detected, {}, tessdata_root);
        } catch (const std::exception&) {
            return page;
        }
    }
    try {
        engine.initialize_multi({detected});
        offline_translator::app_log_info(
            "документ: повторное распознавание языком " + detected);
        return build();
    } catch (const std::exception&) {
        return page;
    }
}

bool process_document(
    ExcelJobConfig config,
    const std::shared_ptr<GuiRuntime>& runtime,
    std::wstring* message_out) {
    std::wstring message;
    bool success = false;
    try {
            std::vector<offline_translator::LayoutPage> pages;
            std::vector<std::vector<std::uint8_t>> page_images;
            std::vector<int> image_widths;
            std::vector<int> image_heights;
            std::vector<std::size_t> page_text_counts;

            const std::wstring source_extension =
                config.source_path.extension().wstring();
            const bool is_pdf =
                _wcsicmp(source_extension.c_str(), L".pdf") == 0;

            if (is_pdf) {
                offline_translator::PdfDocument document(config.source_path);
                if (!document.valid()) {
                    message = L"Не удалось открыть PDF";
                } else {
                    std::unique_ptr<offline_translator::OcrEngine> ocr;
                    const int count = document.page_count();
                    for (int index = 0; index < count; ++index) {
                        auto layer = document.extract_page_layer(index);
                        offline_translator::LayoutPage page;
                        std::vector<std::uint8_t> raster;
                        int raster_width = 0;
                        int raster_height = 0;
                        if (layer.words.size() >= 3) {
                            // Есть текстовый слой — точная разметка.
                            page = offline_translator::build_layout_page(layer);
                            if (config.output_png) {
                                auto rendered =
                                    document.render_page(index, 2.0f);
                                if (rendered) {
                                    raster = std::move(rendered->bgra);
                                    raster_width = rendered->width;
                                    raster_height = rendered->height;
                                }
                            }
                        } else {
                            // Скан: рендер + OCR.
                            auto rendered = document.render_page(index, 2.0f);
                            if (!rendered) {
                                continue;
                            }
                            raster = std::move(rendered->bgra);
                            raster_width = rendered->width;
                            raster_height = rendered->height;
                            if (!ocr) {
                                ocr = std::make_unique<
                                    offline_translator::OcrEngine>();
                                ocr->initialize_multi(config.ocr_languages);
                            }
                            page = ocr_raster_page(
                                *ocr,
                                config,
                                raster,
                                raster_width,
                                raster_height);
                        }
                        page_text_counts.push_back(
                            offline_translator::layout_texts(page).size());
                        pages.push_back(std::move(page));
                        page_images.push_back(std::move(raster));
                        image_widths.push_back(raster_width);
                        image_heights.push_back(raster_height);
                    }
                }
            } else {
                std::vector<std::uint8_t> image;
                int width = config.image_width;
                int height = config.image_height;
                if (!config.image.empty()) {
                    image = std::move(config.image);
                } else {
                    auto loaded =
                        offline_translator::load_image_file(config.source_path);
                    if (loaded) {
                        image = std::move(loaded->bgra);
                        width = loaded->width;
                        height = loaded->height;
                    }
                }
                if (image.empty()) {
                    message = L"Не удалось прочитать изображение";
                } else {
                    offline_translator::OcrEngine ocr;
                    ocr.initialize_multi(config.ocr_languages);
                    auto page = ocr_raster_page(ocr, config, image, width, height);
                    page_text_counts.push_back(
                        offline_translator::layout_texts(page).size());
                    if (config.output_png) {
                        page_images.push_back(image);
                    } else {
                        page_images.emplace_back();
                    }
                    image_widths.push_back(width);
                    image_heights.push_back(height);
                    pages.push_back(std::move(page));
                }
            }

            if (pages.empty()) {
                if (message.empty()) {
                    message = L"Текст не найден";
                }
            } else {
                // Язык документа определяем один раз по всему тексту:
                // короткие ячейки таблиц при поячеечном автоопределении
                // остаются без перевода.
                if (!config.manual_source) {
                    std::string sample;
                    for (const auto& page : pages) {
                        for (const auto& block : page.blocks) {
                            for (const auto& line : block.lines) {
                                for (const auto& cell : line.cells) {
                                    if (sample.size() >= 4000) {
                                        continue;
                                    }
                                    sample += to_utf8(cell.text);
                                    sample += '\n';
                                }
                            }
                        }
                    }
                    if (!sample.empty()) {
                        const std::string detected =
                            detect_language_optional(sample, config.engine);
                        if (!detected.empty() && detected != config.target) {
                            config.manual_source = true;
                            config.source = detected;
                            offline_translator::app_log_info(
                                "документ: язык источника " + detected);
                        }
                    }
                }
                std::vector<std::wstring> texts;
                for (const auto& page : pages) {
                    for (auto& text : offline_translator::layout_texts(page)) {
                        texts.push_back(std::move(text));
                    }
                }
                if (texts.empty()) {
                    message = L"Текст не найден";
                } else {
                    std::vector<std::wstring> translated;
                    if (translate_text_batch(
                            config,
                            runtime,
                            texts,
                            &translated,
                            &message)) {
                        if (message_out != nullptr) {
                            try {
                                std::ofstream dump(
                                    std::filesystem::path(
                                        config.output_path.wstring() +
                                        L".cells.txt"),
                                    std::ios::binary);
                                for (std::size_t index = 0;
                                     index < texts.size() &&
                                     index < translated.size();
                                     ++index) {
                                    dump << to_utf8(texts[index]) << "  =>  "
                                         << to_utf8(translated[index])
                                         << "\n";
                                }
                            } catch (const std::exception&) {
                            }
                        }
                        std::size_t offset = 0;
                        for (std::size_t index = 0; index < pages.size();
                             ++index) {
                            const std::size_t take = page_text_counts[index];
                            std::vector<std::wstring> slice(
                                translated.begin() + offset,
                                translated.begin() + offset + take);
                            offset += take;
                            offline_translator::apply_layout_translations(
                                &pages[index],
                                slice);
                        }
                        if (config.output_png) {
                            const std::wstring base =
                                config.output_path.stem().wstring();
                            for (std::size_t index = 0; index < pages.size();
                                 ++index) {
                                if (page_images[index].empty()) {
                                    continue;
                                }
                                std::filesystem::path target =
                                    config.output_path;
                                if (pages.size() > 1) {
                                    target =
                                        config.output_path.parent_path() /
                                        (base + L"-" +
                                         std::to_wstring(index + 1) + L".png");
                                }
                                offline_translator::render_translated_image(
                                    pages[index],
                                    page_images[index],
                                    image_widths[index],
                                    image_heights[index],
                                    target);
                            }
                        } else {
                            offline_translator::export_layout_document(
                                pages,
                                config.output_path);
                        }
                        message = L"Перевод сохранён: " +
                            config.output_path.filename().wstring();
                        success = true;
                    }
                }
            }
        } catch (const std::exception& error) {
            message = L"Ошибка перевода: " + from_utf8(error.what());
        }
    if (message_out != nullptr) {
        *message_out = message;
    }
    return success;
}

void start_document_job(HWND main_window, ExcelJobConfig config) {
    if (!g_runtime) {
        return;
    }
    if (g_runtime->image_busy.exchange(true)) {
        offline_translator::app_log_warn(
            "документ: перевод пропущен, уже выполняется " + runtime_flags());
        return;
    }
    auto runtime = g_runtime;
    set_status(L"Перевожу документ…");
    std::thread([main_window, config = std::move(config), runtime]() mutable {
        std::wstring message;
        const bool success =
            process_document(std::move(config), runtime, &message);
        runtime->image_busy = false;
        auto* payload = new StatusPayload();
        payload->failed = !success;
        payload->text = message;
        if (runtime->closing || !IsWindow(main_window) ||
            !PostMessageW(
                main_window,
                kImageResultMessage,
                0,
                reinterpret_cast<LPARAM>(payload))) {
            delete payload;
        }
    }).detach();
}

void start_image_path_translation(
    HWND window,
    const std::filesystem::path& source) {
    if (!g_runtime) {
        return;
    }
    if (g_runtime->image_busy) {
        set_status(L"Уже перевожу…");
        return;
    }
    const bool is_pdf =
        _wcsicmp(source.extension().c_str(), L".pdf") == 0;
    std::vector<std::string> ocr_languages = current_ocr_languages();
    if (is_pdf) {
        // Для текстовых PDF OCR не нужен; на сканах используем только
        // установленные языковые данные.
        std::vector<std::string> installed;
        const auto root = offline_translator::default_tessdata_root();
        for (const auto& code : ocr_languages) {
            if (offline_translator::tessdata_installed(code, root)) {
                installed.push_back(code);
            }
        }
        if (!installed.empty()) {
            ocr_languages = installed;
        }
    } else if (!ensure_ocr_languages(window, ocr_languages)) {
        return;
    }
    const std::wstring stem = source.stem().wstring() + L" — перевод";
    const auto output = choose_export_path(window, stem, source.parent_path());
    if (!output) {
        return;
    }
    ExcelJobConfig config = capture_job_config();
    config.ocr_languages = ocr_languages;
    config.output_path = *output;
    config.source_path = source;
    config.output_png =
        _wcsicmp(output->extension().c_str(), L".png") == 0;
    start_document_job(window, std::move(config));
}

void start_image_file_translation(HWND window) {
    const auto source = choose_image_file(window);
    if (!source) {
        return;
    }
    start_image_path_translation(window, *source);
}

// Область экрана > файл Word/Excel.
void start_image_region_translation(HWND window) {
    if (!g_runtime) {
        return;
    }
    if (g_runtime->image_busy) {
        set_status(L"Уже перевожу изображение…");
        return;
    }
    const auto ocr_languages = current_ocr_languages();
    if (!ensure_ocr_languages(window, ocr_languages)) {
        return;
    }
    set_status(L"Выделите область экрана мышью (Esc — отмена)");
    if (g_status_label) {
        UpdateWindow(g_status_label);
    }
    if (!g_smoke_mode) {
        KillTimer(window, kSelectionPollTimer);
    }
    auto image = offline_translator::select_screen_region_image(2);
    if (!g_smoke_mode) {
        SetTimer(window, kSelectionPollTimer, 40, nullptr);
    }
    if (!image) {
        set_status(L"Отменено");
        return;
    }
    const auto output =
        choose_export_path(
            window,
            offline_translator::tr(L"Перевод изображения"),
            {});
    if (!output) {
        return;
    }
    ExcelJobConfig config = capture_job_config();
    config.ocr_languages = ocr_languages;
    config.output_path = *output;
    config.output_png =
        _wcsicmp(output->extension().c_str(), L".png") == 0;
    config.image = std::move(image->bgra);
    config.image_width = image->width;
    config.image_height = image->height;
    start_document_job(window, std::move(config));
}

// Плавающая кнопка: перетащите на неё файл изображения.
constexpr wchar_t kFloatButtonClass[] = L"TLingImageFloatButton";

void paint_float_button(HWND window) {
    PAINTSTRUCT paint{};
    HDC dc = BeginPaint(window, &paint);
    RECT client{};
    GetClientRect(window, &client);
    const UiTheme theme = current_ui_theme();
    HBRUSH background = CreateSolidBrush(theme.background);
    FillRect(dc, &client, background);
    DeleteObject(background);
    HBRUSH frame = CreateSolidBrush(theme.border);
    RECT border = client;
    FrameRect(dc, &border, frame);
    DeleteObject(frame);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, theme.button_text);
    SelectObject(dc, popup_font(PopupFont::body));
    DrawTextW(
        dc,
        L"Aa",
        -1,
        &client,
        DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    EndPaint(window, &paint);
}

LRESULT CALLBACK float_button_proc_impl(
    HWND window,
    UINT message,
    WPARAM w_param,
    LPARAM l_param) {
    if (message == WM_NCHITTEST) {
        return HTCAPTION;  // окно таскается за любую точку
    }
    if (message == WM_NCLBUTTONDBLCLK && g_runtime) {
        start_image_file_translation(g_runtime->main_window);
        return 0;
    }
    if (message == WM_DROPFILES) {
        wchar_t file[MAX_PATH]{};
        const HDROP drop = reinterpret_cast<HDROP>(w_param);
        if (DragQueryFileW(drop, 0, file, MAX_PATH) > 0 && g_runtime) {
            DragFinish(drop);
            start_image_path_translation(
                g_runtime->main_window,
                std::filesystem::path(file));
            return 0;
        }
        DragFinish(drop);
        return 0;
    }
    if (message == WM_PAINT) {
        if (!g_float_button_uses_icon) {
            paint_float_button(window);
        }
        return 0;
    }
    if (message == WM_ERASEBKGND) {
        return 1;
    }
    if (message == WM_DESTROY) {
        if (g_float_button == window) {
            g_float_button = nullptr;
            g_float_button_uses_icon = false;
        }
        return 0;
    }
    return DefWindowProcW(window, message, w_param, l_param);
}

LRESULT CALLBACK float_button_proc(
    HWND window,
    UINT message,
    WPARAM w_param,
    LPARAM l_param) {
    try {
        return float_button_proc_impl(window, message, w_param, l_param);
    } catch (const std::exception& error) {
        offline_translator::app_log_error(
            std::string("ошибка плавающей кнопки: ") + error.what());
        return 0;
    }
}

void remove_float_tooltip() {
    if (g_float_tooltip) {
        DestroyWindow(g_float_tooltip);
        g_float_tooltip = nullptr;
    }
}

void add_float_tooltip() {
    if (!g_float_button || g_float_tooltip) {
        return;
    }
    const HINSTANCE instance = g_runtime && g_runtime->main_window
        ? window_instance(g_runtime->main_window)
        : GetModuleHandleW(nullptr);
    g_float_tooltip = CreateWindowExW(
        WS_EX_TOPMOST,
        TOOLTIPS_CLASSW,
        nullptr,
        WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        g_float_button,
        nullptr,
        instance,
        nullptr);
    if (!g_float_tooltip) {
        return;
    }
    static std::wstring tooltip_text;
    tooltip_text = offline_translator::tr(
        L"Перетащите файл сюда или дважды кликните");
    TOOLINFOW tool{};
    tool.cbSize = sizeof(tool);
    tool.uFlags = TTF_SUBCLASS | TTF_IDISHWND;
    tool.hwnd = g_float_button;
    tool.uId = reinterpret_cast<UINT_PTR>(g_float_button);
    tool.lpszText = const_cast<LPWSTR>(tooltip_text.c_str());
    SendMessageW(
        g_float_tooltip,
        TTM_ADDTOOLW,
        0,
        reinterpret_cast<LPARAM>(&tool));
}

void show_float_button(bool show, bool announce = true) {
    if (show) {
        if (g_float_button) {
            return;
        }
        RECT work{};
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
        int x = work.right - 24 - kSelectionIconSize;
        int y = work.top + (work.bottom - work.top) / 3;
        if (g_runtime && g_runtime->settings.image_float_button_x >= 0) {
            x = g_runtime->settings.image_float_button_x;
        }
        if (g_runtime && g_runtime->settings.image_float_button_y >= 0) {
            y = g_runtime->settings.image_float_button_y;
        }
        clamp_point_to_work_area(kSelectionIconSize, kSelectionIconSize, x, y);
        g_float_button_uses_icon = false;
        HBITMAP icon_bitmap = nullptr;
        const bool have_icon = compose_selection_icon_bitmap(&icon_bitmap);
        g_float_button = CreateWindowExW(
            WS_EX_TOPMOST | WS_EX_TOOLWINDOW |
                (have_icon ? WS_EX_LAYERED : 0),
            kFloatButtonClass,
            L"TLing",
            WS_POPUP | WS_VISIBLE,
            x,
            y,
            kSelectionIconSize,
            kSelectionIconSize,
            g_runtime ? g_runtime->main_window : nullptr,
            nullptr,
            g_runtime ? window_instance(g_runtime->main_window)
                      : GetModuleHandleW(nullptr),
            nullptr);
        if (!g_float_button) {
            if (icon_bitmap) {
                DeleteObject(icon_bitmap);
            }
            return;
        }
        if (have_icon &&
            apply_layered_icon(g_float_button, icon_bitmap, x, y)) {
            g_float_button_uses_icon = true;
        } else {
            SetWindowLongPtrW(
                g_float_button,
                GWL_EXSTYLE,
                WS_EX_TOPMOST | WS_EX_TOOLWINDOW);
            SetWindowLongPtrW(
                g_float_button,
                GWL_STYLE,
                WS_POPUP | WS_BORDER | WS_VISIBLE);
        }
        if (icon_bitmap) {
            DeleteObject(icon_bitmap);
        }
        DragAcceptFiles(g_float_button, TRUE);
        add_float_tooltip();
    } else if (g_float_button) {
        RECT bounds{};
        if (GetWindowRect(g_float_button, &bounds)) {
            update_runtime_settings(
                [&bounds](offline_translator::AppSettings& settings) {
                    settings.image_float_button_x = bounds.left;
                    settings.image_float_button_y = bounds.top;
                });
        }
        remove_float_tooltip();
        DestroyWindow(g_float_button);
        g_float_button = nullptr;
    }
    offline_translator::AppSettings snapshot;
    update_runtime_settings(
        [show, &snapshot](offline_translator::AppSettings& settings) {
            settings.image_float_button = show;
            snapshot = settings;
        });
    if (!g_smoke_mode && announce) {
        try {
            offline_translator::save_settings(snapshot);
        } catch (const std::exception&) {
        }
    }
    if (announce) {
        show_tray_balloon(
            L"TLing",
            show ? offline_translator::tr(L"Плавающая кнопка включена")
                 : offline_translator::tr(L"Плавающая кнопка выключена"),
            false);
    }
}

void capture_and_show_button(HWND main_window, int cursor_x, int cursor_y) {
    // Захват идёт в GUI-потоке; повторный вход через прокачанные сообщения
    // должен быть невозможен.
    static std::atomic<bool> capturing{false};
    if (capturing.exchange(true)) {
        return;
    }
    struct CaptureGuard {
        std::atomic<bool>& flag;
        ~CaptureGuard() {
            flag = false;
        }
    } guard{capturing};
    try {
        const std::wstring selected =
            offline_translator::capture_selected_text_win32(
                main_window,
                g_runtime && g_runtime->settings.auto_copy_selection);
        sel_log("captured: size=" + std::to_string(selected.size()));
        if (selected.size() < 2) {
            return;
        }
        if (g_runtime && g_runtime->settings.auto_copy_selection) {
            try {
                offline_translator::add_clipboard_history_item(to_utf8(selected));
            } catch (const std::exception&) {
            }
        }
        g_selection.selected_text = selected;
        g_selection.replace_hwnd = g_selection.press_hwnd;
        show_selection_button(cursor_x, cursor_y, main_window);
        sel_log("button shown");
    } catch (const std::exception& error) {
        sel_log(std::string("capture error: ") + error.what());
    }
}

// Копирует выделение в буфер без показа кнопки перевода
// (режим «Только Ctrl+C+C» или не сработавший модификатор).
void capture_selection_only(HWND main_window, bool translate_now = false) {
    static std::atomic<bool> capturing{false};
    if (capturing.exchange(true)) {
        return;
    }
    struct CaptureGuard {
        std::atomic<bool>& flag;
        ~CaptureGuard() {
            flag = false;
        }
    } guard{capturing};
    try {
        const std::wstring selected =
            offline_translator::capture_selected_text_win32(main_window, true);
        sel_log("copy-only: size=" + std::to_string(selected.size()));
        if (selected.size() < 2) {
            return;
        }
        g_selection.selected_text = selected;
        g_selection.replace_hwnd = g_selection.press_hwnd;
        try {
            offline_translator::add_clipboard_history_item(to_utf8(selected));
        } catch (const std::exception&) {
        }
        if (translate_now) {
            // Турбо-перевод: текст уже выделен, кнопка у курсора не нужна.
            hide_selection_button();
            start_selection_translation(main_window);
        }
    } catch (const std::exception& error) {
        sel_log(std::string("copy-only error: ") + error.what());
    }
}

void finish_double_ctrl_c(HWND main_window) {
    try {
        const std::wstring selected = clipboard_text();
        // Excel: «двойное Ctrl+C» переводит выделенные ячейки на месте.
        if (selected.size() >= 2 && g_runtime &&
            g_runtime->settings.excel_translation &&
            g_runtime->settings.excel_trigger ==
                offline_translator::kExcelTriggerDoubleCtrlC &&
            excel_is_foreground()) {
            offline_translator::app_log_info(
                "Ctrl+C+C: перевод ячеек Excel");
            hide_selection_button();
            start_excel_translation(main_window, selected);
            return;
        }
        if (selected.size() >= 2) {
            g_selection.selected_text = selected;
            g_selection.replace_hwnd = GetForegroundWindow();
            try {
                offline_translator::add_clipboard_history_item(to_utf8(selected));
            } catch (const std::exception&) {
            }
            hide_selection_button();
            offline_translator::app_log_info(
                "Ctrl+C+C буфер chars=" + std::to_string(selected.size()));
            start_selection_translation(main_window);
        } else {
            offline_translator::app_log_warn("Ctrl+C+C: буфер пуст или слишком короткий");
        }
    } catch (const std::exception& error) {
        offline_translator::app_log_error(
            std::string("Ctrl+C+C ошибка чтения буфера: ") + error.what());
    }
}

void poll_double_ctrl_c(HWND main_window) {
    if (!g_runtime) {
        return;
    }
    if (g_selection.pending_ctrl_c_c) {
        if (monotonic_seconds() >= g_selection.ctrl_c_c_ready_at) {
            g_selection.pending_ctrl_c_c = false;
            finish_double_ctrl_c(main_window);
        }
    }
    if (!g_runtime->settings.double_ctrl_c_translation) {
        g_selection.last_ctrl_c_time = 0;
        g_selection.was_c_pressed = key_down('C');
        return;
    }
    const bool ctrl_pressed = key_down(VK_CONTROL);
    const bool c_pressed = key_down('C');
    if (!ctrl_pressed) {
        g_selection.last_ctrl_c_time = 0;
    }
    if (c_pressed && !g_selection.was_c_pressed && ctrl_pressed &&
        !g_selection.pending_ctrl_c_c) {
        const double now = monotonic_seconds();
        if (offline_translator::should_trigger_double_ctrl_c(
                g_selection.last_ctrl_c_time,
                now,
                ctrl_pressed,
                true)) {
            g_selection.last_ctrl_c_time = 0;
            g_selection.pending_ctrl_c_c = true;
            g_selection.ctrl_c_c_ready_at = now + 0.08;
        } else {
            g_selection.last_ctrl_c_time = now;
        }
    }
    g_selection.was_c_pressed = c_pressed;
}

void poll_selection(HWND main_window) {
    if (g_smoke_mode || !g_runtime) {
        return;
    }
    POINT cursor{};
    GetCursorPos(&cursor);
    poll_double_ctrl_c(main_window);
    const bool pressed = key_down(VK_LBUTTON);
    if (pressed && !g_selection.was_pressed) {
        if (is_over_our_popup(cursor.x, cursor.y)) {
            g_selection.press_active = false;
        } else {
            HWND hwnd = WindowFromPoint(cursor);
            g_selection.press_x = cursor.x;
            g_selection.press_y = cursor.y;
            g_selection.press_time = monotonic_seconds();
            g_selection.press_hwnd = hwnd ? GetAncestor(hwnd, GA_ROOT) : nullptr;
            RECT origin{};
            if (g_selection.press_hwnd &&
                GetWindowRect(g_selection.press_hwnd, &origin)) {
                g_selection.press_origin.x = origin.left;
                g_selection.press_origin.y = origin.top;
            } else {
                g_selection.press_origin = cursor;
            }
            g_selection.press_is_client = is_client_hit(cursor.x, cursor.y, hwnd);
            g_selection.gesture_invalid = !g_selection.press_is_client;
            g_selection.press_active = true;
            sel_log("press: hwnd=" +
                    std::to_string(reinterpret_cast<uintptr_t>(
                        g_selection.press_hwnd)) +
                    " client=" + std::to_string(g_selection.press_is_client));
        }
    } else if (pressed && g_selection.was_pressed && g_selection.press_active) {
        if (!g_selection.gesture_invalid) {
            if (window_being_moved()) {
                g_selection.gesture_invalid = true;
            } else if (g_selection.press_hwnd) {
                RECT origin{};
                if (GetWindowRect(g_selection.press_hwnd, &origin)) {
                    if (std::abs(origin.left - g_selection.press_origin.x) >= 4 ||
                        std::abs(origin.top - g_selection.press_origin.y) >= 4) {
                        g_selection.gesture_invalid = true;
                    }
                }
            }
        }
    } else if (!pressed && g_selection.was_pressed && g_selection.press_active) {
        const double now = monotonic_seconds();
        const int drag_distance = std::max(
            std::abs(cursor.x - g_selection.press_x),
            std::abs(cursor.y - g_selection.press_y));
        const double drag_duration = now - g_selection.press_time;
        bool window_moved = false;
        if (g_selection.press_hwnd) {
            RECT origin{};
            if (GetWindowRect(g_selection.press_hwnd, &origin)) {
                window_moved =
                    std::abs(origin.left - g_selection.press_origin.x) >= 4 ||
                    std::abs(origin.top - g_selection.press_origin.y) >= 4;
            }
        }
        const bool is_double_click =
            (now - g_selection.last_up_time) < 0.35 &&
            g_selection.press_hwnd != nullptr &&
            g_selection.press_hwnd == g_selection.last_up_hwnd &&
            std::abs(cursor.x - g_selection.last_up_x) <= 6 &&
            std::abs(cursor.y - g_selection.last_up_y) <= 6 &&
            g_selection.press_is_client;
        g_selection.last_up_time = now;
        g_selection.last_up_hwnd = g_selection.press_hwnd;
        g_selection.last_up_x = cursor.x;
        g_selection.last_up_y = cursor.y;
        const bool ctrl_now = key_down(VK_CONTROL);
        const bool alt_now = key_down(VK_MENU);
        const bool shift_now = key_down(VK_SHIFT);
        const bool copy_or_paste_now = offline_translator::should_skip_selection_copy(
            key_down('C'),
            key_down('V'));
        const bool gesture_ok = !g_selection.gesture_invalid &&
            !is_over_our_popup(cursor.x, cursor.y) &&
            !copy_or_paste_now &&
            offline_translator::should_capture_selection(
                g_selection.press_is_client,
                window_moved,
                drag_distance,
                drag_duration,
                is_double_click);
        const bool modifier_ok =
            offline_translator::should_show_selection_button(
                g_runtime->settings.popup_modifier,
                ctrl_now,
                alt_now,
                shift_now);
        sel_log(
            "release: dist=" + std::to_string(drag_distance) + " dur=" +
            std::to_string(drag_duration) + " dbl=" +
            std::to_string(is_double_click) + " moved=" +
            std::to_string(window_moved) + " invalid=" +
            std::to_string(g_selection.gesture_invalid) + " ctrl=" +
            std::to_string(ctrl_now) + " modifier=" +
            g_runtime->settings.popup_modifier +
            " skip_copy=" + std::to_string(copy_or_paste_now) +
            " gesture=" + std::to_string(gesture_ok) +
            " popup=" + std::to_string(g_runtime->settings.selection_popup_enabled));
        // Антидубль: двойной клик или быстрый повтор не должны запускать
        // второй захват поверх первого.
        const bool recent_capture =
            (now - g_selection.last_capture_time) < 0.35;
        if (!recent_capture && gesture_ok && g_runtime->settings.turbo_translation) {
            // Турбо-перевод: выделили текст — сразу переводим, без кнопки
            // у курсора и без двойного Ctrl+C.
            g_selection.last_capture_time = now;
            capture_selection_only(main_window, true);
        } else if (!recent_capture && gesture_ok &&
            g_runtime->settings.selection_popup_enabled && modifier_ok) {
            g_selection.last_capture_time = now;
            capture_and_show_button(main_window, cursor.x, cursor.y);
        } else if (!recent_capture && gesture_ok &&
                   g_runtime->settings.auto_copy_selection) {
            // Копируем выделение в буфер даже без кнопки
            // (режим «Только Ctrl+C+C» или не сработал модификатор).
            g_selection.last_capture_time = now;
            capture_selection_only(main_window);
        }
        g_selection.press_active = false;
    }
    g_selection.was_pressed = pressed;
}

void close_settings_window();
void start_update_check();
void start_update_install();
void open_releases_page();
void open_settings_window(HWND parent, HINSTANCE instance, bool check_updates = false);
// Из сообщения «… Установите: tr->en» достаём язык, для которого нужно
// скачать модель: у пары en->ru это ru, у пары tr->en — tr. Именно на нём
// фильтруется список пакетов.
std::wstring missing_model_filter(const std::wstring& error_text) {
    const std::wstring marker =
        from_utf8(std::string(offline_translator::kMissingModelMarker));
    const auto marker_pos = error_text.find(marker);
    if (marker_pos == std::wstring::npos) {
        return {};
    }
    const std::wstring tail =
        error_text.substr(marker_pos + marker.size());
    const auto arrow = tail.find(L"->");
    if (arrow == std::wstring::npos || arrow == 0) {
        return {};
    }
    std::size_t start = arrow;
    while (start > 0) {
        const wchar_t ch = tail[start - 1];
        if ((ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z') ||
            (ch >= L'0' && ch <= L'9')) {
            --start;
        } else {
            break;
        }
    }
    if (start == arrow) {
        return {};
    }
    const std::wstring source_code = tail.substr(start, arrow - start);
    std::size_t end = arrow + 2;
    while (end < tail.size()) {
        const wchar_t ch = tail[end];
        if ((ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z') ||
            (ch >= L'0' && ch <= L'9')) {
            ++end;
        } else {
            break;
        }
    }
    const std::wstring target_code = tail.substr(arrow + 2, end - arrow - 2);
    // Английский — лишь промежуточная ступень, фильтруем по другому языку.
    if (!source_code.empty() && source_code != L"en") {
        return source_code;
    }
    return target_code;
}

void open_packages_window(
    HWND parent,
    HINSTANCE instance,
    const std::wstring& language_filter = std::wstring{});

// Окно настроек: слева сайдбар со страницами (Языки/Поведение/Темы/
// О программе), справа панель содержимого с вертикальным ползунком.
// Контентные контролы — дети панели; прокрутка сдвигает панель целиком,
// сайдбар и кнопки Сохранить/Отмена остаются на месте.
int g_settings_scroll{0};
int g_settings_page{1};
HWND g_settings_panel = nullptr;
HWND g_settings_nav[5]{};
HWND g_settings_save = nullptr;
HWND g_settings_cancel = nullptr;

constexpr int kSettingsSidebarWidth = 170;
constexpr int kSettingsNavTop = 16;
constexpr int kSettingsNavHeight = 46;
constexpr int kSettingsNavGap = 18;
constexpr wchar_t kSettingsPageProperty[] = L"ot_page";
constexpr int kSettingsPageLanguages = 0;
constexpr int kSettingsPageBehavior = 1;
constexpr int kSettingsPageThemes = 2;
constexpr int kSettingsPageLanguage = 3;
constexpr int kSettingsPageAbout = 4;

constexpr int kSettingsScrollLine = 28;
constexpr int kSettingsScrollPageLines = 3;

HFONT settings_section_font() {
    static HFONT font = [] {
        HDC dc = GetDC(nullptr);
        const int height = -MulDiv(11, GetDeviceCaps(dc, LOGPIXELSY), 72);
        ReleaseDC(nullptr, dc);
        return CreateFontW(
            height, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    }();
    return font;
}

HFONT settings_nav_font(bool bold) {
    static HFONT regular = [] {
        HDC dc = GetDC(nullptr);
        const int height = -MulDiv(12, GetDeviceCaps(dc, LOGPIXELSY), 72);
        ReleaseDC(nullptr, dc);
        return CreateFontW(
            height, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    }();
    static HFONT heavy = [] {
        HDC dc = GetDC(nullptr);
        const int height = -MulDiv(12, GetDeviceCaps(dc, LOGPIXELSY), 72);
        ReleaseDC(nullptr, dc);
        return CreateFontW(
            height, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    }();
    return bold ? heavy : regular;
}

// Высота контента текущей страницы: видимые дети панели.
int settings_content_height(HWND panel) {
    if (!panel) {
        return 0;
    }
    int max_bottom = 0;
    struct Ctx {
        HWND panel;
        int max_bottom;
    } ctx{panel, 0};
    EnumChildWindows(
        panel,
        [](HWND child, LPARAM lparam) -> BOOL {
            auto* c = reinterpret_cast<Ctx*>(lparam);
            if (!IsWindowVisible(child)) {
                return TRUE;
            }
            RECT bounds{};
            GetWindowRect(child, &bounds);
            MapWindowPoints(nullptr, c->panel, reinterpret_cast<POINT*>(&bounds), 2);
            if (bounds.bottom > c->max_bottom) {
                c->max_bottom = bounds.bottom;
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&ctx));
    return ctx.max_bottom > 0 ? ctx.max_bottom + 14 : 0;
}

void settings_apply_scroll(HWND window, int new_pos) {
    if (!g_settings_panel) {
        return;
    }
    RECT client{};
    GetClientRect(window, &client);
    SCROLLINFO info{sizeof(SCROLLINFO)};
    info.fMask = SIF_RANGE | SIF_PAGE;
    GetScrollInfo(window, SB_VERT, &info);
    const int max_pos =
        (int)info.nMax >= (int)info.nPage
            ? (int)info.nMax - (int)info.nPage + 1
            : 0;
    int clamped = new_pos;
    if (clamped < 0) {
        clamped = 0;
    }
    if (clamped > max_pos) {
        clamped = max_pos;
    }
    g_settings_scroll = clamped;
    SetWindowPos(
        g_settings_panel,
        nullptr,
        kSettingsSidebarWidth + 6,
        -clamped,
        0,
        0,
        SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    SCROLLINFO pos{sizeof(SCROLLINFO)};
    pos.fMask = SIF_POS;
    pos.nPos = clamped;
    SetScrollInfo(window, SB_VERT, &pos, TRUE);
}

void settings_relayout(HWND window) {
    RECT client{};
    GetClientRect(window, &client);
    const int content = settings_content_height(g_settings_panel);
    const int panel_width =
        client.right - kSettingsSidebarWidth - 8 - 2;
    // Панель не заходит в полосу кнопок Сохранить/Отмена внизу.
    const int strip = 48;
    const int panel_height =
        content > client.bottom - strip
            ? content
            : client.bottom - strip;
    if (g_settings_panel) {
        SetWindowPos(
            g_settings_panel,
            nullptr,
            kSettingsSidebarWidth + 6,
            -g_settings_scroll,
            panel_width,
            panel_height,
            SWP_NOZORDER | SWP_NOACTIVATE);
    }
    SCROLLINFO info{sizeof(SCROLLINFO)};
    info.fMask = SIF_RANGE | SIF_PAGE | SIF_DISABLENOSCROLL;
    info.nMin = 0;
    info.nMax = content;
    info.nPage = static_cast<UINT>(client.bottom);
    info.nPos = g_settings_scroll;
    SetScrollInfo(window, SB_VERT, &info, TRUE);
    settings_apply_scroll(window, g_settings_scroll);
    // Сохранить/Отмена — фиксированный низ правой области, всегда поверх панели.
    if (g_settings_save && g_settings_cancel) {
        const int y = client.bottom - 40;
        SetWindowPos(
            g_settings_cancel,
            HWND_TOP,
            client.right - 124,
            y,
            0,
            0,
            SWP_NOSIZE | SWP_NOACTIVATE);
        SetWindowPos(
            g_settings_save,
            HWND_TOP,
            client.right - 248,
            y,
            0,
            0,
            SWP_NOSIZE | SWP_NOACTIVATE);
    }
}

void settings_show_page(HWND window, int page) {
    g_settings_page = page;
    if (g_settings_panel) {
        EnumChildWindows(
            g_settings_panel,
            [](HWND child, LPARAM lparam) -> BOOL {
                const INT_PTR child_page =
                    reinterpret_cast<INT_PTR>(GetPropW(
                        child, kSettingsPageProperty));
                ShowWindow(
                    child,
                    child_page == static_cast<INT_PTR>(lparam) ? SW_SHOW
                                                               : SW_HIDE);
                return TRUE;
            },
            static_cast<LPARAM>(page));
    }
    for (int index = 0; index < 5; ++index) {
        if (g_settings_nav[index]) {
            InvalidateRect(g_settings_nav[index], nullptr, FALSE);
        }
    }
    g_settings_scroll = 0;
    settings_relayout(window);
}

void set_update_status(const std::wstring& text) {
    if (g_settings_update_status) {
        SetWindowTextW(g_settings_update_status, text.c_str());
    }
}

void set_update_controls_busy(bool busy) {
    g_update_busy = busy;
    const BOOL enabled = busy ? FALSE : TRUE;
    if (g_settings_check_update) {
        EnableWindow(g_settings_check_update, enabled);
    }
    if (g_settings_open_releases) {
        EnableWindow(g_settings_open_releases, enabled);
    }
    const bool can_install = !busy && g_pending_update && g_pending_update->asset;
    if (g_settings_install_update) {
        EnableWindow(g_settings_install_update, can_install ? TRUE : FALSE);
    }
}

void open_releases_page() {
    const std::wstring url =
        from_utf8(std::string(offline_translator::kGithubReleasesPage));
    ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void show_tray_balloon(
    const std::wstring& title,
    const std::wstring& text,
    bool warning) {
    if (!g_tray_added) {
        return;
    }
    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd = g_tray_data.hWnd;
    data.uID = g_tray_data.uID;
    std::wstring body = text;
    // szInfo ограничен 256 символами, длинный текст обрезаем.
    if (body.size() > 200) {
        body.resize(200);
        body += L"...";
    }
    wcscpy_s(data.szInfoTitle, title.c_str());
    wcscpy_s(data.szInfo, body.c_str());
    data.uFlags = NIF_INFO;
    data.dwInfoFlags = NIIF_INFO | (warning ? NIIF_WARNING : NIIF_NONE);
    Shell_NotifyIconW(NIM_MODIFY, &data);
}

void start_update_check() {
    if (g_update_busy) {
        return;
    }
    HWND settings = g_runtime ? g_runtime->settings_window : nullptr;
    HWND main_window = g_runtime ? g_runtime->main_window : nullptr;
    if (!settings && !main_window) {
        return;
    }
    // Результат ждут там, откуда нажали: в окне настроек, если оно
    // открыто, иначе — во всплывающем сообщении у иконки в трее.
    HWND target = settings ? settings : main_window;
    g_pending_update.reset();
    set_update_controls_busy(true);
    set_update_status(L"Проверяю GitHub...");
    std::thread([target]() {
        auto* payload = new UpdateCheckPayload{};
        try {
            const auto info = offline_translator::check_for_update(
                offline_translator::kAppVersion,
                executable_path().parent_path());
            if (!info) {
                payload->text =
                    L"Установлена последняя версия (" +
                    from_utf8(std::string(offline_translator::kAppVersionDisplay)) +
                    L").";
            } else {
                payload->info = info;
                std::wstring text =
                    L"Доступна " + from_utf8(info->release.title) + L" (" +
                    from_utf8(info->release.tag) + L").\n";
                if (info->asset) {
                    text += L"Файл: " + from_utf8(info->asset->name);
                } else {
                    text +=
                        L"Подходящего файла установки в релизе нет — откройте страницу релизов.";
                }
                payload->text = std::move(text);
            }
        } catch (const std::exception& error) {
            payload->failed = true;
            payload->text =
                L"Не удалось проверить обновления: " + from_utf8(error.what());
        }
        if (!target || !IsWindow(target) ||
            !PostMessageW(
                target,
                kUpdateCheckDoneMessage,
                0,
                reinterpret_cast<LPARAM>(payload))) {
            delete payload;
        }
    }).detach();
}

void start_update_install() {
    if (g_update_busy || !g_pending_update) {
        return;
    }
    if (!g_pending_update->asset) {
        open_releases_page();
        return;
    }
    HWND settings = g_runtime ? g_runtime->settings_window : nullptr;
    HWND main_window = g_runtime ? g_runtime->main_window : nullptr;
    const auto info = *g_pending_update;
    set_update_controls_busy(true);
    set_update_status(L"Скачиваю обновление...");
    std::thread([settings, main_window, info]() {
        auto* payload = new UpdateInstallPayload{};
        try {
            const auto folder =
                std::filesystem::temp_directory_path() / "offline-translator-update";
            const std::filesystem::path destination =
                folder / std::filesystem::u8path(info.asset->name);
            offline_translator::download_update_asset(*info.asset, destination);
            const auto action = offline_translator::apply_downloaded_update(
                destination,
                info.install_kind,
                executable_path().parent_path(),
                executable_path());
            payload->quit = action == offline_translator::ApplyUpdateAction::quit;
            payload->text = payload->quit
                ? L"Обновление запущено. Программа будет закрыта."
                : L"Установщик запущен.";
        } catch (const std::exception& error) {
            payload->failed = true;
            payload->text =
                L"Не удалось установить обновление: " + from_utf8(error.what());
        }
        HWND target = settings && IsWindow(settings) ? settings : main_window;
        if (!target || !IsWindow(target) ||
            !PostMessageW(
                target,
                kUpdateInstallDoneMessage,
                0,
                reinterpret_cast<LPARAM>(payload))) {
            delete payload;
        }
    }).detach();
}

// Тихая проверка обновлений после запуска: уведомляем только если есть
// новая версия (при отсутствии обновлений ничего не показываем).
void start_startup_update_check(HWND window) {
    if (!g_runtime || g_update_busy) {
        return;
    }
    std::thread([window]() {
        auto* payload = new UpdateCheckPayload{};
        try {
            payload->info = offline_translator::check_for_update(
                offline_translator::kAppVersion,
                executable_path().parent_path());
        } catch (const std::exception& error) {
            payload->failed = true;
            payload->text = from_utf8(error.what());
        }
        if (!window || !IsWindow(window) ||
            !PostMessageW(
                window,
                kStartupUpdateCheckDoneMessage,
                0,
                reinterpret_cast<LPARAM>(payload))) {
            delete payload;
        }
    }).detach();
}

// Предложение обновления — своё всплывающее окно в стиле приложения
// (тема, шрифты) с кнопками «Да» и «Нет» вместо системного MessageBox.
void hide_update_popup() {
    if (g_update_popup) {
        DestroyWindow(g_update_popup);
        g_update_popup = nullptr;
        g_update_popup_text = nullptr;
        g_update_popup_yes = nullptr;
        g_update_popup_no = nullptr;
    }
    if (g_update_popup_disabled_owner && g_runtime &&
        g_runtime->main_window && IsWindow(g_runtime->main_window) &&
        !g_runtime->closing) {
        EnableWindow(g_runtime->main_window, TRUE);
    }
    g_update_popup_disabled_owner = false;
}

LRESULT CALLBACK update_button_subclass(
    HWND handle,
    UINT message,
    WPARAM w_param,
    LPARAM l_param,
    UINT_PTR,
    DWORD_PTR) {
    if (message == WM_KEYDOWN && w_param == VK_ESCAPE) {
        hide_update_popup();
        return 0;
    }
    return DefSubclassProc(handle, message, w_param, l_param);
}

void show_update_popup(HWND owner, const offline_translator::UpdateInfo& info) {
    hide_update_popup();
    constexpr int border = 1;
    constexpr int pad_x = 16;
    constexpr int pad_y = 14;
    const int width = 420;
    const int content_width = width - 2 * border - 2 * pad_x;

    std::wstring message = offline_translator::tr(L"Доступна ") +
        from_utf8(info.release.title) + L" (" +
        from_utf8(info.release.tag) + L").";
    if (info.asset) {
        message += L"\r\n" + offline_translator::tr(L"Файл: ") +
            from_utf8(info.asset->name);
    } else {
        message += L"\r\n" + offline_translator::tr(
            L"Подходящего файла установки в релизе нет — "
            L"откройте страницу релизов.");
    }
    message += L"\r\n\r\n" +
        offline_translator::tr(L"Скачать и установить сейчас?");

    // Высота текста: 3..12 строк с переносом, как у окна перевода.
    const int line_height = popup_line_height();
    const int header_height = line_height + 4;
    int visual_lines =
        (measure_text_height(message, content_width) + line_height - 1) /
        line_height;
    visual_lines = std::clamp(visual_lines, 3, 12);
    const int text_height = visual_lines * line_height + 4;
    const int button_height = 28;
    const int buttons_y =
        border + pad_y + header_height + 4 + text_height + 12;
    const int client_height = buttons_y + button_height + pad_y + border;

    int x = 0;
    int y = 0;
    RECT anchor{};
    if (owner && IsWindow(owner) && IsWindowVisible(owner) &&
        GetWindowRect(owner, &anchor)) {
        x = anchor.left + ((anchor.right - anchor.left) - width) / 2;
        y = anchor.top + ((anchor.bottom - anchor.top) - client_height) / 2;
    } else {
        RECT work{};
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
        x = work.left + ((work.right - work.left) - width) / 2;
        y = work.top + ((work.bottom - work.top) - client_height) / 2;
    }
    clamp_point_to_work_area(width, client_height, x, y);

    g_update_popup = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        L"TLingUpdatePopup",
        L"Обновление TLing",
        WS_POPUP | WS_VISIBLE,
        x,
        y,
        width,
        client_height,
        owner,
        nullptr,
        window_instance(owner),
        nullptr);
    if (!g_update_popup) {
        return;
    }

    HWND header = CreateWindowW(
        L"STATIC",
        offline_translator::tr(L"Обновление TLing").c_str(),
        WS_VISIBLE | WS_CHILD,
        border + pad_x,
        border + pad_y,
        content_width,
        header_height,
        g_update_popup,
        nullptr,
        nullptr,
        nullptr);
    SendMessageW(
        header,
        WM_SETFONT,
        reinterpret_cast<WPARAM>(popup_font(PopupFont::header)),
        TRUE);

    g_update_popup_text = CreateWindowW(
        L"STATIC",
        message.c_str(),
        WS_VISIBLE | WS_CHILD,
        border + pad_x,
        border + pad_y + header_height + 4,
        content_width,
        text_height,
        g_update_popup,
        nullptr,
        nullptr,
        nullptr);
    SendMessageW(
        g_update_popup_text,
        WM_SETFONT,
        reinterpret_cast<WPARAM>(popup_font(PopupFont::body)),
        TRUE);

    const int button_width = 96;
    const int no_x = width - border - pad_x - button_width;
    const int yes_x = no_x - button_width - 8;
    const auto create_button = [&](int button_x, const wchar_t* label, int id) {
        HWND button = CreateWindowW(
            L"BUTTON",
            offline_translator::tr(label).c_str(),
            WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
            button_x,
            buttons_y,
            button_width,
            button_height,
            g_update_popup,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
            nullptr,
            nullptr);
        SendMessageW(
            button,
            WM_SETFONT,
            reinterpret_cast<WPARAM>(popup_font(PopupFont::button)),
            TRUE);
        SetWindowSubclass(button, flat_button_subclass, 1, 0);
        SetWindowSubclass(button, update_button_subclass, 2, 0);
        return button;
    };
    g_update_popup_yes = create_button(yes_x, L"Да", kUpdateYesButton);
    g_update_popup_no = create_button(no_x, L"Нет", kUpdateNoButton);

    // Как у системного диалога: пока не дан ответ, главное окно недоступно.
    if (owner && IsWindow(owner) && IsWindowEnabled(owner)) {
        EnableWindow(owner, FALSE);
        g_update_popup_disabled_owner = true;
    }
    // Фокус на «Нет»: случайное нажатие не запускает установку.
    SetFocus(g_update_popup_no);
    offline_translator::app_log_info(
        "update popup: показано версия=" + info.release.tag);
}

LRESULT CALLBACK update_popup_proc_impl(
    HWND window,
    UINT message,
    WPARAM w_param,
    LPARAM l_param) {
    if (message == WM_COMMAND) {
        const int id = LOWORD(w_param);
        if (id == kUpdateYesButton) {
            hide_update_popup();
            set_status(L"Скачивание обновления…");
            start_update_install();
            return 0;
        }
        if (id == kUpdateNoButton) {
            offline_translator::app_log_info(
                "update popup: пользователь отложил обновление");
            hide_update_popup();
            return 0;
        }
    }
    if (message == WM_DRAWITEM) {
        auto* draw = reinterpret_cast<DRAWITEMSTRUCT*>(l_param);
        if (draw && draw->CtlType == ODT_BUTTON) {
            const UiTheme theme = current_ui_theme();
            const bool hovered =
                GetPropW(draw->hwndItem, kHoverPropertyName) != nullptr;
            HBRUSH brush = CreateSolidBrush(
                hovered ? theme.button_hover : theme.button_face);
            FillRect(draw->hDC, &draw->rcItem, brush);
            DeleteObject(brush);
            SetBkMode(draw->hDC, TRANSPARENT);
            SetTextColor(draw->hDC, theme.button_text);
            SelectObject(draw->hDC, popup_font(PopupFont::button));
            wchar_t label[64]{};
            GetWindowTextW(draw->hwndItem, label, 64);
            DrawTextW(
                draw->hDC,
                label,
                -1,
                &draw->rcItem,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            return TRUE;
        }
    }
    if (message == WM_ERASEBKGND) {
        RECT client{};
        GetClientRect(window, &client);
        HDC dc = reinterpret_cast<HDC>(w_param);
        const UiTheme theme = current_ui_theme();
        HBRUSH border_brush = CreateSolidBrush(theme.border);
        FillRect(dc, &client, border_brush);
        DeleteObject(border_brush);
        RECT inner = client;
        InflateRect(&inner, -1, -1);
        HBRUSH panel_brush = CreateSolidBrush(theme.background);
        FillRect(dc, &inner, panel_brush);
        DeleteObject(panel_brush);
        return 1;
    }
    if (message == WM_CTLCOLORSTATIC || message == WM_CTLCOLOREDIT) {
        const HDC dc = reinterpret_cast<HDC>(w_param);
        const UiTheme theme = current_ui_theme();
        SetBkColor(dc, theme.background);
        if (reinterpret_cast<HWND>(l_param) == g_update_popup_text) {
            SetTextColor(dc, theme.text);
        } else {
            SetTextColor(dc, theme.header);
        }
        return reinterpret_cast<LRESULT>(theme_background_brush());
    }
    if (message == WM_KEYDOWN && w_param == VK_ESCAPE) {
        hide_update_popup();
        return 0;
    }
    if (message == WM_CLOSE) {
        hide_update_popup();
        return 0;
    }
    if (message == WM_DESTROY) {
        if (g_update_popup == window) {
            g_update_popup = nullptr;
            g_update_popup_text = nullptr;
            g_update_popup_yes = nullptr;
            g_update_popup_no = nullptr;
        }
        return 0;
    }
    return DefWindowProcW(window, message, w_param, l_param);
}

LRESULT CALLBACK update_popup_proc(
    HWND window,
    UINT message,
    WPARAM w_param,
    LPARAM l_param) {
    try {
        return update_popup_proc_impl(window, message, w_param, l_param);
    } catch (const std::exception& error) {
        offline_translator::app_log_error(
            std::string("ошибка окна обновления: ") + error.what());
        return 0;
    }
}

LRESULT CALLBACK selection_button_proc_impl(
    HWND window,
    UINT message,
    WPARAM w_param,
    LPARAM l_param) {
    if (message == WM_LBUTTONUP) {
        if (g_runtime) {
            start_selection_translation(g_runtime->main_window);
        }
        return 0;
    }
    if (message == WM_PAINT) {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        RECT client{};
        GetClientRect(window, &client);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(20, 20, 20));
        DrawTextW(dc, L"Aa", -1, &client, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        EndPaint(window, &paint);
        return 0;
    }
    if (message == WM_DESTROY) {
        if (g_selection_button == window) {
            g_selection_button = nullptr;
        }
        return 0;
    }
    return DefWindowProcW(window, message, w_param, l_param);
}

// Исключение из оконной процедуры приводит к std::terminate, поэтому
// каждая процедура оборачивается в try/catch (как window_proc).
LRESULT CALLBACK selection_button_proc(
    HWND window,
    UINT message,
    WPARAM w_param,
    LPARAM l_param) {
    try {
        return selection_button_proc_impl(window, message, w_param, l_param);
    } catch (const std::exception& error) {
        offline_translator::app_log_error(
            std::string("ошибка кнопки выделения: ") + error.what());
        return 0;
    }
}

LRESULT CALLBACK result_popup_proc_impl(
    HWND window,
    UINT message,
    WPARAM w_param,
    LPARAM l_param) {
    const bool click_to_close = !g_runtime ||
        g_runtime->settings.result_window_mode !=
            offline_translator::kResultWindowSelectable;
    if (message == WM_COMMAND) {
        const int id = LOWORD(w_param);
        if (id == kResultCopyButton) {
            // В режиме «клик закрывает» копируем весь перевод: выделение
            // исходного русского текста ещё висит в другом окне.
            const std::wstring text = g_popup_result_edit
                ? control_text(g_popup_result_edit)
                : std::wstring{};
            std::wstring copy = text;
            if (!click_to_close && g_popup_result_edit) {
                DWORD selection_start = 0;
                DWORD selection_end = 0;
                SendMessageW(
                    g_popup_result_edit,
                    EM_GETSEL,
                    reinterpret_cast<WPARAM>(&selection_start),
                    reinterpret_cast<LPARAM>(&selection_end));
                if (selection_end > selection_start &&
                    selection_end <= text.size()) {
                    copy = text.substr(
                        selection_start,
                        selection_end - selection_start);
                }
            }
            copy_text_to_clipboard(window, copy);
            if (!g_last_clipboard_error.empty()) {
                const std::wstring message =
                    offline_translator::tr(L"Ошибка") + L": " +
                    g_last_clipboard_error;
                localized_message_box(
                    window,
                    message.c_str(),
                    L"Копировать",
                    MB_ICONWARNING | MB_OK);
            } else if (g_popup_header) {
                SetWindowTextW(
                    g_popup_header,
                    offline_translator::tr(L"Скопировано в буфер").c_str());
                InvalidateRect(g_popup_header, nullptr, TRUE);
            }
            return 0;
        }
        if (id == kResultOriginalButton) {
            const bool show_original =
                SendMessageW(
                    g_popup_original_button, BM_GETCHECK, 0, 0) ==
                BST_CHECKED;
            g_popup_showing_original = show_original;
            if (g_popup_result_edit) {
                SetWindowTextW(
                    g_popup_result_edit,
                    (show_original ? g_popup_source_text
                                   : g_popup_translation_text)
                        .c_str());
                SendMessageW(
                    g_popup_result_edit,
                    EM_SETSEL,
                    static_cast<WPARAM>(-1),
                    0);
            }
            return 0;
        }
        if (id == kResultAddButton) {
            offline_translator::app_log_info("add: клик по кнопке");
            const std::wstring text = g_popup_result_edit
                ? control_text(g_popup_result_edit)
                : std::wstring{};
            std::string separator{" / "};
            if (g_runtime) {
                separator = g_runtime->settings.append_separator;
            }
            add_source_with_translation(
                window,
                g_popup_source_text,
                text,
                from_utf8(separator));
            return 0;
        }
        if (id == kResultReplaceButton) {
            offline_translator::app_log_info("replace: клик по кнопке");
            const std::wstring text = g_popup_result_edit
                ? control_text(g_popup_result_edit)
                : std::wstring{};
            replace_source_with_translation(window, text);
            return 0;
        }
        if (id == kResultCloseButton) {
            hide_result_popup();
            return 0;
        }
    }
    if (message == WM_DRAWITEM) {
        auto* draw = reinterpret_cast<DRAWITEMSTRUCT*>(l_param);
        if (draw && draw->CtlType == ODT_BUTTON) {
            const UiTheme theme = current_ui_theme();
            const bool hovered =
                GetPropW(draw->hwndItem, kHoverPropertyName) != nullptr;
            HBRUSH brush = CreateSolidBrush(
                hovered ? theme.button_hover : theme.button_face);
            FillRect(draw->hDC, &draw->rcItem, brush);
            DeleteObject(brush);
            SetBkMode(draw->hDC, TRANSPARENT);
            SetTextColor(draw->hDC, theme.button_text);
            SelectObject(draw->hDC, popup_font(PopupFont::button));
            wchar_t label[64]{};
            GetWindowTextW(draw->hwndItem, label, 64);
            DrawTextW(
                draw->hDC,
                label,
                -1,
                &draw->rcItem,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            return TRUE;
        }
    }
    if (message == WM_ERASEBKGND) {
        RECT client{};
        GetClientRect(window, &client);
        HDC dc = reinterpret_cast<HDC>(w_param);
        const UiTheme theme = current_ui_theme();
        HBRUSH border_brush = CreateSolidBrush(theme.border);
        FillRect(dc, &client, border_brush);
        DeleteObject(border_brush);
        RECT inner = client;
        InflateRect(&inner, -1, -1);
        HBRUSH panel_brush = CreateSolidBrush(theme.background);
        FillRect(dc, &inner, panel_brush);
        DeleteObject(panel_brush);
        return 1;
    }
    if (message == WM_CTLCOLORSTATIC || message == WM_CTLCOLOREDIT) {
        const HDC dc = reinterpret_cast<HDC>(w_param);
        const UiTheme theme = current_ui_theme();
        SetBkColor(dc, theme.background);
        if (reinterpret_cast<HWND>(l_param) == g_popup_result_edit) {
            SetTextColor(dc, theme.text);
        } else {
            SetTextColor(dc, theme.header);
        }
        return reinterpret_cast<LRESULT>(theme_background_brush());
    }
    if (message == WM_KEYDOWN && w_param == VK_ESCAPE) {
        hide_result_popup();
        return 0;
    }
    if (message == WM_LBUTTONUP && click_to_close) {
        offline_translator::app_log_info("popup: WM_LBUTTONUP (закрываю)");
        hide_result_popup();
        return 0;
    }
    if (message == WM_PARENTNOTIFY && click_to_close &&
        LOWORD(w_param) == WM_LBUTTONDOWN) {
        // Для кликов мыши lParam — это КООРДИНАТЫ курсора, а не HWND
        // дочернего окна. По ним и решаем: нажатие на кнопку действия
        // (Копировать/Замена) не закрывает окно, остальное закрывает.
        POINT cursor{};
        GetCursorPos(&cursor);
        auto over_button = [&cursor](HWND button) {
            if (!button || !IsWindow(button)) {
                return false;
            }
            RECT bounds{};
            GetWindowRect(button, &bounds);
            return PtInRect(&bounds, cursor) != FALSE;
        };
        const bool on_action_button =
            over_button(g_popup_copy_button) ||
            over_button(g_popup_replace_button) ||
            over_button(g_popup_add_button);
        offline_translator::app_log_info(
            "popup: нажатие в " + std::to_string(cursor.x) + "," +
            std::to_string(cursor.y) +
            (on_action_button ? " (кнопка действия)" : " (закрываю)"));
        if (!on_action_button) {
            hide_result_popup();
        }
        return 0;
    }
    if (message == WM_DESTROY) {
        if (g_result_popup == window) {
            g_result_popup = nullptr;
            g_popup_result_edit = nullptr;
            g_popup_copy_button = nullptr;
            g_popup_replace_button = nullptr;
            g_popup_add_button = nullptr;
            g_popup_original_button = nullptr;
            g_popup_header = nullptr;
        }
        return 0;
    }
    return DefWindowProcW(window, message, w_param, l_param);
}

LRESULT CALLBACK result_popup_proc(
    HWND window,
    UINT message,
    WPARAM w_param,
    LPARAM l_param) {
    try {
        return result_popup_proc_impl(window, message, w_param, l_param);
    } catch (const std::exception& error) {
        offline_translator::app_log_error(
            std::string("ошибка окна перевода: ") + error.what());
        // Окно не уничтожаем: DestroyWindow из его же процедуры — известный
        // источник краша. Пользователь закроет его обычным щелчком.
        return 0;
    }
}

void apply_settings_dialog(HWND settings_window) {
    if (!g_runtime) {
        return;
    }
    auto settings = g_runtime->settings;
    settings.popup_modifier = popup_modifier_from_combo(g_settings_popup_modifier);
    settings.popup_requires_ctrl =
        settings.popup_modifier == offline_translator::kPopupModifierCtrl;
    const bool only_ctrl_c_c =
        SendMessageW(g_settings_only_ctrl_c_c, BM_GETCHECK, 0, 0) == BST_CHECKED;
    settings.selection_popup_enabled = !only_ctrl_c_c;
    settings.double_ctrl_c_translation =
        SendMessageW(g_settings_double_ctrl_c, BM_GETCHECK, 0, 0) == BST_CHECKED;
    if (only_ctrl_c_c) {
        settings.double_ctrl_c_translation = true;
    }
    settings.result_window_mode =
        SendMessageW(g_settings_click_to_close, BM_GETCHECK, 0, 0) == BST_CHECKED
            ? std::string{offline_translator::kResultWindowClickToClose}
            : std::string{offline_translator::kResultWindowSelectable};
    settings.auto_copy_selection =
        g_settings_auto_copy &&
        SendMessageW(g_settings_auto_copy, BM_GETCHECK, 0, 0) == BST_CHECKED;
    int history_limit = offline_translator::kDefaultClipboardHistoryLimit;
    if (g_settings_history_limit) {
        const std::wstring limit_text = control_text(g_settings_history_limit);
        try {
            history_limit = std::stoi(to_utf8(limit_text));
        } catch (const std::exception&) {
            history_limit = offline_translator::kDefaultClipboardHistoryLimit;
        }
    }
    settings.clipboard_history_limit =
        offline_translator::normalize_clipboard_history_limit(history_limit);
    if (g_settings_append_separator) {
        settings.append_separator =
            append_separator_from_combo(g_settings_append_separator);
    }
    if (g_settings_excel_enabled) {
        settings.excel_translation =
            SendMessageW(
                g_settings_excel_enabled,
                BM_GETCHECK,
                0,
                0) == BST_CHECKED;
    }
    if (g_settings_excel_trigger) {
        const LRESULT index =
            SendMessageW(g_settings_excel_trigger, CB_GETCURSEL, 0, 0);
        settings.excel_trigger = index == 0
            ? std::string{offline_translator::kExcelTriggerDoubleCtrlC}
            : std::string{offline_translator::kExcelTriggerHotkey};
    }
    if (g_settings_excel_hotkey) {
        const std::string spec =
            to_utf8(control_text(g_settings_excel_hotkey));
        if (const auto parsed = offline_translator::parse_hotkey(spec)) {
            settings.excel_hotkey =
                offline_translator::format_hotkey(*parsed);
        } else if (
            settings.excel_translation &&
            settings.excel_trigger ==
                offline_translator::kExcelTriggerHotkey) {
            localized_message_box(
                settings_window,
                L"Некорректная горячая клавиша Excel. Пример: Ctrl+Alt+T",
                L"Настройки",
                MB_ICONWARNING | MB_OK);
            return;
        }
    }
    // Жест «двойное Ctrl+C» работает через общий Ctrl+C+C.
    if (settings.excel_translation &&
        settings.excel_trigger ==
            offline_translator::kExcelTriggerDoubleCtrlC) {
        settings.double_ctrl_c_translation = true;
    }
    if (g_settings_excel_method) {
        settings.excel_write_method =
            excel_method_from_combo(g_settings_excel_method);
    }
    if (g_settings_ocr_engine) {
        settings.ocr_engine = ocr_engine_from_combo(g_settings_ocr_engine);
    }
    if (g_settings_ocr_only) {
        settings.ocr_only =
            SendMessageW(g_settings_ocr_only, BM_GETCHECK, 0, 0) ==
            BST_CHECKED;
    }
    settings.ui_theme =
        g_settings_theme_dark &&
        SendMessageW(g_settings_theme_dark, BM_GETCHECK, 0, 0) == BST_CHECKED
            ? std::string{offline_translator::kUiThemeDark}
            : std::string{offline_translator::kUiThemeLight};
    // Ключи онлайн-переводчиков: обрезаем пробелы по краям.
    if (g_settings_google_key) {
        settings.google_api_key =
            trim_spaces(to_utf8(control_text(g_settings_google_key)));
    }
    if (g_settings_check_updates_startup) {
        settings.check_updates_on_startup =
            SendMessageW(
                g_settings_check_updates_startup,
                BM_GETCHECK,
                0,
                0) == BST_CHECKED;
    }
    if (g_runtime->main_window && g_engine_combo) {
        settings.engine =
            offline_translator::settings_engine_name(selected_engine());
    }
    try {
        // selected_language() бросает исключение, если в комбобоксе нет
        // выбранного пункта, поэтому читать языки нужно внутри try.
        if (g_runtime->main_window && g_engine_combo) {
            settings.source_language = g_auto_source_language
                ? std::string(kAutoSourceLanguage)
                : selected_source_language(g_source_language_combo);
            settings.target_language =
                selected_language(g_target_language_combo);
            if (g_fallback_target_language_combo) {
                settings.fallback_target_language =
                    selected_language(g_fallback_target_language_combo);
            }
            collect_window_size(g_runtime->main_window, settings);
        }
        if (!g_smoke_mode) {
            offline_translator::save_settings(settings);
            const bool autostart =
                SendMessageW(g_settings_autostart, BM_GETCHECK, 0, 0) ==
                BST_CHECKED;
            offline_translator::set_app_autostart(autostart);
            if (g_runtime->main_window) {
                if (!settings.excel_translation ||
                    settings.excel_trigger ==
                        offline_translator::kExcelTriggerDoubleCtrlC) {
                    UnregisterHotKey(g_runtime->main_window, kExcelHotkey);
                } else if (!register_excel_hotkey(
                               g_runtime->main_window,
                               settings.excel_hotkey)) {
                    localized_message_box(
                        settings_window,
                        L"Не удалось зарегистрировать горячую клавишу Excel.",
                        L"Настройки",
                        MB_ICONWARNING | MB_OK);
                }
            }
        }
        store_runtime_settings(settings);
        apply_ocr_backend_from_settings(settings);
        refresh_tray_icon();
        apply_live_theme();
        offline_translator::app_log_info(
            "настройки сохранены engine=" + settings.engine +
            " theme=" + settings.ui_theme +
            " modifier=" + settings.popup_modifier);
        close_settings_window();
        set_status(L"Настройки сохранены");
    } catch (const std::exception& error) {
        localized_message_box(
            settings_window,
            from_utf8(error.what()).c_str(),
            L"Настройки",
            MB_ICONERROR | MB_OK);
    }
}

void close_settings_window() {
    if (!g_runtime || !g_runtime->settings_window) {
        return;
    }
    HWND settings = g_runtime->settings_window;
    HWND main = g_runtime->main_window;
    g_runtime->settings_window = nullptr;
    g_settings_scroll = 0;
    g_settings_panel = nullptr;
    g_settings_save = nullptr;
    g_settings_cancel = nullptr;
    g_settings_engine_combo = nullptr;
        g_settings_behavior_summary = nullptr;
        g_settings_google_key = nullptr;
        g_settings_check_updates_startup = nullptr;
    for (int index = 0; index < 5; ++index) {
        g_settings_nav[index] = nullptr;
    }
    g_settings_popup_ctrl = nullptr;
    g_settings_popup_modifier = nullptr;
    g_settings_only_ctrl_c_c = nullptr;
    g_settings_double_ctrl_c = nullptr;
    g_settings_click_to_close = nullptr;
    g_settings_selectable = nullptr;
    g_settings_autostart = nullptr;
    g_settings_theme_light = nullptr;
    g_settings_theme_dark = nullptr;
    g_settings_auto_copy = nullptr;
    g_settings_history_limit = nullptr;
    g_settings_append_separator = nullptr;
    g_settings_excel_enabled = nullptr;
    g_settings_excel_hotkey = nullptr;
    g_settings_excel_method = nullptr;
    g_settings_excel_trigger = nullptr;
    g_settings_ocr_engine = nullptr;
    g_settings_ocr_only = nullptr;
    g_settings_check_update = nullptr;
    g_settings_install_update = nullptr;
    g_settings_open_releases = nullptr;
    g_settings_update_status = nullptr;
    DestroyWindow(settings);
    if (main) {
        EnableWindow(main, TRUE);
        SetForegroundWindow(main);
    }
}

// Панель содержимого окна настроек: прокачивает цвета темы и
// пересылает команды в само окно настроек.
LRESULT CALLBACK settings_panel_proc_impl(
    HWND window,
    UINT message,
    WPARAM w_param,
    LPARAM l_param) {
    if (message == WM_ERASEBKGND) {
        RECT client{};
        GetClientRect(window, &client);
        FillRect(
            reinterpret_cast<HDC>(w_param),
            &client,
            theme_background_brush());
        return 1;
    }
    if (message == WM_CTLCOLORSTATIC || message == WM_CTLCOLOREDIT ||
        message == WM_CTLCOLORLISTBOX) {
        const HDC dc = reinterpret_cast<HDC>(w_param);
        const UiTheme theme = current_ui_theme();
        SetBkColor(dc, theme.background);
        SetTextColor(dc, theme.text);
        return reinterpret_cast<LRESULT>(theme_background_brush());
    }
    if (message == WM_MEASUREITEM) {
        auto* measure = reinterpret_cast<MEASUREITEMSTRUCT*>(l_param);
        if (measure && measure->CtlType == ODT_COMBOBOX) {
            measure_owner_combo(measure);
            return TRUE;
        }
    }
    if (message == WM_DRAWITEM) {
        auto* draw = reinterpret_cast<DRAWITEMSTRUCT*>(l_param);
        if (draw && draw->CtlType == ODT_COMBOBOX) {
            draw_owner_combo(*draw);
            return TRUE;
        }
        if (draw && draw->CtlType == ODT_BUTTON) {
            draw_flat_button(*draw);
            return TRUE;
        }
    }
    if (message == WM_COMMAND) {
        const HWND settings = GetParent(window);
        if (settings) {
            SendMessageW(settings, message, w_param, l_param);
        }
        return 0;
    }
    return DefWindowProcW(window, message, w_param, l_param);
}

LRESULT CALLBACK settings_panel_proc(
    HWND window,
    UINT message,
    WPARAM w_param,
    LPARAM l_param) {
    try {
        return settings_panel_proc_impl(window, message, w_param, l_param);
    } catch (const std::exception& error) {
        offline_translator::app_log_error(
            std::string("ошибка панели настроек: ") + error.what());
        return 0;
    }
}

LRESULT CALLBACK settings_proc_impl(
    HWND window,
    UINT message,
    WPARAM w_param,
    LPARAM l_param) {
    if (message == WM_CREATE) {
        const auto settings = g_runtime ? g_runtime->settings
                                        : offline_translator::AppSettings{};
        // Панель содержимого: все контентные контролы — её дети.
        g_settings_panel = CreateWindowW(
            L"TLingSettingsPanel",
            nullptr,
            WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
            kSettingsSidebarWidth + 6,
            0,
            10,
            10,
            window,
            nullptr,
            nullptr,
            nullptr);
        auto content = [&](int page, const wchar_t* cls, const wchar_t* text,
                           DWORD style, int x, int y, int w, int h,
                           int id = 0) -> HWND {
            HWND control = CreateWindowW(
                cls,
                text,
                WS_CHILD | WS_VISIBLE | style,
                x,
                y,
                w,
                h,
                g_settings_panel,
                id ? reinterpret_cast<HMENU>(static_cast<INT_PTR>(id))
                   : nullptr,
                nullptr,
                nullptr);
            if (control) {
                SetPropW(
                    control,
                    kSettingsPageProperty,
                    reinterpret_cast<HANDLE>(static_cast<INT_PTR>(page)));
            }
            return control;
        };
        // ===== Сайдбар =====
        const wchar_t* nav_titles[5] = {
            L"Языки",
            L"Поведение",
            L"Темы",
            L"Язык интерфейса",
            L"О программе"};
        for (int index = 0; index < 5; ++index) {
            g_settings_nav[index] = CreateWindowW(
                L"BUTTON",
                nav_titles[index],
                WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                0,
                kSettingsNavTop + index * (kSettingsNavHeight + kSettingsNavGap),
                kSettingsSidebarWidth,
                kSettingsNavHeight,
                window,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(
                    kSettingsNavLanguages + index)),
                nullptr,
                nullptr);
            SetWindowSubclass(
                g_settings_nav[index],
                flat_button_subclass,
                1,
                0);
        }
        // ===== Страница «Языки» =====
        HWND languages_title = content(
            kSettingsPageLanguages,
            L"STATIC",
            L"Движок перевода",
            0,
            16,
            16,
            300,
            24);
        SendMessageW(
            languages_title,
            WM_SETFONT,
            reinterpret_cast<WPARAM>(settings_section_font()),
            TRUE);
        g_settings_engine_combo = content(
            kSettingsPageLanguages,
            L"COMBOBOX",
            nullptr,
            CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_VSCROLL | WS_TABSTOP,
            16,
            46,
            260,
            200,
            kSettingsEngineCombo);
        SendMessageW(
            g_settings_engine_combo,
            CB_ADDSTRING,
            0,
            reinterpret_cast<LPARAM>(L"Argos"));
        SendMessageW(
            g_settings_engine_combo,
            CB_ADDSTRING,
            0,
            reinterpret_cast<LPARAM>(L"NLLB-200"));
        SendMessageW(
            g_settings_engine_combo,
            CB_ADDSTRING,
            0,
            reinterpret_cast<LPARAM>(L"Firefox"));
    SendMessageW(
        g_settings_engine_combo,
        CB_ADDSTRING,
        0,
        reinterpret_cast<LPARAM>(L"MarianMT"));
    SendMessageW(
        g_settings_engine_combo,
        CB_ADDSTRING,
        0,
        reinterpret_cast<LPARAM>(L"Google (онлайн)"));
    SendMessageW(
        g_settings_engine_combo,
        CB_SETCURSEL,
            engine_combo_index(
                offline_translator::engine_kind_from_settings(
                    settings.engine)),
            0);
        content(
            kSettingsPageLanguages,
            L"STATIC",
            L"Установка и удаление языковых моделей — в окне «Пакеты».",
            0,
            16,
            84,
            340,
            36);
        content(
            kSettingsPageLanguages,
            L"BUTTON",
            L"Пакеты моделей…",
            BS_OWNERDRAW | WS_TABSTOP,
            16,
            128,
            180,
            28,
            kSettingsPackagesButton);
        // Онлайн-перевод: движок Google работает через интернет, поэтому
        // ключ API (необязательный) указывается здесь.
        HWND online_title = content(
            kSettingsPageLanguages,
            L"STATIC",
            L"Онлайн-перевод (Google)",
            0,
            16,
            190,
            340,
            24);
        SendMessageW(
            online_title,
            WM_SETFONT,
            reinterpret_cast<WPARAM>(settings_section_font()),
            TRUE);
        content(
            kSettingsPageLanguages,
            L"STATIC",
            L"Ключ Google (необязательно, для надёжности):",
            0,
            16,
            220,
            340,
            18);
        g_settings_google_key = content(
            kSettingsPageLanguages,
            L"EDIT",
            from_utf8(settings.google_api_key).c_str(),
            WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP,
            16,
            242,
            460,
            24,
            kSettingsGoogleKey);
        content(
            kSettingsPageLanguages,
            L"STATIC",
            L"Ключ хранится в settings.json и передаётся только сервису "
            L"Google. Без ключа перевод идёт через бесплатные эндпоинты, "
            L"но сервис может ограничивать автоматические запросы.",
            0,
            16,
            276,
            460,
            54);
        // ===== Страница «Поведение» =====
        // Сводка выбранных опций поведения (как в ранних версиях):
        // рамка «Сейчас работает» с буллетами, обновляется при изменении
        // любого переключателя на странице.
        HWND summary_title = content(
            kSettingsPageBehavior,
            L"STATIC",
            L"Сейчас работает",
            0,
            16,
            16,
            340,
            24);
        SendMessageW(
            summary_title,
            WM_SETFONT,
            reinterpret_cast<WPARAM>(settings_section_font()),
            TRUE);
        g_settings_behavior_summary = content(
            kSettingsPageBehavior,
            L"STATIC",
            L"",
            0,
            36,
            44,
            440,
            110);
        HWND behavior_title = content(
            kSettingsPageBehavior,
            L"STATIC",
            L"Перевод выделенного текста",
            0,
            16,
            162,
            340,
            24);
        SendMessageW(
            behavior_title,
            WM_SETFONT,
            reinterpret_cast<WPARAM>(settings_section_font()),
            TRUE);
        content(
            kSettingsPageBehavior,
            L"STATIC",
            L"Клавиша удержания для кнопки перевода:",
            0,
            16,
            194,
            300,
            18);
        g_settings_popup_modifier = content(
            kSettingsPageBehavior,
            L"COMBOBOX",
            nullptr,
            CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_VSCROLL | WS_TABSTOP,
            16,
            216,
            240,
            160,
            kSettingsPopupModifier);
        fill_popup_modifier_combo(
            g_settings_popup_modifier,
            settings.popup_modifier);
        g_settings_double_ctrl_c = content(
            kSettingsPageBehavior,
            L"BUTTON",
            L"Переводить выделенный текст по Ctrl+C+C",
            BS_AUTOCHECKBOX | WS_TABSTOP,
            16,
            248,
            470,
            24,
            kSettingsDoubleCtrlC);
        SendMessageW(
            g_settings_double_ctrl_c,
            BM_SETCHECK,
            settings.double_ctrl_c_translation ? BST_CHECKED : BST_UNCHECKED,
            0);
        g_settings_only_ctrl_c_c = content(
            kSettingsPageBehavior,
            L"BUTTON",
            L"Только Ctrl+C+C (не показывать кнопку при выделении)",
            BS_AUTOCHECKBOX | WS_TABSTOP,
            16,
            276,
            470,
            24,
            kSettingsOnlyCtrlCC);
        SendMessageW(
            g_settings_only_ctrl_c_c,
            BM_SETCHECK,
            settings.selection_popup_enabled ? BST_UNCHECKED : BST_CHECKED,
            0);
        content(
            kSettingsPageBehavior,
            L"STATIC",
            L"Ctrl+C+C: удерживайте Ctrl и дважды нажмите C. Перевод появится рядом с курсором. Режим «только Ctrl+C+C» не перехватывает обычные Ctrl+C/Ctrl+V.",
            0,
            36,
            304,
            440,
            54);
        content(
            kSettingsPageBehavior,
            L"STATIC",
            L"Окно результата:",
            0,
            16,
            364,
            300,
            18);
        g_settings_click_to_close = content(
            kSettingsPageBehavior,
            L"BUTTON",
            L"Закрывать нажатием по окну",
            BS_AUTORADIOBUTTON | WS_GROUP | WS_TABSTOP,
            16,
            386,
            300,
            22,
            kSettingsClickToClose);
        g_settings_selectable = content(
            kSettingsPageBehavior,
            L"BUTTON",
            L"Выделять часть текста; закрывать кнопкой «Закрыть»",
            BS_AUTORADIOBUTTON | WS_TABSTOP,
            16,
            410,
            420,
            22,
            kSettingsSelectable);
        const bool click_to_close = settings.result_window_mode !=
            offline_translator::kResultWindowSelectable;
        SendMessageW(
            g_settings_click_to_close,
            BM_SETCHECK,
            click_to_close ? BST_CHECKED : BST_UNCHECKED,
            0);
        SendMessageW(
            g_settings_selectable,
            BM_SETCHECK,
            click_to_close ? BST_UNCHECKED : BST_CHECKED,
            0);
        g_settings_auto_copy = content(
            kSettingsPageBehavior,
            L"BUTTON",
            L"Автоматически копировать выделенный текст в буфер обмена",
            BS_AUTOCHECKBOX | WS_TABSTOP,
            16,
            440,
            470,
            24,
            kSettingsAutoCopy);
        SendMessageW(
            g_settings_auto_copy,
            BM_SETCHECK,
            settings.auto_copy_selection ? BST_CHECKED : BST_UNCHECKED,
            0);
        content(
            kSettingsPageBehavior,
            L"STATIC",
            L"После выделения текст останется в буфере: его не нужно копировать вручную. Без этой настройки захват для перевода буфер не меняет.",
            0,
            36,
            468,
            440,
            54);
        content(
            kSettingsPageBehavior,
            L"STATIC",
            L"Символ добавления к оригиналу:",
            0,
            16,
            528,
            280,
            20);
        g_settings_append_separator = content(
            kSettingsPageBehavior,
            L"COMBOBOX",
            nullptr,
            CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_VSCROLL | WS_TABSTOP,
            16,
            550,
            120,
            120,
            kSettingsAppendSeparator);
        fill_append_separator_combo(
            g_settings_append_separator,
            settings.append_separator);
        content(
            kSettingsPageBehavior,
            L"STATIC",
            L"Хранить последние копирования (1–50):",
            0,
            16,
            592,
            280,
            20);
        const int history_limit =
            offline_translator::normalize_clipboard_history_limit(
                settings.clipboard_history_limit);
        g_settings_history_limit = content(
            kSettingsPageBehavior,
            L"EDIT",
            std::to_wstring(history_limit).c_str(),
            WS_BORDER | ES_NUMBER | ES_AUTOHSCROLL | WS_TABSTOP,
            300,
            588,
            48,
            24,
            kSettingsHistoryLimit);
        HWND launch_title = content(
            kSettingsPageBehavior,
            L"STATIC",
            L"Запуск",
            0,
            16,
            636,
            200,
            26);
        SendMessageW(
            launch_title,
            WM_SETFONT,
            reinterpret_cast<WPARAM>(settings_section_font()),
            TRUE);
        g_settings_autostart = content(
            kSettingsPageBehavior,
            L"BUTTON",
            L"Запускать вместе с Windows",
            BS_AUTOCHECKBOX | WS_TABSTOP,
            16,
            666,
            400,
            24,
            kSettingsAutostart);
        bool autostart = false;
        try {
            autostart = offline_translator::is_app_autostart_enabled();
        } catch (const std::exception&) {
        }
        SendMessageW(
            g_settings_autostart,
            BM_SETCHECK,
            autostart ? BST_CHECKED : BST_UNCHECKED,
            0);
        // ===== Excel: перевод выделенных ячеек =====
        HWND excel_title = content(
            kSettingsPageBehavior,
            L"STATIC",
            L"Excel: перевод выделенных ячеек",
            0,
            16,
            706,
            470,
            26);
        SendMessageW(
            excel_title,
            WM_SETFONT,
            reinterpret_cast<WPARAM>(settings_section_font()),
            TRUE);
        g_settings_excel_enabled = content(
            kSettingsPageBehavior,
            L"BUTTON",
            L"Переводить выделенные ячейки Excel",
            BS_AUTOCHECKBOX | WS_TABSTOP,
            16,
            736,
            470,
            24,
            kSettingsExcelEnabled);
        SendMessageW(
            g_settings_excel_enabled,
            BM_SETCHECK,
            settings.excel_translation ? BST_CHECKED : BST_UNCHECKED,
            0);
        content(
            kSettingsPageBehavior,
            L"STATIC",
            L"Вызов Excel-перевода:",
            0,
            16,
            764,
            300,
            18);
        g_settings_excel_trigger = content(
            kSettingsPageBehavior,
            L"COMBOBOX",
            nullptr,
            CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_VSCROLL | WS_TABSTOP,
            16,
            786,
            260,
            160,
            kSettingsExcelTrigger);
        fill_excel_trigger_combo(
            g_settings_excel_trigger,
            settings.excel_trigger,
            settings.excel_hotkey);
        content(
            kSettingsPageBehavior,
            L"STATIC",
            L"Горячая клавиша Excel:",
            0,
            16,
            814,
            300,
            18);
        g_settings_excel_hotkey = content(
            kSettingsPageBehavior,
            L"EDIT",
            from_utf8(settings.excel_hotkey).c_str(),
            WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP,
            16,
            836,
            240,
            24,
            kSettingsExcelHotkey);
        SetWindowSubclass(
            g_settings_excel_hotkey,
            hotkey_capture_subclass,
            1,
            0);
        content(
            kSettingsPageBehavior,
            L"STATIC",
            L"Нажмите комбинацию в поле «Горячая клавиша Excel», чтобы задать свою.",
            0,
            264,
            838,
            222,
            36);
        update_excel_hotkey_field_state();
        content(
            kSettingsPageBehavior,
            L"STATIC",
            L"Способ записи в Excel:",
            0,
            16,
            876,
            300,
            18);
        g_settings_excel_method = content(
            kSettingsPageBehavior,
            L"COMBOBOX",
            nullptr,
            CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_VSCROLL | WS_TABSTOP,
            16,
            898,
            260,
            160,
            kSettingsExcelMethod);
        fill_excel_method_combo(
            g_settings_excel_method,
            settings.excel_write_method);
        content(
            kSettingsPageBehavior,
            L"STATIC",
            L"Перевод добавляется к каждой выделенной ячейке через разделитель «Добавить к оригиналу». «Буфер обмена»: Excel должен быть в фокусе. «COM»: запись напрямую в открытый Excel, формулы не трогаются.",
            0,
            16,
            930,
            470,
            54);
        content(
            kSettingsPageBehavior,
            L"STATIC",
            L"Движок распознавания текста (OCR):",
            0,
            16,
            996,
            300,
            18);
        g_settings_ocr_engine = content(
            kSettingsPageBehavior,
            L"COMBOBOX",
            nullptr,
            CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_VSCROLL | WS_TABSTOP,
            16,
            1018,
            260,
            160,
            kSettingsOcrEngine);
        fill_ocr_engine_combo(g_settings_ocr_engine, settings.ocr_engine);
        content(
            kSettingsPageBehavior,
            L"STATIC",
            L"«Автоматически»: PaddleOCR (точнее на фото и сканах), иначе Tesseract. PaddleOCR работает при наличии моделей PP-OCR.",
            0,
            16,
            1050,
            470,
            54);
        g_settings_ocr_only = content(
            kSettingsPageBehavior,
            L"BUTTON",
            L"Только OCR всегда (распознавать область экрана без перевода)",
            BS_AUTOCHECKBOX | WS_TABSTOP,
            16,
            1108,
            470,
            24,
            kSettingsOcrOnly);
        SendMessageW(
            g_settings_ocr_only,
            BM_SETCHECK,
            settings.ocr_only ? BST_CHECKED : BST_UNCHECKED,
            0);
        // ===== Страница «Темы» =====
        HWND themes_title = content(
            kSettingsPageThemes,
            L"STATIC",
            L"Тема оформления",
            0,
            16,
            16,
            300,
            26);
        SendMessageW(
            themes_title,
            WM_SETFONT,
            reinterpret_cast<WPARAM>(settings_section_font()),
            TRUE);
        g_settings_theme_light = content(
            kSettingsPageThemes,
            L"BUTTON",
            L"Светлая",
            BS_AUTORADIOBUTTON | WS_GROUP | WS_TABSTOP,
            16,
            50,
            160,
            22,
            kSettingsThemeLight);
        g_settings_theme_dark = content(
            kSettingsPageThemes,
            L"BUTTON",
            L"Тёмная",
            BS_AUTORADIOBUTTON | WS_TABSTOP,
            16,
            76,
            160,
            22,
            kSettingsThemeDark);
        const bool dark_theme =
            offline_translator::normalize_ui_theme(settings.ui_theme) ==
            offline_translator::kUiThemeDark;
        SendMessageW(
            g_settings_theme_light,
            BM_SETCHECK,
            dark_theme ? BST_UNCHECKED : BST_CHECKED,
            0);
        SendMessageW(
            g_settings_theme_dark,
            BM_SETCHECK,
            dark_theme ? BST_CHECKED : BST_UNCHECKED,
            0);
        content(
            kSettingsPageThemes,
            L"STATIC",
            L"Тема применяется к главному окну, настройкам и кнопке у курсора.",
            0,
            16,
            108,
            440,
            36);
        HWND language_title = content(
            kSettingsPageLanguage,
            L"STATIC",
            L"Язык интерфейса",
            0,
            16,
            16,
            300,
            24);
        SendMessageW(
            language_title,
            WM_SETFONT,
            reinterpret_cast<WPARAM>(settings_section_font()),
            TRUE);
        g_settings_ui_language = content(
            kSettingsPageLanguage,
            L"COMBOBOX",
            nullptr,
            CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_VSCROLL | WS_TABSTOP,
            16,
            52,
            240,
            220,
            kSettingsUiLanguage);
        content(
            kSettingsPageLanguage,
            L"STATIC",
            L"Язык определяется по языку системы. Изменение применяется сразу.",
            0,
            16,
            96,
            440,
            36);
        SendMessageW(
            g_settings_ui_language,
            CB_ADDSTRING,
            0,
            reinterpret_cast<LPARAM>(L"Авто (система)"));
        {
            int selected_language = 0;
            int option_index = 1;
            for (const auto& option :
                 offline_translator::ui_language_options()) {
                SendMessageW(
                    g_settings_ui_language,
                    CB_ADDSTRING,
                    0,
                    reinterpret_cast<LPARAM>(option.label));
                if (settings.ui_language != "auto" &&
                    settings.ui_language == option.code) {
                    selected_language = option_index;
                }
                ++option_index;
            }
            SendMessageW(
                g_settings_ui_language,
                CB_SETCURSEL,
                selected_language,
                0);
        }
        // ===== Страница «О программе» =====
        HWND about_title = content(
            kSettingsPageAbout,
            L"STATIC",
            L"О программе",
            0,
            16,
            16,
            300,
            26);
        SendMessageW(
            about_title,
            WM_SETFONT,
            reinterpret_cast<WPARAM>(settings_section_font()),
            TRUE);
        const std::wstring about_version =
            L"Версия: " +
            from_utf8(std::string(offline_translator::kAppVersionDisplay));
        const std::wstring about_author =
            L"Автор: " +
            from_utf8(std::string(offline_translator::kAppPublisher));
        content(
            kSettingsPageAbout,
            L"STATIC",
            about_version.c_str(),
            0,
            16,
            50,
            470,
            20);
        content(
            kSettingsPageAbout,
            L"STATIC",
            about_author.c_str(),
            0,
            16,
            74,
            470,
            20);
        HWND updates_title = content(
            kSettingsPageAbout,
            L"STATIC",
            L"Обновления",
            0,
            16,
            108,
            300,
            24);
        SendMessageW(
            updates_title,
            WM_SETFONT,
            reinterpret_cast<WPARAM>(settings_section_font()),
            TRUE);
        g_settings_update_status = content(
            kSettingsPageAbout,
            L"EDIT",
            L"Проверка не запускалась. Обновления берутся с GitHub.",
            ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
            16,
            138,
            470,
            56,
            0);
        g_settings_check_update = content(
            kSettingsPageAbout,
            L"BUTTON",
            L"Проверить обновления",
            BS_OWNERDRAW | WS_TABSTOP,
            16,
            202,
            180,
            28,
            kSettingsCheckUpdate);
        g_settings_install_update = content(
            kSettingsPageAbout,
            L"BUTTON",
            L"Скачать и установить",
            BS_OWNERDRAW | WS_DISABLED | WS_TABSTOP,
            204,
            202,
            170,
            28,
            kSettingsInstallUpdate);
        g_settings_open_releases = content(
            kSettingsPageAbout,
            L"BUTTON",
            L"Страница релизов",
            BS_OWNERDRAW | WS_TABSTOP,
            16,
            238,
            160,
            28,
            kSettingsOpenReleases);
        g_settings_check_updates_startup = content(
            kSettingsPageAbout,
            L"BUTTON",
            L"Проверять обновления при запуске",
            BS_AUTOCHECKBOX | WS_TABSTOP,
            16,
            274,
            320,
            24,
            kSettingsCheckUpdatesStartup);
        if (settings.check_updates_on_startup) {
            SendMessageW(
                g_settings_check_updates_startup,
                BM_SETCHECK,
                BST_CHECKED,
                0);
        }
        // ===== Кнопки фиксированной нижней панели =====
        g_settings_save = CreateWindowW(
            L"BUTTON",
            L"Сохранить",
            WS_VISIBLE | WS_CHILD | BS_OWNERDRAW | WS_TABSTOP,
            248,
            660,
            118,
            30,
            window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSettingsSave)),
            nullptr,
            nullptr);
        g_settings_cancel = CreateWindowW(
            L"BUTTON",
            L"Отмена",
            WS_VISIBLE | WS_CHILD | BS_OWNERDRAW | WS_TABSTOP,
            372,
            660,
            118,
            30,
            window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSettingsCancel)),
            nullptr,
            nullptr);
        apply_theme_to_window(window);
        g_settings_scroll = 0;
        g_settings_page = kSettingsPageBehavior;
        settings_show_page(window, kSettingsPageBehavior);
        localize_window(window);
        update_behavior_summary();
        return 0;
    }
    if (message == WM_VSCROLL) {
        SCROLLINFO info{sizeof(SCROLLINFO)};
        info.fMask = SIF_ALL;
        GetScrollInfo(window, SB_VERT, &info);
        int pos = info.nTrackPos ? info.nTrackPos : info.nPos;
        switch (LOWORD(w_param)) {
            case SB_TOP:
                pos = info.nMin;
                break;
            case SB_BOTTOM:
                pos = info.nMax;
                break;
            case SB_LINEUP:
                pos = info.nPos - kSettingsScrollLine;
                break;
            case SB_LINEDOWN:
                pos = info.nPos + kSettingsScrollLine;
                break;
            case SB_PAGEUP:
                pos = info.nPos - static_cast<int>(info.nPage) -
                    kSettingsScrollLine;
                break;
            case SB_PAGEDOWN:
                pos = info.nPos + static_cast<int>(info.nPage) +
                    kSettingsScrollLine;
                break;
            case SB_THUMBPOSITION:
            case SB_THUMBTRACK:
                pos = info.nTrackPos;
                break;
            default:
                return 0;
        }
        settings_apply_scroll(window, pos);
        return 0;
    }
    if (message == WM_MOUSEWHEEL) {
        const int wheel = GET_WHEEL_DELTA_WPARAM(w_param);
        if (wheel == 0) {
            return 0;
        }
        SCROLLINFO info{sizeof(SCROLLINFO)};
        info.fMask = SIF_POS | SIF_PAGE;
        GetScrollInfo(window, SB_VERT, &info);
        const int step = static_cast<int>(info.nPage) >= 1
            ? kSettingsScrollPageLines * kSettingsScrollLine
            : kSettingsScrollLine;
        const int pos = info.nPos - (wheel / WHEEL_DELTA) * step;
        settings_apply_scroll(window, pos);
        return 0;
    }
    if (message == WM_SIZE) {
        settings_relayout(window);
        return 0;
    }
    if (message == WM_ERASEBKGND) {
        RECT client{};
        GetClientRect(window, &client);
        HDC dc = reinterpret_cast<HDC>(w_param);
        FillRect(dc, &client, theme_background_brush());
        // Сайдбар слева с оттенком, отличным от контента.
        const UiTheme theme = current_ui_theme();
        const bool dark = is_dark_theme();
        const COLORREF sidebar = dark ? RGB(40, 40, 46) : RGB(233, 233, 233);
        RECT side{0, 0, kSettingsSidebarWidth, client.bottom};
        HBRUSH side_brush = CreateSolidBrush(sidebar);
        FillRect(dc, &side, side_brush);
        DeleteObject(side_brush);
        RECT line{kSettingsSidebarWidth, 0, kSettingsSidebarWidth + 1, client.bottom};
        HBRUSH line_brush = CreateSolidBrush(theme.border);
        FillRect(dc, &line, line_brush);
        DeleteObject(line_brush);
        return 1;
    }
    if (message == WM_CTLCOLORSTATIC || message == WM_CTLCOLOREDIT) {
        const HDC dc = reinterpret_cast<HDC>(w_param);
        const UiTheme theme = current_ui_theme();
        SetBkColor(dc, theme.background);
        SetTextColor(dc, theme.text);
        return reinterpret_cast<LRESULT>(theme_background_brush());
    }
    if (message == WM_DRAWITEM) {
        auto* draw = reinterpret_cast<DRAWITEMSTRUCT*>(l_param);
        if (draw && draw->CtlType == ODT_BUTTON) {
            int index = -1;
            for (int item = 0; item < 5; ++item) {
                if (draw->hwndItem == g_settings_nav[item]) {
                    index = item;
                    break;
                }
            }
            if (index < 0) {
                draw_flat_button(*draw);
                return TRUE;
            }
            const bool dark = is_dark_theme();
            const COLORREF sidebar = dark ? RGB(40, 40, 46) : RGB(233, 233, 233);
            const bool hovered =
                GetPropW(draw->hwndItem, kHoverPropertyName) != nullptr;
            const bool active = g_settings_page == index;
            COLORREF face = sidebar;
            if (active) {
                face = current_ui_theme().button_face;
            } else if (hovered) {
                face = current_ui_theme().button_hover;
            }
            HBRUSH brush = CreateSolidBrush(face);
            FillRect(draw->hDC, &draw->rcItem, brush);
            DeleteObject(brush);
            SetBkMode(draw->hDC, TRANSPARENT);
            SetTextColor(draw->hDC, current_ui_theme().text);
            SelectObject(draw->hDC, settings_nav_font(active));
            RECT text_rect = draw->rcItem;
            text_rect.left += 14;
            wchar_t label[64]{};
            GetWindowTextW(draw->hwndItem, label, 64);
            DrawTextW(
                draw->hDC,
                label,
                -1,
                &text_rect,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            return TRUE;
        }
    }
    if (message == WM_COMMAND) {
        const int id = LOWORD(w_param);
        const int notification = HIWORD(w_param);
        if (id >= kSettingsNavLanguages && id <= kSettingsNavAbout) {
            settings_show_page(window, id - kSettingsNavLanguages);
            return 0;
        }
        if (id == kSettingsPackagesButton) {
            open_packages_window(window, window_instance(window));
            return 0;
        }
        if (id == kSettingsEngineCombo && notification == CBN_SELCHANGE) {            const LRESULT engine = SendMessageW(
                g_settings_engine_combo, CB_GETCURSEL, 0, 0);
            const wchar_t* names[4] = {
                L"argos", L"nllb", L"firefox", L"marian"};
            if (engine >= 0 && engine < 4 && g_runtime) {
                const std::string engine_name = to_utf8(names[engine]);
                update_runtime_settings(
                    [&engine_name](offline_translator::AppSettings& current) {
                        current.engine = engine_name;
                    });
                try {
                    if (!g_smoke_mode) {
                        offline_translator::save_settings(g_runtime->settings);
                    }
                } catch (const std::exception&) {
                }
                if (g_engine_combo) {
                    SendMessageW(g_engine_combo, CB_SETCURSEL, engine, 0);
                }
            }
            return 0;
        }
        if (id == kSettingsUiLanguage && notification == CBN_SELCHANGE &&
            g_runtime) {
            const LRESULT selected = SendMessageW(
                g_settings_ui_language, CB_GETCURSEL, 0, 0);
            std::string code = "auto";
            const auto& options = offline_translator::ui_language_options();
            if (selected > 0 &&
                static_cast<std::size_t>(selected - 1) < options.size()) {
                code = options[static_cast<std::size_t>(selected - 1)].code;
            }
            update_runtime_settings(
                    [&code](offline_translator::AppSettings& current) {
                        current.ui_language = code;
                    });
            offline_translator::set_ui_language(code);
            try {
                if (!g_smoke_mode) {
                    offline_translator::save_settings(g_runtime->settings);
                }
            } catch (const std::exception&) {
            }
            // Меняем язык открытых окон, трея и списка пакетов.
            if (g_package_caption) {
                refresh_package_list();
            }
            relocalize_all();
            if (g_runtime->main_window) {
                RedrawWindow(
                    g_runtime->main_window,
                    nullptr,
                    nullptr,
                    RDW_ERASE | RDW_INVALIDATE | RDW_ALLCHILDREN);
            }
            return 0;
        }
        if (id == kSettingsOnlyCtrlCC && notification == BN_CLICKED) {
            if (SendMessageW(g_settings_only_ctrl_c_c, BM_GETCHECK, 0, 0) ==
                BST_CHECKED) {
                SendMessageW(
                    g_settings_double_ctrl_c,
                    BM_SETCHECK,
                    BST_CHECKED,
                    0);
            }
            update_behavior_summary();
            return 0;
        }
        if (notification == BN_CLICKED &&
            (id == kSettingsDoubleCtrlC || id == kSettingsAutoCopy ||
             id == kSettingsAutostart || id == kSettingsClickToClose ||
             id == kSettingsSelectable)) {
            update_behavior_summary();
            return 0;
        }
        if (id == kSettingsPopupModifier && notification == CBN_SELCHANGE) {
            update_behavior_summary();
            return 0;
        }
        if (id == kSettingsExcelTrigger && notification == CBN_SELCHANGE) {
            apply_excel_trigger_selection();
            return 0;
        }
        if (id == kSettingsSave) {
            apply_settings_dialog(window);
            return 0;
        }
        if (id == kSettingsCancel) {
            close_settings_window();
            return 0;
        }
        if (id == kSettingsCheckUpdate) {
            start_update_check();
            return 0;
        }
        if (id == kSettingsInstallUpdate) {
            start_update_install();
            return 0;
        }
        if (id == kSettingsOpenReleases) {
            open_releases_page();
            return 0;
        }
    }
    if (message == kUpdateCheckDoneMessage) {
        std::unique_ptr<UpdateCheckPayload> payload(
            reinterpret_cast<UpdateCheckPayload*>(l_param));
        g_pending_update = payload->info;
        set_update_status(payload->text);
        set_update_controls_busy(false);
        return 0;
    }
    if (message == kUpdateInstallDoneMessage) {
        std::unique_ptr<UpdateInstallPayload> payload(
            reinterpret_cast<UpdateInstallPayload*>(l_param));
        set_update_status(payload->text);
        set_update_controls_busy(false);
        if (!payload->failed && payload->quit && g_runtime && g_runtime->main_window) {
            auto* forwarded = new UpdateInstallPayload(*payload);
            if (!PostMessageW(
                    g_runtime->main_window,
                    kUpdateInstallDoneMessage,
                    0,
                    reinterpret_cast<LPARAM>(forwarded))) {
                delete forwarded;
            }
        }
        return 0;
    }
    if (message == WM_CLOSE) {
        close_settings_window();
        return 0;
    }
    if (message == WM_DESTROY) {
        if (g_runtime && g_runtime->settings_window == window) {
            g_runtime->settings_window = nullptr;
        }
        g_settings_panel = nullptr;
        g_settings_save = nullptr;
        g_settings_cancel = nullptr;
        g_settings_engine_combo = nullptr;
    g_settings_behavior_summary = nullptr;
        g_settings_google_key = nullptr;
        for (int index = 0; index < 5; ++index) {
            g_settings_nav[index] = nullptr;
        }
        return 0;
    }
    return DefWindowProcW(window, message, w_param, l_param);
}

LRESULT CALLBACK settings_proc(
    HWND window,
    UINT message,
    WPARAM w_param,
    LPARAM l_param) {
    try {
        return settings_proc_impl(window, message, w_param, l_param);
    } catch (const std::exception& error) {
        offline_translator::app_log_error(
            std::string("ошибка окна настроек: ") + error.what());
        localized_message_box(
            window,
            from_utf8(error.what()).c_str(),
            L"Ошибка",
            MB_ICONERROR | MB_OK);
        return 0;
    }
}

void open_settings_window(HWND parent, HINSTANCE instance, bool check_updates) {
    if (g_runtime && g_runtime->settings_window) {
        SetForegroundWindow(g_runtime->settings_window);
        if (check_updates) {
            start_update_check();
        }
        return;
    }
    // Высота не больше рабочей области: на небольших экранах окно
    // ужимается, а недостающее добирает скроллбар.
    RECT work_area{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work_area, 0);
    const int window_height = std::min(800, static_cast<int>(work_area.bottom - work_area.top));
    // Центрируем в рабочей области: CW_USEDEFAULT может утопить низ
    // окна под панель задач, и кнопки низа становятся недоступны.
    int window_x = work_area.left +
        ((work_area.right - work_area.left) - 900) / 2;
    if (window_x < work_area.left) {
        window_x = work_area.left;
    }
    int window_y = work_area.top +
        (static_cast<int>(work_area.bottom - work_area.top) - window_height) / 2;
    HWND settings = CreateWindowW(
        L"TLingSettings",
        L"Настройки",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_VSCROLL,
        window_x,
        window_y,
        900,
        window_height,
        parent,
        nullptr,
        instance,
        nullptr);
    if (!settings) {
        throw std::runtime_error("Не удалось открыть настройки");
    }
    if (g_runtime) {
        g_runtime->settings_window = settings;
    }
    EnableWindow(parent, FALSE);
    ShowWindow(settings, SW_SHOW);
    UpdateWindow(settings);
    if (check_updates) {
        start_update_check();
    }
}

void create_main_controls(HWND window) {
    CreateWindowW(
        L"STATIC",
        L"Движок:",
        WS_VISIBLE | WS_CHILD,
        16,
        16,
        70,
        24,
        window,
        nullptr,
        nullptr,
        nullptr);
    g_engine_combo = CreateWindowW(
        L"COMBOBOX",
        nullptr,
        WS_VISIBLE | WS_CHILD | CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_VSCROLL | WS_TABSTOP,
        90,
        12,
        155,
        200,
        window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kEngineCombo)),
        nullptr,
        nullptr);
    SendMessageW(
        g_engine_combo,
        CB_ADDSTRING,
        0,
        reinterpret_cast<LPARAM>(L"Argos"));
    SendMessageW(
        g_engine_combo,
        CB_ADDSTRING,
        0,
        reinterpret_cast<LPARAM>(L"NLLB-200"));
    SendMessageW(
        g_engine_combo,
        CB_ADDSTRING,
        0,
        reinterpret_cast<LPARAM>(L"Firefox"));
    SendMessageW(
        g_engine_combo,
        CB_ADDSTRING,
        0,
        reinterpret_cast<LPARAM>(L"MarianMT"));
    SendMessageW(
        g_engine_combo,
        CB_ADDSTRING,
        0,
        reinterpret_cast<LPARAM>(L"Google (онлайн)"));
    SendMessageW(g_engine_combo, CB_SETCURSEL, 0, 0);
    g_packages_button = CreateWindowW(
        L"BUTTON",
        L"Пакеты",
        WS_VISIBLE | WS_CHILD | BS_OWNERDRAW | WS_TABSTOP,
        390,
        12,
        110,
        28,
        window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kPackagesButton)),
        nullptr,
        nullptr);
    g_settings_button = CreateWindowW(
        L"BUTTON",
        L"Настройки",
        WS_VISIBLE | WS_CHILD | BS_OWNERDRAW | WS_TABSTOP,
        272,
        12,
        110,
        28,
        window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSettingsButton)),
        nullptr,
        nullptr);
    CreateWindowW(
        L"STATIC",
        L"С языка:",
        WS_VISIBLE | WS_CHILD,
        16,
        52,
        70,
        24,
        window,
        nullptr,
        nullptr,
        nullptr);
    g_source_language_combo = CreateWindowW(
        L"COMBOBOX",
        nullptr,
        WS_VISIBLE | WS_CHILD | CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_VSCROLL | WS_TABSTOP,
        90,
        48,
        150,
        420,
        window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSourceLanguageCombo)),
        nullptr,
        nullptr);
    CreateWindowW(
        L"STATIC",
        L"На язык:",
        WS_VISIBLE | WS_CHILD,
        270,
        52,
        60,
        24,
        window,
        nullptr,
        nullptr,
        nullptr);
    g_target_language_combo = CreateWindowW(
        L"COMBOBOX",
        nullptr,
        WS_VISIBLE | WS_CHILD | CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_VSCROLL | WS_TABSTOP,
        335,
        48,
        130,
        420,
        window,
        reinterpret_cast<HMENU>(
            static_cast<INT_PTR>(kTargetLanguageCombo)),
        nullptr,
        nullptr);
    // Видна, когда выбраны два конкретных языка: перевод идёт в обе стороны.
    // При «С языка» = Авто скрывается.
    g_both_directions_label = CreateWindowW(
        L"STATIC",
        L"переводит в обоих направлениях",
        WS_CHILD,
        473,
        52,
        250,
        20,
        window,
        nullptr,
        nullptr,
        nullptr);
    // Вторая строка (появляется только при «С языка» = Авто): третий язык,
    // на который переводим, если определённый язык совпал с «На язык».
    g_fallback_target_label = CreateWindowW(
        L"STATIC",
        L"Если язык переводимого текста совпал с «На язык» — переводить на:",
        WS_CHILD,
        16,
        84,
        480,
        20,
        window,
        nullptr,
        nullptr,
        nullptr);
    g_fallback_target_language_combo = CreateWindowW(
        L"COMBOBOX",
        nullptr,
        WS_CHILD | CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_VSCROLL | WS_TABSTOP,
        502,
        80,
        170,
        420,
        window,
        reinterpret_cast<HMENU>(
            static_cast<INT_PTR>(kFallbackTargetCombo)),
        nullptr,
        nullptr);
    g_source_edit = CreateWindowW(
        L"EDIT",
        L"Hello world",
        WS_VISIBLE | WS_CHILD | WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL |
            WS_VSCROLL | WS_TABSTOP,
        16,
        84,
        470,
        90,
        window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSourceEdit)),
        nullptr,
        nullptr);
    g_translate_button = CreateWindowW(
        L"BUTTON",
        L"Перевести",
        WS_VISIBLE | WS_CHILD | BS_OWNERDRAW | WS_TABSTOP,
        16,
        186,
        120,
        30,
        window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kTranslateButton)),
        nullptr,
        nullptr);
    g_screen_ocr_button = CreateWindowW(
        L"BUTTON",
        L"Область экрана",
        WS_VISIBLE | WS_CHILD | BS_OWNERDRAW | WS_TABSTOP,
        144,
        186,
        130,
        30,
        window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kScreenOcrButton)),
        nullptr,
        nullptr);
    g_image_button = CreateWindowW(
        L"BUTTON",
        L"Изображение…",
        WS_VISIBLE | WS_CHILD | BS_OWNERDRAW | WS_TABSTOP,
        282,
        186,
        130,
        30,
        window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kImageButton)),
        nullptr,
        nullptr);
    g_result_edit = CreateWindowW(
        L"EDIT",
        nullptr,
        WS_VISIBLE | WS_CHILD | WS_BORDER | ES_MULTILINE | ES_READONLY |
            ES_AUTOVSCROLL | WS_VSCROLL,
        16,
        228,
        470,
        140,
        window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kResultEdit)),
        nullptr,
        nullptr);
    g_status_label = CreateWindowW(
        L"STATIC",
        L"Готово",
        WS_VISIBLE | WS_CHILD | SS_LEFT,
        16,
        378,
        470,
        20,
        window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kStatusLabel)),
        nullptr,
        nullptr);
    const auto settings = offline_translator::load_settings();
    store_runtime_settings(settings);
    apply_settings_to_ui(settings);
    layout_main(window);
    update_fallback_language_row(window);
    if (g_packages_button) {
        SetWindowSubclass(g_packages_button, flat_button_subclass, 2, 0);
    }
    if (g_settings_button) {
        SetWindowSubclass(g_settings_button, flat_button_subclass, 2, 0);
    }
    if (g_translate_button) {
        SetWindowSubclass(g_translate_button, flat_button_subclass, 2, 0);
    }
    if (g_screen_ocr_button) {
        SetWindowSubclass(g_screen_ocr_button, flat_button_subclass, 2, 0);
    }
    if (g_image_button) {
        SetWindowSubclass(g_image_button, flat_button_subclass, 2, 0);
    }
    apply_theme_to_window(window);
}

void close_packages_window() {
    if (!g_runtime || !g_runtime->packages_window) {
        return;
    }
    HWND packages = g_runtime->packages_window;
    HWND main = g_packages_parent ? g_packages_parent : g_runtime->main_window;
    g_packages_parent = nullptr;
    g_runtime->packages_window = nullptr;
    g_package_list = nullptr;
    g_package_install = nullptr;
    g_package_uninstall = nullptr;
    g_package_status = nullptr;
    DestroyWindow(packages);
    if (main) {
        EnableWindow(main, TRUE);
        SetForegroundWindow(main);
    }
}

LRESULT CALLBACK packages_proc(
    HWND window,
    UINT message,
    WPARAM w_param,
    LPARAM l_param) {
    try {
        if (message == WM_CREATE) {
            g_package_caption = CreateWindowW(
                L"STATIC",
                L"Установленные и доступные языковые пакеты:",
                WS_VISIBLE | WS_CHILD,
                16,
                12,
                430,
                20,
                window,
                nullptr,
                nullptr,
                nullptr);
            CreateWindowW(
                L"STATIC",
                L"Поиск:",
                WS_VISIBLE | WS_CHILD,
                16,
                40,
                48,
                20,
                window,
                nullptr,
                nullptr,
                nullptr);
            g_package_search = CreateWindowW(
                L"EDIT",
                nullptr,
                WS_VISIBLE | WS_CHILD | WS_BORDER | WS_TABSTOP |
                    ES_AUTOHSCROLL,
                68,
                36,
                398,
                24,
                window,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(
                    kPackageSearchEdit)),
                nullptr,
                nullptr);
            SendMessageW(
                g_package_search,
                WM_SETFONT,
                reinterpret_cast<WPARAM>(
                    GetStockObject(DEFAULT_GUI_FONT)),
                TRUE);
            SendMessageW(
                g_package_search,
                EM_SETCUEBANNER,
                TRUE,
                reinterpret_cast<LPARAM>(
                    offline_translator::tr(L"код или язык: ru, немецкий...")
                        .c_str()));
            g_package_arch_caption = CreateWindowW(
                L"STATIC",
                L"Размер:",
                WS_CHILD,  // показывается только для движка Firefox
                240,
                15,
                56,
                20,
                window,
                nullptr,
                nullptr,
                nullptr);
            g_package_arch_combo = CreateWindowW(
                L"COMBOBOX",
                nullptr,
                WS_CHILD | CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_TABSTOP,
                302,
                11,
                148,
                160,
                window,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(
                    kPackageArchCombo)),
                nullptr,
                nullptr);
            SendMessageW(g_package_arch_combo, CB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>(L"tiny"));
            SendMessageW(g_package_arch_combo, CB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>(L"base"));
            g_package_list = CreateWindowW(
                L"LISTBOX",
                nullptr,
                WS_VISIBLE | WS_CHILD | WS_BORDER | WS_VSCROLL | LBS_NOTIFY |
                    WS_TABSTOP | LBS_NOINTEGRALHEIGHT,
                16,
                66,
                450,
                206,
                window,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kPackageList)),
                nullptr,
                nullptr);
            g_package_status = CreateWindowW(
                L"STATIC",
                L"",
                WS_VISIBLE | WS_CHILD | SS_LEFT,
                16,
                276,
                450,
                22,
                window,
                nullptr,
                nullptr,
                nullptr);
            g_package_install = CreateWindowW(
                L"BUTTON",
                L"Установить",
                WS_VISIBLE | WS_CHILD | BS_OWNERDRAW | WS_TABSTOP,
                16,
                306,
                120,
                30,
                window,
                reinterpret_cast<HMENU>(
                    static_cast<INT_PTR>(kPackageInstallButton)),
                nullptr,
                nullptr);
            g_package_uninstall = CreateWindowW(
                L"BUTTON",
                L"Удалить",
                WS_VISIBLE | WS_CHILD | BS_OWNERDRAW | WS_TABSTOP,
                148,
                306,
                120,
                30,
                window,
                reinterpret_cast<HMENU>(
                    static_cast<INT_PTR>(kPackageUninstallButton)),
                nullptr,
                nullptr);
            CreateWindowW(
                L"BUTTON",
                L"Закрыть",
                WS_VISIBLE | WS_CHILD | BS_OWNERDRAW | WS_TABSTOP,
                346,
                306,
                120,
                30,
                window,
                reinterpret_cast<HMENU>(
                    static_cast<INT_PTR>(kPackageCloseButton)),
                nullptr,
                nullptr);
            refresh_package_list();
            if (!g_pending_package_filter.empty()) {
                SetWindowTextW(
                    g_package_search,
                    g_pending_package_filter.c_str());
                offline_translator::app_log_info(
                    "пакеты: фильтр по языку " +
                    to_utf8(g_pending_package_filter) +
                    " " + runtime_flags());
                g_pending_package_filter.clear();
                apply_package_filter();
            }
            apply_theme_to_window(window);
            localize_window(window);
            std::thread([window]() {
                if (selected_engine() ==
                    offline_translator::EngineKind::marian) {
                    offline_translator::MarianModelManager::update_remote_index();
                    post_payload(
                        window,
                        kPackageIndexMessage,
                        L"Каталог MarianMT обновлён",
                        false);
                    return;
                }
                offline_translator::ArgosModelManager::update_remote_index();
                post_payload(
                    window,
                    kPackageIndexMessage,
                    L"Каталог Argos обновлён",
                    false);
            }).detach();
            return 0;
        }
        if (message == WM_ERASEBKGND) {
            RECT client{};
            GetClientRect(window, &client);
            FillRect(
                reinterpret_cast<HDC>(w_param),
                &client,
                theme_background_brush());
            return 1;
        }
        if (message == WM_CTLCOLORSTATIC || message == WM_CTLCOLOREDIT ||
            message == WM_CTLCOLORLISTBOX || message == WM_CTLCOLORBTN) {
            return theme_control_color(w_param, l_param);
        }
        if (message == WM_MEASUREITEM) {
            auto* measure = reinterpret_cast<MEASUREITEMSTRUCT*>(l_param);
            if (measure && measure->CtlType == ODT_COMBOBOX) {
                measure_owner_combo(measure);
                return TRUE;
            }
        }
        if (message == WM_DRAWITEM) {
            auto* draw = reinterpret_cast<DRAWITEMSTRUCT*>(l_param);
            if (draw && draw->CtlType == ODT_COMBOBOX) {
                draw_owner_combo(*draw);
                return TRUE;
            }
            if (draw && draw->CtlType == ODT_BUTTON) {
                draw_flat_button(*draw);
                return TRUE;
            }
        }
        if (message == kPackageIndexMessage) {
            std::unique_ptr<StatusPayload> payload(
                reinterpret_cast<StatusPayload*>(l_param));
            if (!g_runtime || g_runtime->packages_busy) {
                return 0;
            }
            refresh_package_list();
            if (g_package_status && !g_runtime->packages_busy) {
                update_packages_status_count();
            }
            static_cast<void>(payload);
            return 0;
        }
    if (message == WM_COMMAND) {
        const int id = LOWORD(w_param);
        offline_translator::app_log_info(
            "popup: WM_COMMAND id=" + std::to_string(id));
            const int notification = HIWORD(w_param);
            if (id == kPackageCloseButton) {
                close_packages_window();
                return 0;
            }
            if (id == kPackageInstallButton) {
                start_package_job(window, true);
                return 0;
            }
            if (id == kPackageUninstallButton) {
                start_package_job(window, false);
                return 0;
            }
            if (id == kPackageSearchEdit &&
                notification == EN_CHANGE &&
                !g_runtime->packages_busy) {
                apply_package_filter();
            }
            if (id == kPackageArchCombo &&
                notification == CBN_SELCHANGE && g_runtime) {
                const LRESULT arch =
                    SendMessageW(g_package_arch_combo, CB_GETCURSEL, 0, 0);
                update_runtime_settings(
                    [arch](offline_translator::AppSettings& current) {
                        current.architecture = arch == 1 ? "base" : "tiny";
                    });
                try {
                    if (!g_smoke_mode) {
                        offline_translator::save_settings(g_runtime->settings);
                    }
                } catch (const std::exception&) {
                }
                refresh_package_list();
            }
        }
        if (message == kPackageProgressMessage) {
            std::unique_ptr<StatusPayload> payload(
                reinterpret_cast<StatusPayload*>(l_param));
            if (g_package_status) {
                SetWindowTextW(g_package_status, payload->text.c_str());
            }
            return 0;
        }
        if (message == kPackageDoneMessage) {
            std::unique_ptr<StatusPayload> payload(
                reinterpret_cast<StatusPayload*>(l_param));
            set_packages_busy(false);
            refresh_package_list();
            if (g_package_status) {
                SetWindowTextW(g_package_status, payload->text.c_str());
            }
            if (payload->failed) {
                localized_message_box(
                    window,
                    payload->text.c_str(),
                    L"Ошибка пакета",
                    MB_ICONERROR | MB_OK);
            }
            return 0;
        }
        if (message == WM_CLOSE) {
            if (g_runtime && g_runtime->packages_busy) {
                localized_message_box(
                    window,
                    L"Дождитесь окончания установки или удаления.",
                    L"Пакеты",
                    MB_ICONINFORMATION | MB_OK);
                return 0;
            }
            close_packages_window();
            return 0;
        }
        if (message == WM_DESTROY) {
            g_package_list = nullptr;
            g_package_install = nullptr;
            g_package_uninstall = nullptr;
            g_package_status = nullptr;
            g_package_search = nullptr;
            g_package_arch_caption = nullptr;
            g_package_arch_combo = nullptr;
            g_package_caption = nullptr;
            if (g_runtime && g_runtime->packages_window == window) {
                g_runtime->packages_window = nullptr;
            }
            return 0;
        }
    } catch (const std::exception& error) {
        localized_message_box(
            window,
            from_utf8(error.what()).c_str(),
            L"Ошибка",
            MB_ICONERROR | MB_OK);
        return 0;
    }
    return DefWindowProcW(window, message, w_param, l_param);
}

void open_packages_window(
    HWND parent,
    HINSTANCE instance,
    const std::wstring& language_filter) {
    if (g_runtime && g_runtime->packages_window) {
        SetForegroundWindow(g_runtime->packages_window);
        // Окно уже открыто: применяем фильтр к существующему списку.
        if (!language_filter.empty() && g_package_search) {
            SetWindowTextW(g_package_search, language_filter.c_str());
            apply_package_filter();
        }
        return;
    }
    g_pending_package_filter = language_filter;
    HWND packages = CreateWindowW(
        L"TLingPackages",
        L"Пакеты моделей",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        500,
        400,
        parent,
        nullptr,
        instance,
        nullptr);
    if (!packages) {
        throw std::runtime_error("Не удалось открыть окно пакетов");
    }
    if (g_runtime) {
        g_runtime->packages_window = packages;
    }
    // Родитель запоминаем: окно пакетов открывается и из главного окна,
    // и из настроек — разблокировать надо именно того, кто блокировался.
    g_packages_parent = parent;
    EnableWindow(parent, FALSE);
    ShowWindow(packages, SW_SHOW);
    UpdateWindow(packages);
}

LRESULT CALLBACK window_proc(
    HWND window,
    UINT message,
    WPARAM w_param,
    LPARAM l_param) {
    try {
        if (message == WM_CREATE) {
            create_main_controls(window);
            localize_window(window);
            if (!g_smoke_mode) {
                SetTimer(window, kSelectionPollTimer, 40, nullptr);
            }
            return 0;
        }
        if (message == WM_ACTIVATE) {
            apply_dark_title_bar(window);
        }
        if (message == WM_SIZE) {
            layout_main(window);
            return 0;
        }
        if (message == WM_GETMINMAXINFO) {
            auto* info = reinterpret_cast<MINMAXINFO*>(l_param);
            info->ptMinTrackSize.x = kMinWindowWidth;
            info->ptMinTrackSize.y = kMinWindowHeight;
            return 0;
        }
        if (message == WM_EXITSIZEMOVE) {
            persist_settings(window);
            return 0;
        }
        if (message == WM_MEASUREITEM) {
            auto* measure = reinterpret_cast<MEASUREITEMSTRUCT*>(l_param);
            if (measure && measure->CtlType == ODT_MENU) {
                measure->itemWidth = 240;
                measure->itemHeight = 26;
                return TRUE;
            }
            if (measure && measure->CtlType == ODT_COMBOBOX) {
                measure_owner_combo(measure);
                return TRUE;
            }
        }
        if (message == WM_ERASEBKGND) {
            RECT client{};
            GetClientRect(window, &client);
            FillRect(
                reinterpret_cast<HDC>(w_param),
                &client,
                theme_background_brush());
            return 1;
        }
        if (message == WM_CTLCOLORSTATIC || message == WM_CTLCOLOREDIT ||
            message == WM_CTLCOLORLISTBOX || message == WM_CTLCOLORBTN) {
            return theme_control_color(w_param, l_param);
        }
        if (message == WM_DRAWITEM) {
            auto* draw = reinterpret_cast<DRAWITEMSTRUCT*>(l_param);
            if (draw && draw->CtlType == ODT_BUTTON) {
                const UiTheme theme = current_ui_theme();
                const bool hovered =
                    GetPropW(draw->hwndItem, kHoverPropertyName) != nullptr;
                HBRUSH brush = CreateSolidBrush(
                    hovered ? theme.button_hover : theme.button_face);
                FillRect(draw->hDC, &draw->rcItem, brush);
                DeleteObject(brush);
                SetBkMode(draw->hDC, TRANSPARENT);
                SetTextColor(draw->hDC, theme.button_text);
                wchar_t label[64]{};
                GetWindowTextW(draw->hwndItem, label, 64);
                DrawTextW(
                    draw->hDC,
                    label,
                    -1,
                    &draw->rcItem,
                    DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                return TRUE;
            }
            if (draw && draw->CtlType == ODT_COMBOBOX) {
                draw_owner_combo(*draw);
                return TRUE;
            }
            if (draw && draw->CtlType == ODT_MENU) {
                const UiTheme theme = current_ui_theme();
                const bool selected = (draw->itemState & ODS_SELECTED) != 0;
                HBRUSH brush = CreateSolidBrush(
                    selected ? theme.button_hover : theme.background);
                FillRect(draw->hDC, &draw->rcItem, brush);
                DeleteObject(brush);
                SetBkMode(draw->hDC, TRANSPARENT);
                SetTextColor(draw->hDC, theme.text);
                RECT text_bounds = draw->rcItem;
                text_bounds.left += 24;
                if (draw->itemState & ODS_CHECKED) {
                    // Галочку рисуем линиями: символ «✓» зависит от шрифта
                    // и часто подменяется на «?».
                    const int center =
                        draw->rcItem.top +
                        (draw->rcItem.bottom - draw->rcItem.top) / 2;
                    const int left = draw->rcItem.left + 7;
                    HPEN pen = CreatePen(PS_SOLID, 2, theme.text);
                    HGDIOBJ old_pen = SelectObject(draw->hDC, pen);
                    MoveToEx(draw->hDC, left, center, nullptr);
                    LineTo(draw->hDC, left + 4, center + 5);
                    LineTo(draw->hDC, left + 11, center - 5);
                    SelectObject(draw->hDC, old_pen);
                    DeleteObject(pen);
                }
                const std::wstring tray_label = offline_translator::tr(
                    tray_menu_label(static_cast<UINT>(draw->itemID)));
                DrawTextW(
                    draw->hDC,
                    tray_label.c_str(),
                    -1,
                    &text_bounds,
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                return TRUE;
            }
        }
        if (message == WM_COMMAND &&
            LOWORD(w_param) == kTranslateButton &&
            HIWORD(w_param) == BN_CLICKED) {
            start_translation(window);
            return 0;
        }
        if (message == WM_COMMAND &&
            LOWORD(w_param) == kScreenOcrButton &&
            HIWORD(w_param) == BN_CLICKED) {
            start_screen_ocr(window);
            return 0;
        }
        if (message == WM_COMMAND &&
            LOWORD(w_param) == kImageButton &&
            HIWORD(w_param) == BN_CLICKED) {
            start_image_file_translation(window);
            return 0;
        }
        if (message == WM_COMMAND &&
            LOWORD(w_param) == kPackagesButton &&
            HIWORD(w_param) == BN_CLICKED) {
            open_packages_window(
                window,
                window_instance(window));
            return 0;
        }
        if (message == WM_COMMAND &&
            LOWORD(w_param) == kSettingsButton &&
            HIWORD(w_param) == BN_CLICKED) {
            open_settings_window(window, window_instance(window));
            return 0;
        }
        if (message == WM_COMMAND && LOWORD(w_param) == kTrayOpen) {
            restore_from_tray(window);
            return 0;
        }
        if (message == WM_COMMAND && LOWORD(w_param) == kTrayExit) {
            using offline_translator::UiCommand;
            using offline_translator::effect_for;
            using offline_translator::UiEffect;
            if (effect_for(UiCommand::tray_exit) == UiEffect::destroy_and_quit) {
                DestroyWindow(window);
            }
            return 0;
        }
        if (message == WM_COMMAND && LOWORD(w_param) == kTrayAutoCopy) {
            if (g_runtime) {
                bool enabled = false;
                update_runtime_settings(
                    [&enabled](offline_translator::AppSettings& current) {
                        enabled = !current.auto_copy_selection;
                        current.auto_copy_selection = enabled;
                    });
                try {
                    if (!g_smoke_mode) {
                        offline_translator::save_settings(g_runtime->settings);
                    }
                } catch (const std::exception&) {
                }
                // Синхронизируем галочку в настройках, если окно открыто.
                if (g_settings_auto_copy) {
                    SendMessageW(
                        g_settings_auto_copy,
                        BM_SETCHECK,
                        enabled ? BST_CHECKED : BST_UNCHECKED,
                        0);
                }
                set_status(
                    offline_translator::tr(
                        enabled ? L"Автокопирование включено"
                                : L"Автокопирование выключено"));
                offline_translator::app_log_info(
                    std::string("трей: автокопирование ") +
                    (enabled ? "включено" : "выключено"));
            }
            return 0;
        }
if (message == WM_COMMAND && LOWORD(w_param) == kTrayCheckUpdate) {
                // Проверка идёт тихо: результат приходит во всплывающем
                // сообщении, настройки открываются только если обновление
                // найдено (чтобы предложить кнопку установки).
                start_update_check();
                return 0;
            }
        if (message == WM_COMMAND && LOWORD(w_param) == kTrayScreenOcr) {
            start_screen_ocr(window);
            return 0;
        }
        if (message == WM_COMMAND && LOWORD(w_param) == kTrayImageFile) {
            start_image_file_translation(window);
            return 0;
        }
        if (message == WM_COMMAND && LOWORD(w_param) == kTrayImageRegion) {
            start_image_region_translation(window);
            return 0;
        }
        if (message == WM_COMMAND && LOWORD(w_param) == kTrayFloatButton) {
            const bool show = !(g_runtime &&
                g_runtime->settings.image_float_button);
            show_float_button(show);
            return 0;
        }
        if (message == WM_COMMAND && LOWORD(w_param) == kTrayOcrOnly) {
            const bool enable = !g_ocr_only_session;
            g_ocr_only_session = enable;
            if (enable && !(g_runtime && g_runtime->settings.ocr_only)) {
                localized_message_box(
                    window,
                    L"Режим «Только OCR» включён до перезапуска программы.\n\n"
                    L"Чтобы включить навсегда, отметьте «Только OCR всегда» "
                    L"в Настройки → Поведение.",
                    L"Только OCR",
                    MB_ICONINFORMATION | MB_OK);
            }
            set_status(enable
                ? L"Режим «Только OCR» включён (до перезапуска)"
                : L"Режим «Только OCR» выключен");
            return 0;
        }
        if (message == kUpdateCheckDoneMessage) {
            // Проверка была запущена из трея: показываем результат
            // всплывающим сообщением, а настройки открываем только когда
            // обновление найдено — там живёт кнопка установки.
            std::unique_ptr<UpdateCheckPayload> payload(
                reinterpret_cast<UpdateCheckPayload*>(l_param));
            g_pending_update = payload->info;
            set_update_controls_busy(false);
            if (payload->failed) {
                show_tray_balloon(
                    offline_translator::tr(L"Ошибка"), payload->text, true);
            } else if (payload->info) {
                show_tray_balloon(
                    offline_translator::tr(L"Доступно обновление"),
                    payload->text,
                    false);
                try {
                    open_settings_window(window, window_instance(window));
                    if (g_runtime && g_runtime->settings_window) {
                        settings_show_page(
                            g_runtime->settings_window,
                            kSettingsPageAbout);
                        // Окно только что создано: обновляем состояние
                        // кнопок, иначе «Скачать и установить» останется
                        // неактивной до повторной проверки.
                        set_update_controls_busy(false);
                    }
                } catch (const std::exception& error) {
                    offline_translator::app_log_error(
                        std::string("не удалось открыть настройки: ") +
                        error.what());
                }
            } else {
                show_tray_balloon(
                    offline_translator::tr(L"Обновлений нет"),
                    payload->text,
                    false);
            }
            return 0;
        }
        if (message == WM_COMMAND && LOWORD(w_param) == kTrayTurbo) {
            if (g_runtime) {
                bool enabled = false;
                update_runtime_settings(
                    [&enabled](offline_translator::AppSettings& current) {
                        enabled = !current.turbo_translation;
                        current.turbo_translation = enabled;
                    });
                try {
                    if (!g_smoke_mode) {
                        offline_translator::save_settings(g_runtime->settings);
                    }
                } catch (const std::exception&) {
                }
                set_status(
                    offline_translator::tr(
                        enabled ? L"Турбо перевод включён"
                                : L"Турбо перевод выключен"));
                offline_translator::app_log_info(
                    std::string("трей: турбо перевод ") +
                    (enabled ? "включён" : "выключен"));
            }
            return 0;
        }
        if (message == WM_COMMAND && LOWORD(w_param) == kTrayHistoryClear) {
            try {
                offline_translator::clear_clipboard_history();
            } catch (const std::exception&) {
            }
            g_tray_history_items.clear();
            g_tray_history_labels.clear();
            return 0;
        }
        if (message == WM_COMMAND) {
            const UINT command_id = LOWORD(w_param);
            if (command_id >= kTrayHistoryFirst &&
                command_id <= kTrayHistoryLast) {
                const std::size_t index =
                    static_cast<std::size_t>(command_id - kTrayHistoryFirst);
                if (index < g_tray_history_items.size()) {
                    copy_text_to_clipboard(window, g_tray_history_items[index]);
                }
                return 0;
            }
            if (command_id >= kTrayHistoryTranslateFirst &&
                command_id <= kTrayHistoryTranslateLast) {
                const std::size_t index = static_cast<std::size_t>(
                    command_id - kTrayHistoryTranslateFirst);
                if (index < g_tray_history_items.size()) {
                    g_selection.selected_text = g_tray_history_items[index];
                    start_selection_translation(window);
                }
                return 0;
            }
        }
        if (message == kDropdownOpenedMessage) {
            // Показываем список языков с первого пункта: иначе Windows
            // прокручивает его к выбранному языку, и верхние пункты
            // («Авто», «Английский») остаются скрытыми.
            const int id = static_cast<int>(LOWORD(w_param));
            HWND combo = nullptr;
            if (id == kSourceLanguageCombo) {
                combo = g_source_language_combo;
            } else if (id == kTargetLanguageCombo) {
                combo = g_target_language_combo;
            } else if (id == kFallbackTargetCombo) {
                combo = g_fallback_target_language_combo;
            }
            if (combo) {
                SendMessageW(combo, CB_SETTOPINDEX, 0, 0);
            }
            return 0;
        }
        if (message == WM_COMMAND &&
            (LOWORD(w_param) == kSourceLanguageCombo ||
             LOWORD(w_param) == kTargetLanguageCombo ||
             LOWORD(w_param) == kFallbackTargetCombo) &&
            HIWORD(w_param) == CBN_DROPDOWN) {
            // Список языков длинный, а Windows прокручивает его к выбранному
            // пункту: «Авто» и первые языки оставались невидимыми. Показываем
            // список с начала.
            // Список дорисовывается после этого уведомления, поэтому
            // прокрутку к началу делаем отдельным сообщением.
            PostMessageW(
                window,
                kDropdownOpenedMessage,
                w_param,
                l_param);
            return 0;
        }
        if (message == WM_COMMAND &&
            (LOWORD(w_param) == kEngineCombo ||
             LOWORD(w_param) == kSourceLanguageCombo ||
             LOWORD(w_param) == kTargetLanguageCombo ||
             LOWORD(w_param) == kFallbackTargetCombo) &&
            HIWORD(w_param) == CBN_SELCHANGE) {
            if (LOWORD(w_param) == kSourceLanguageCombo) {
                g_auto_source_language =
                    selected_source_language(g_source_language_combo) ==
                    kAutoSourceLanguage;
                update_fallback_language_row(window);
            }
            persist_settings(window);
            if (LOWORD(w_param) == kEngineCombo) {
                switch (selected_engine()) {
                    case offline_translator::EngineKind::nllb:
                        set_status(
                            L"Выбран NLLB-200. Модель загрузится при переводе.");
                        break;
                    case offline_translator::EngineKind::firefox:
                        set_status(
                            L"Выбран Firefox. Пакет загрузится при переводе.");
                        break;
                    case offline_translator::EngineKind::marian:
                        set_status(
                            L"Выбран MarianMT. Пакет загрузится при переводе.");
                        break;
                    case offline_translator::EngineKind::argos:
                        set_status(
                            L"Движок Argos. Модели загружаются по парам.");
                        break;
                    case offline_translator::EngineKind::google:
                        set_status(
                            L"Google (онлайн). Нужен интернет; текст уходит на сервис Google.");
                        break;
                }
            }
            return 0;
        }
        if (message == kStatusMessage) {
            std::unique_ptr<StatusPayload> payload(
                reinterpret_cast<StatusPayload*>(l_param));
            set_status(payload->text);
            return 0;
        }
        if (message == kExcelResultMessage) {
            std::unique_ptr<StatusPayload> payload(
                reinterpret_cast<StatusPayload*>(l_param));
            set_status(payload->text);
            show_tray_balloon(
                L"TLing — Excel",
                payload->text,
                payload->failed);
            return 0;
        }
        if (message == kImageResultMessage) {
            std::unique_ptr<StatusPayload> payload(
                reinterpret_cast<StatusPayload*>(l_param));
            set_status(payload->text);
            show_tray_balloon(
                L"TLing — изображение",
                payload->text,
                payload->failed);
            return 0;
        }
        if (message == kUpdateInstallDoneMessage) {
            std::unique_ptr<UpdateInstallPayload> payload(
                reinterpret_cast<UpdateInstallPayload*>(l_param));
            set_status(payload->text);
            set_update_controls_busy(false);
            if (!payload->failed && payload->quit) {
                DestroyWindow(window);
            }
            return 0;
        }
        if (message == kStartupUpdateCheckDoneMessage) {
            std::unique_ptr<UpdateCheckPayload> payload(
                reinterpret_cast<UpdateCheckPayload*>(l_param));
            // Обновлений нет (или ошибка) — молча ничего не показываем.
            if (payload->info) {
                g_pending_update = payload->info;
                show_update_popup(window, *payload->info);
            }
            return 0;
        }
if (message == kTranslateMessage) {
            std::unique_ptr<StatusPayload> result(
                reinterpret_cast<StatusPayload*>(l_param));
            SetWindowTextW(g_result_edit, result->text.c_str());
            set_main_busy(false);
            if (result->failed) {
                set_status(L"Ошибка. Смотрите результат перевода.");
                // Нет установленной модели для пары: предлагаем скачать её
                // в окне «Пакеты» (маркер ставит route_planner).
                const std::string failed_text = to_utf8(result->text);
                if (failed_text.find(
                        std::string(offline_translator::kMissingModelMarker)) !=
                    std::string::npos) {
                    const int answer = localized_message_box(
                        window,
                        L"Для выбранной пары нет установленной модели. "
                        L"Открыть окно «Пакеты», чтобы скачать её?",
                        L"Нет модели",
                        MB_ICONQUESTION | MB_YESNO);
                    if (answer == IDYES) {
                        try {
                            open_packages_window(
                                window,
                                window_instance(window),
                                missing_model_filter(result->text));
                        } catch (const std::exception& error) {
                            offline_translator::app_log_error(
                                std::string("не удалось открыть пакеты: ") +
                                error.what());
                        }
                    }
                }
            }
            return 0;
        }
        if (message == kSelectionResultMessage) {
            std::unique_ptr<StatusPayload> result(
                reinterpret_cast<StatusPayload*>(l_param));
            const auto direction = selection_translation_direction(
                g_selection.selected_text);
            // Показываем фактически использованную пару (автоопределение,
            // смена направления), а не текущие значения из настроек.
            const std::string source_code = result->source_code.empty()
                ? direction.first
                : result->source_code;
            const std::string target_code = result->target_code.empty()
                ? direction.second
                : result->target_code;
            show_result_popup(
                window,
                result->text,
                source_code,
                target_code);
            g_screen_ocr_mode = false;
            if (result->failed) {
                set_status(L"Ошибка перевода выделения");
                // Нет модели для пары — предлагаем скачать её в «Пакетах»
                // (так же, как в главном окне).
                const std::string failed_text = to_utf8(result->text);
                if (failed_text.find(
                        std::string(offline_translator::kMissingModelMarker)) !=
                    std::string::npos) {
                    const int answer = localized_message_box(
                        window,
                        L"Для выбранной пары нет установленной модели. "
                        L"Открыть окно «Пакеты», чтобы скачать её?",
                        L"Нет модели",
                        MB_ICONQUESTION | MB_YESNO);
                    if (answer == IDYES) {
                        try {
                            open_packages_window(
                                window,
                                window_instance(window),
                                missing_model_filter(result->text));
                        } catch (const std::exception& error) {
                            offline_translator::app_log_error(
                                std::string("не удалось открыть пакеты: ") +
                                error.what());
                        }
                    }
                }
            }
            return 0;
        }
        if (message == kTrayMessage) {
            if (l_param == WM_LBUTTONDBLCLK || l_param == WM_LBUTTONUP) {
                restore_from_tray(window);
                return 0;
            }
            if (l_param == WM_RBUTTONUP || l_param == WM_CONTEXTMENU) {
                show_tray_menu(window);
                return 0;
            }
            return 0;
        }
        if (message == kRecoverInputMessage) {
            recover_input_hooks(window, "сторож: опрос замер");
            return 0;
        }
        if (message == WM_POWERBROADCAST) {
            if (w_param == PBT_APMSUSPEND) {
                offline_translator::app_log_warn("система засыпает " + runtime_flags());
            } else if (
                w_param == PBT_APMRESUMESUSPEND ||
                w_param == PBT_APMRESUMEAUTOMATIC) {
                offline_translator::app_log_warn("система проснулась " + runtime_flags());
                recover_input_hooks(window, "пробуждение");
            }
            return TRUE;
        }
        if (message == WM_TIMER && w_param == kSelectionPollTimer) {
            note_poll_tick(window);
            poll_selection(window);
            return 0;
        }
        if (message == WM_TIMER && w_param == kSelectionButtonHideTimer) {
            hide_selection_button();
            return 0;
        }
        if (message == WM_TIMER && w_param == kStartupUpdateTimer) {
            KillTimer(window, kStartupUpdateTimer);
            if (!g_smoke_mode && g_runtime &&
                g_runtime->settings.check_updates_on_startup) {
                start_startup_update_check(window);
            }
            return 0;
        }
        if (message == WM_HOTKEY &&
            w_param == static_cast<WPARAM>(kScreenOcrHotkey)) {
            start_screen_ocr(window);
            return 0;
        }
        if (message == WM_HOTKEY &&
            w_param == static_cast<WPARAM>(kExcelHotkey)) {
            start_excel_translation(window);
            return 0;
        }
        if (message == WM_CLOSE) {
            using offline_translator::UiCommand;
            using offline_translator::effect_for;
            using offline_translator::UiEffect;
            if (effect_for(UiCommand::window_close) == UiEffect::hide_to_tray) {
                hide_to_tray(window);
                return 0;
            }
            DestroyWindow(window);
            return 0;
        }
        if (message == WM_DESTROY) {
            offline_translator::app_log_info("выход " + runtime_flags());
            persist_settings(window);
            KillTimer(window, kSelectionPollTimer);
            KillTimer(window, kSelectionButtonHideTimer);
            destroy_popup_windows();
            if (g_float_button) {
                remove_float_tooltip();
                DestroyWindow(g_float_button);
                g_float_button = nullptr;
            }
            remove_tray_icon();
            if (g_tray_icon) {
                DestroyIcon(g_tray_icon);
                g_tray_icon = nullptr;
            }
            if (g_runtime) {
                g_runtime->closing = true;
                if (g_runtime->packages_window) {
                    DestroyWindow(g_runtime->packages_window);
                    g_runtime->packages_window = nullptr;
                }
                if (g_runtime->settings_window) {
                    DestroyWindow(g_runtime->settings_window);
                    g_runtime->settings_window = nullptr;
                }
            }
            UnregisterHotKey(window, kScreenOcrHotkey);
            UnregisterHotKey(window, kExcelHotkey);
            PostQuitMessage(0);
            return 0;
        }
    } catch (const std::exception& error) {
        offline_translator::app_log_error(
            std::string("исключение окна: ") + error.what());
        localized_message_box(
            window,
            from_utf8(error.what()).c_str(),
            L"Ошибка",
            MB_ICONERROR | MB_OK);
        if (g_runtime) {
            g_runtime->busy = false;
        }
        set_main_busy(false);
        return 0;
    }
    return DefWindowProcW(window, message, w_param, l_param);
}

bool command_has_flag(const wchar_t* command_line, const wchar_t* flag) {
    return command_line != nullptr && wcsstr(command_line, flag) != nullptr;
}

int run_smoke_loop(HWND window) {
    using offline_translator::smoke_hides_forever;
    using offline_translator::smoke_may_enable_autostart;
    if (smoke_may_enable_autostart() || smoke_hides_forever()) {
        return 1;
    }
    ShowWindow(window, SW_SHOWNA);
    MSG message{};
    SendMessageW(window, WM_CLOSE, 0, 0);
    if (!IsWindow(window)) {
        return 1;
    }
    if (IsWindowVisible(window)) {
        return 1;
    }
    for (int step = 0; step < 30; ++step) {
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) {
                return static_cast<int>(message.wParam);
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        Sleep(10);
    }
    if (!g_engine_combo || !g_source_language_combo ||
        !g_target_language_combo || !g_translate_button ||
        !g_packages_button || !g_settings_button || !g_image_button) {
        return 1;
    }
    // Плавающая кнопка: создание и удаление по переключателю из трея.
    show_float_button(true);
    if (!IsWindow(g_float_button)) {
        return 1;
    }
    show_float_button(false);
    if (IsWindow(g_float_button)) {
        return 1;
    }
    // Окно настроек: создание, скроллбар, прокрутка и закрытие.
    SendMessageW(window, WM_COMMAND, kSettingsButton, 0);
    for (int step = 0; step < 20; ++step) {
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) {
                return static_cast<int>(message.wParam);
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        Sleep(10);
    }
    if (!g_runtime || !g_runtime->settings_window) {
        return 1;
    }
    if (!(GetWindowLongPtrW(g_runtime->settings_window, GWL_STYLE) & WS_VSCROLL)) {
        return 1;
    }
    // Уменьшаем окно: клиент ниже контента — должен работать скроллбар.
    SetWindowPos(
        g_runtime->settings_window,
        nullptr,
        0,
        0,
        520,
        420,
        SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    for (int step = 0; step < 10; ++step) {
        MSG pump{};
        while (PeekMessageW(&pump, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&pump);
            DispatchMessageW(&pump);
        }
        Sleep(10);
    }
    SendMessageW(
        g_runtime->settings_window,
        WM_VSCROLL,
        MAKEWPARAM(SB_PAGEDOWN, 0),
        0);
    for (int step = 0; step < 10; ++step) {
        MSG pump{};
        while (PeekMessageW(&pump, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&pump);
            DispatchMessageW(&pump);
        }
        Sleep(10);
    }
    if (g_settings_scroll <= 0) {
        return 1;
    }
    SendMessageW(g_runtime->settings_window, WM_CLOSE, 0, 0);
    for (int step = 0; step < 20; ++step) {
        MSG pump{};
        while (PeekMessageW(&pump, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&pump);
            DispatchMessageW(&pump);
        }
        Sleep(10);
    }
    if (IsWindow(g_runtime->settings_window) || g_settings_scroll != 0) {
        return 1;
    }
    DestroyWindow(window);
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (IsWindow(window)) {
        return 1;
    }
    return 0;
}

// Smoke-проверка попапов выделения: кнопка с иконкой и окно результата
// в обоих режимах. Не трогает автозагрузку и треи.
int run_popup_smoke_loop(HWND window) {
    MSG message{};
    // Причина падения записывается в лог: иначе «exit 1» ничего не объясняет.
    auto fail = [](const char* reason) {
        offline_translator::app_log_error(
            std::string("popup smoke: ") + reason);
        return 1;
    };
    auto pump = [&message](int iterations) {
        for (int step = 0; step < iterations; ++step) {
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                if (message.message == WM_QUIT) {
                    return false;
                }
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            Sleep(10);
        }
        return true;
    };

    show_selection_button(120, 120, window);
    if (!IsWindow(g_selection_button)) {
        return fail("кнопка у курсора не создана");
    }
    if (!pump(10)) {
        return static_cast<int>(message.wParam);
    }
    // Иконка проверяется в самом процессе, а не по пикселям экрана: иначе
    // тест ломается, если в этой точке лежит чужое окно (браузер, терминал).
    if (!g_button_uses_icon) {
        return fail("иконка не применилась, сработал запасной вариант Aa");
    }
    {
        HBITMAP composed = nullptr;
        if (!compose_selection_icon_bitmap(&composed) || !composed) {
            return fail("иконка кнопки не собирается");
        }
        HDC screen = GetDC(nullptr);
        HDC memory = CreateCompatibleDC(screen);
        const HGDIOBJ previous = SelectObject(memory, composed);
        BITMAP info{};
        GetObjectW(composed, sizeof(info), &info);
        const COLORREF reference =
            GetPixel(memory, info.bmWidth / 2, info.bmHeight / 2);
        int distinct = 0;
        for (int step_y = 4; step_y < info.bmHeight && distinct < 3;
             step_y += 8) {
            for (int step_x = 4; step_x < info.bmWidth; step_x += 8) {
                if (GetPixel(memory, step_x, step_y) != reference) {
                    ++distinct;
                }
            }
        }
        SelectObject(memory, previous);
        DeleteDC(memory);
        ReleaseDC(nullptr, screen);
        DeleteObject(composed);
        if (distinct < 3) {
            return fail("иконка кнопки пустая");
        }
    }
    hide_selection_button();

    g_runtime->settings.result_window_mode =
        offline_translator::kResultWindowSelectable;
    g_selection.selected_text = L"Проверка перевода";
    show_result_popup(window, L"Проверка перевода", "ru", "en");
    if (!IsWindow(g_result_popup) || !g_popup_result_edit ||
        !g_popup_copy_button) {
        return fail("попап перевода не создан");
    }
    if (!g_popup_add_button) {
        return fail("кнопка добавления к оригиналу не создана");
    }
    RECT popup_bounds{};
    GetWindowRect(g_result_popup, &popup_bounds);
    // Высота должна подстраиваться под короткий текст (2 строки минимум).
    const int selectable_height = popup_bounds.bottom - popup_bounds.top;
    if (selectable_height <= 60 || selectable_height > 400) {
        return fail("неверная высота попапа");
    }
    if (!pump(10)) {
        return static_cast<int>(message.wParam);
    }
    hide_result_popup();

    g_runtime->settings.result_window_mode =
        offline_translator::kResultWindowClickToClose;
    show_result_popup(window, L"Второй режим", "en", "ru");
    if (!IsWindow(g_result_popup) || !g_popup_copy_button) {
        return fail("попап в режиме click_to_close не создан");
    }
    pump(10);
    hide_result_popup();
    if (IsWindow(g_result_popup) || IsWindow(g_selection_button)) {
        return fail("попап или кнопка не закрылись");
    }
    // Окно предложения обновления: своё, в стиле приложения. Проверяем
    // создание и закрытие по «Нет» (кнопка «Да» запустила бы загрузку).
    {
        offline_translator::UpdateInfo info;
        info.release.title = "TLing Test";
        info.release.tag = "v9.9.9";
        show_update_popup(window, info);
        if (!IsWindow(g_update_popup) || !g_update_popup_yes ||
            !g_update_popup_no) {
            return fail("окно обновления не создано");
        }
        if (!pump(3)) {
            return static_cast<int>(message.wParam);
        }
        SendMessageW(g_update_popup, WM_COMMAND, kUpdateNoButton, 0);
        if (IsWindow(g_update_popup)) {
            return fail("окно обновления не закрылось по «Нет»");
        }
    }
    DestroyWindow(window);
    return 0;
}

}  // анонимное пространство имён

int WINAPI wWinMain(
    HINSTANCE instance,
    HINSTANCE,
    PWSTR command_line,
    int show_command) {
    // Резкий текст в legacy-приложении без манифеста: GDI-масштабирование
    // рендерит шрифты в реальном DPI, а не растягивает их bitmap-ом.
    if (HMODULE user32 = GetModuleHandleW(L"user32.dll")) {
        using SetProcessDpiAwarenessContextFn = BOOL(WINAPI*)(void*);
        const auto set_dpi =
            reinterpret_cast<SetProcessDpiAwarenessContextFn>(
                GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
        if (set_dpi != nullptr) {
            // DPI_AWARENESS_CONTEXT_UNAWARE_GDISCALED == (HANDLE)-5.
            set_dpi(reinterpret_cast<void*>(
                static_cast<std::intptr_t>(-5)));
        }
    }
    // Оба smoke-режима идут мимо единственного экземпляра и горячих клавиш:
    // иначе запущенное приложение превращает проверки в тихий no-op.
    g_smoke_mode = command_has_flag(command_line, L"--smoke-start") ||
        command_has_flag(GetCommandLineW(), L"--smoke-start") ||
        command_has_flag(command_line, L"--smoke-popup") ||
        command_has_flag(GetCommandLineW(), L"--smoke-popup");
    g_start_minimized =
        !g_smoke_mode &&
        (command_has_flag(command_line, L"--minimized") ||
         command_has_flag(GetCommandLineW(), L"--minimized"));
    g_runtime = std::make_shared<GuiRuntime>();
    offline_translator::set_capture_wait_hook(&pump_wait);
    if (!g_smoke_mode) {
        offline_translator::init_app_log();
        offline_translator::app_log_info(
            "=== старт " +
            std::string(offline_translator::kAppVersion) +
            " pid=" + std::to_string(GetCurrentProcessId()) +
            " exe=" + to_utf8(executable_path().wstring()) +
            " home=" + to_utf8(
                offline_translator::default_data_root().wstring()));
    }

    // Один экземпляр на пользователя: старый процесс из другой папки
    // (автозапуск/трей) иначе продолжает перехватывать жесты и показывать
    // своё окно результата со старыми ошибками.
    HANDLE single_instance = nullptr;
    const bool cli_translate =
        command_has_flag(command_line, L"--translate-document") ||
        command_has_flag(GetCommandLineW(), L"--translate-document");
    if (!g_smoke_mode && !cli_translate) {
        single_instance = CreateMutexW(
            nullptr,
            TRUE,
            L"Local\\TLingSingleInstance");
        if (single_instance &&
            GetLastError() == ERROR_ALREADY_EXISTS) {
            offline_translator::app_log_warn(
                "уже запущен другой экземпляр — показываю его окно и выходим");
            if (HWND existing = FindWindowW(
                    L"TLingWindow", nullptr)) {
                ShowWindow(existing, SW_RESTORE);
                SetForegroundWindow(existing);
            }
            CloseHandle(single_instance);
            return 0;
        }
    }



    const wchar_t class_name[] = L"TLingWindow";
    WNDCLASSW window_class{};
    window_class.hInstance = instance;
    window_class.lpfnWndProc = window_proc;
    window_class.lpszClassName = class_name;
    window_class.hIcon = load_window_icon(GetSystemMetrics(SM_CXICON));
    window_class.hCursor = LoadCursorW(
        nullptr,
        MAKEINTRESOURCEW(IDC_ARROW));
    window_class.hbrBackground = reinterpret_cast<HBRUSH>(
        COLOR_WINDOW + 1);
    if (!RegisterClassW(&window_class)) {
        return 1;
    }

    WNDCLASSW packages_class{};
    packages_class.hInstance = instance;
    packages_class.lpfnWndProc = packages_proc;
    packages_class.lpszClassName = L"TLingPackages";
    packages_class.hCursor = LoadCursorW(
        nullptr,
        MAKEINTRESOURCEW(IDC_ARROW));
    packages_class.hbrBackground = reinterpret_cast<HBRUSH>(
        COLOR_WINDOW + 1);
    if (!RegisterClassW(&packages_class)) {
        return 1;
    }

    WNDCLASSW settings_class{};
    settings_class.hInstance = instance;
    settings_class.lpfnWndProc = settings_proc;
    settings_class.lpszClassName = L"TLingSettings";
    settings_class.hCursor = LoadCursorW(
        nullptr,
        MAKEINTRESOURCEW(IDC_ARROW));
    settings_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    if (!RegisterClassW(&settings_class)) {
        return 1;
    }

    WNDCLASSW settings_panel_class{};
    settings_panel_class.hInstance = instance;
    settings_panel_class.lpfnWndProc = settings_panel_proc;
    settings_panel_class.lpszClassName = L"TLingSettingsPanel";
    settings_panel_class.hCursor = LoadCursorW(
        nullptr,
        MAKEINTRESOURCEW(IDC_ARROW));
    settings_panel_class.hbrBackground = nullptr;
    if (!RegisterClassW(&settings_panel_class)) {
        return 1;
    }

    WNDCLASSW selection_class{};
    selection_class.hInstance = instance;
    selection_class.lpfnWndProc = selection_button_proc;
    selection_class.lpszClassName = L"TLingSelectionButton";
    selection_class.hCursor = LoadCursorW(
        nullptr,
        MAKEINTRESOURCEW(IDC_HAND));
    selection_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    if (!RegisterClassW(&selection_class)) {
        return 1;
    }

    WNDCLASSW result_class{};
    result_class.hInstance = instance;
    result_class.lpfnWndProc = result_popup_proc;
    result_class.lpszClassName = L"TLingResultPopup";
    result_class.hCursor = LoadCursorW(
        nullptr,
        MAKEINTRESOURCEW(IDC_ARROW));
    result_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    if (!RegisterClassW(&result_class)) {
        return 1;
    }

    WNDCLASSW update_class{};
    update_class.hInstance = instance;
    update_class.lpfnWndProc = update_popup_proc;
    update_class.lpszClassName = L"TLingUpdatePopup";
    update_class.hCursor = LoadCursorW(
        nullptr,
        MAKEINTRESOURCEW(IDC_ARROW));
    update_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    if (!RegisterClassW(&update_class)) {
        return 1;
    }

    WNDCLASSW float_class{};
    float_class.hInstance = instance;
    float_class.lpfnWndProc = float_button_proc;
    float_class.lpszClassName = kFloatButtonClass;
    float_class.hCursor = LoadCursorW(
        nullptr,
        MAKEINTRESOURCEW(IDC_HAND));
    float_class.hbrBackground = nullptr;
    if (!RegisterClassW(&float_class)) {
        return 1;
    }

    const auto settings = offline_translator::load_settings();
    offline_translator::set_ui_language(settings.ui_language);
    apply_ocr_backend_from_settings(settings);
    offline_translator::app_log_info(
        "настройки: engine=" + settings.engine +
        " lang=" + settings.ui_language +
        " modifier=" + settings.popup_modifier +
        " popup=" + std::to_string(settings.selection_popup_enabled) +
        " auto_copy=" + std::to_string(settings.auto_copy_selection) +
        " result=" + settings.result_window_mode +
        " theme=" + settings.ui_theme);
    // Скрытый режим: перевод документа/PDF из консоли (отладка и пакетная
    // обработка): TLing --translate-document <вход> <выход>.
    if (command_has_flag(command_line, L"--translate-document") ||
        command_has_flag(GetCommandLineW(), L"--translate-document")) {
        int argument_count = 0;
        LPWSTR* arguments =
            CommandLineToArgvW(GetCommandLineW(), &argument_count);
        std::filesystem::path input;
        std::filesystem::path output;
        if (arguments != nullptr) {
            for (int index = 0; index + 2 < argument_count; ++index) {
                if (_wcsicmp(arguments[index], L"--translate-document") == 0) {
                    input = arguments[index + 1];
                    output = arguments[index + 2];
                    break;
                }
            }
            LocalFree(arguments);
        }
        if (input.empty() || output.empty()) {
            return 2;
        }
        ExcelJobConfig config;
        config.engine =
            offline_translator::engine_kind_from_settings(settings.engine);
        config.models_root = model_root_for_kind(config.engine);
        config.engine_variant =
            config.engine == offline_translator::EngineKind::firefox
                ? settings.architecture
                : std::string{};
        config.api_key = online_api_key_for_kind(config.engine);
        config.target = settings.target_language;
        config.fallback_target = settings.fallback_target_language;
        if (settings.source_language != "auto" &&
            !settings.source_language.empty()) {
            config.manual_source = true;
            config.source = settings.source_language;
        }
        config.separator = from_utf8(settings.append_separator);
        config.source_path = input;
        config.output_path = output;
        config.output_png =
            _wcsicmp(output.extension().c_str(), L".png") == 0;
        const auto tessdata = offline_translator::default_tessdata_root();
        for (const char* code : {"en", "ru"}) {
            if (offline_translator::tessdata_installed(code, tessdata)) {
                config.ocr_languages.push_back(code);
            }
        }
        if (config.ocr_languages.empty()) {
            config.ocr_languages.push_back("en");
        }
        Gdiplus::GdiplusStartupInput gdiplus_input{};
        ULONG_PTR gdiplus_token = 0;
        const bool gdiplus_ready =
            Gdiplus::GdiplusStartup(
                &gdiplus_token,
                &gdiplus_input,
                nullptr) == Gdiplus::Ok;
        std::wstring message;
        const bool ok =
            process_document(std::move(config), g_runtime, &message);
        if (gdiplus_ready) {
            Gdiplus::GdiplusShutdown(gdiplus_token);
        }
        try {
            std::ofstream status(
                std::filesystem::path(output.wstring() + L".status.txt"),
                std::ios::binary);
            status << (ok ? "ok: " : "fail: ") << to_utf8(message);
        } catch (const std::exception&) {
        }
        offline_translator::app_log_info(
            "translate-document: " + (ok ? std::string("ok") : "fail") +
            " " + to_utf8(message));
        return ok ? 0 : 1;
    }

    const int width = settings.window_width >= kMinWindowWidth
        ? settings.window_width
        : kDefaultWindowWidth;
    const int height = settings.window_height >= kMinWindowHeight
        ? settings.window_height
        : kDefaultWindowHeight;

    HWND window = CreateWindowW(
        class_name,
        L"TLing",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        width,
        height,
        nullptr,
        nullptr,
        instance,
        nullptr);
    if (!window) {
        return 1;
    }
    g_runtime->main_window = window;
    store_runtime_settings(settings);
    if (!g_smoke_mode && !register_screen_ocr_hotkey(window)) {
        offline_translator::app_log_warn(
            "не удалось зарегистрировать горячую клавишу перевода области " +
            std::string(kScreenOcrHotkeySpec));
    }
    if (!g_smoke_mode && settings.excel_translation &&
        settings.excel_trigger == offline_translator::kExcelTriggerHotkey &&
        !register_excel_hotkey(window, settings.excel_hotkey)) {
        offline_translator::app_log_warn(
            "не удалось зарегистрировать горячую клавишу Excel " +
            settings.excel_hotkey);
    }
    Gdiplus::GdiplusStartupInput gdiplus_input{};
    ULONG_PTR gdiplus_token = 0;
    const bool gdiplus_ready =
        Gdiplus::GdiplusStartup(&gdiplus_token, &gdiplus_input, nullptr) ==
        Gdiplus::Ok;
    if (command_has_flag(command_line, L"--smoke-popup") ||
        command_has_flag(GetCommandLineW(), L"--smoke-popup")) {
        const int result = run_popup_smoke_loop(window);
        if (gdiplus_ready) {
            Gdiplus::GdiplusShutdown(gdiplus_token);
        }
        return result;
    }
    if (g_smoke_mode) {
        if (gdiplus_ready) {
            Gdiplus::GdiplusShutdown(gdiplus_token);
        }
        return run_smoke_loop(window);
    }
    add_tray_icon(window);
    g_app_start_tick = GetTickCount64();
    g_last_poll_tick = g_app_start_tick;
    offline_translator::app_log_info(
        "запуск pid=" + std::to_string(GetCurrentProcessId()) +
        " engine=" + settings.engine +
        " theme=" + settings.ui_theme +
        " hotkey=" + settings.translate_hotkey +
        " modifier=" + settings.popup_modifier +
        " log=" + to_utf8(offline_translator::app_log_path().wstring()));
    start_watchdog(window);
    if (g_start_minimized) {
        hide_to_tray(window);
    } else {
        // Запуск через CreateProcess может прийти с nCmdShow=0 (SW_HIDE):
        // окно обязано появиться, как при обычном двойном клике.
        ShowWindow(window, show_command == 0 ? SW_SHOWNORMAL : show_command);
        UpdateWindow(window);
    }
    if (!g_smoke_mode && settings.check_updates_on_startup) {
        // Тихая проверка обновлений через несколько секунд после запуска.
        SetTimer(window, kStartupUpdateTimer, 4000, nullptr);
    }
    apply_dark_title_bar(window);
    // Плавающую кнопку создаём после старта GDI+ (иначе иконка не
    // загрузится и включится запасной вариант «Aa»).
    if (g_runtime && g_runtime->settings.image_float_button) {
        show_float_button(true, false);
    }

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (gdiplus_ready) {
        Gdiplus::GdiplusShutdown(gdiplus_token);
    }
    return static_cast<int>(message.wParam);
}
