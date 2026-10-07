#include "offline_translator/app_log.hpp"
#include "offline_translator/app_language.hpp"
#include "offline_translator/app_settings.hpp"
#include "offline_translator/app_update.hpp"
#include "offline_translator/app_version.hpp"
#include "offline_translator/argos_model_manager.hpp"
#include "offline_translator/autostart.hpp"
#include "offline_translator/clipboard.hpp"
#include "offline_translator/clipboard_history.hpp"
#include "offline_translator/firefox_model_manager.hpp"
#include "offline_translator/marian_model_manager.hpp"
#include "offline_translator/hotkey.hpp"
#include "offline_translator/language_store.hpp"
#include "offline_translator/nllb_model_manager.hpp"
#include "offline_translator/route_planner.hpp"
#include "offline_translator/selection.hpp"
#include "offline_translator/translation_application.hpp"
#include "offline_translator/window_policy.hpp"

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
constexpr UINT kUpdateCheckDoneMessage = WM_APP + 21;
constexpr UINT kUpdateInstallDoneMessage = WM_APP + 22;
constexpr UINT kSelectionPollTimer = 1;
constexpr UINT kSelectionButtonHideTimer = 2;
constexpr int kEngineCombo = 1001;
constexpr int kSourceEdit = 1002;
constexpr int kTranslateButton = 1003;
constexpr int kResultEdit = 1004;
constexpr int kSourceLanguageCombo = 1005;
constexpr int kTargetLanguageCombo = 1006;
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
constexpr int kSettingsHotkeyEdit = 1206;
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
constexpr int kResultCopyButton = 1302;
constexpr int kResultCloseButton = 1303;
constexpr int kResultReplaceButton = 1304;
constexpr int kShowWindowHotkey = 2001;
constexpr int kTrayOpen = 3001;
constexpr int kTrayAutostart = 3002;
constexpr int kTrayExit = 3003;
constexpr int kTrayCheckUpdate = 3004;
constexpr int kTrayAutoCopy = 3005;
constexpr int kTrayTurbo = 3006;
constexpr int kTrayHistoryRoot = 3097;
constexpr int kTrayHistoryEmpty = 3098;
constexpr int kTrayHistoryClear = 3099;
constexpr int kTrayHistoryFirst = 3100;
constexpr int kTrayHistoryLast = 3149;
constexpr int kTrayHistoryTranslateFirst = 3200;
constexpr int kTrayHistoryTranslateLast = 3249;
constexpr int kTrayHistoryPreviewFirst = 3300;
constexpr int kDefaultWindowWidth = 560;
constexpr int kDefaultWindowHeight = 450;
constexpr int kMinWindowWidth = 520;
constexpr int kMinWindowHeight = 400;

HWND g_engine_combo = nullptr;
HWND g_source_edit = nullptr;
HWND g_translate_button = nullptr;
HWND g_result_edit = nullptr;
HWND g_source_language_combo = nullptr;
HWND g_target_language_combo = nullptr;
HWND g_packages_button = nullptr;
HWND g_settings_button = nullptr;
HWND g_status_label = nullptr;
HWND g_package_list = nullptr;
HWND g_package_install = nullptr;
HWND g_package_uninstall = nullptr;
HWND g_package_status = nullptr;
HWND g_package_search = nullptr;
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
HWND g_settings_hotkey_edit = nullptr;
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
std::atomic<bool> g_update_busy{false};
std::optional<offline_translator::UpdateInfo> g_pending_update;
HWND g_selection_button = nullptr;
HWND g_result_popup = nullptr;
HWND g_popup_result_edit = nullptr;
HWND g_popup_copy_button = nullptr;
HWND g_popup_replace_button = nullptr;
HWND g_popup_header = nullptr;
std::wstring g_last_clipboard_error;
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
        case offline_translator::EngineKind::argos:
            break;
    }
    return model_root_path(false);
}

std::wstring clipboard_text() {
    offline_translator::Win32Clipboard clipboard;
    return clipboard.get_text();
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

bool key_down(int virtual_key) {
    return (GetAsyncKeyState(virtual_key) & 0x8000) != 0;
}

std::size_t language_count() {
    return offline_translator::supported_languages().size();
}

const std::vector<std::wstring>& combo_language_names() {
    static const std::vector<std::wstring> names = [] {
        std::vector<std::wstring> result;
        result.reserve(offline_translator::supported_languages().size());
        for (const auto& entry : offline_translator::supported_languages()) {
            result.push_back(from_utf8(entry.name));
        }
        return result;
    }();
    return names;
}

int language_index(std::string_view code) {
    const auto& languages = offline_translator::supported_languages();
    for (std::size_t index = 0; index < languages.size(); ++index) {
        if (languages[index].code == code) {
            return static_cast<int>(index);
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
        static_cast<std::size_t>(index)].code;
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
    return offline_translator::EngineKind::argos;
}

int engine_combo_index(offline_translator::EngineKind kind) {
    switch (kind) {
        case offline_translator::EngineKind::nllb:
            return 1;
        case offline_translator::EngineKind::firefox:
            return 2;
        case offline_translator::EngineKind::marian:
            return 3;
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
    select_language(g_source_language_combo, settings.source_language, 0);
    select_language(
        g_target_language_combo,
        settings.target_language,
        language_count() > 1 ? 1 : 0);
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
    settings.source_language = selected_language(g_source_language_combo);
    settings.target_language = selected_language(g_target_language_combo);
    collect_window_size(window, settings);
    return settings;
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
    EnableWindow(g_packages_button, !busy);
}

void layout_main(HWND window) {
    RECT client{};
    GetClientRect(window, &client);
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;
    const int content_width = width - 32;
    const int status_top = height - 28;
    const int result_top = 228;
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
            84,
            content_width,
            90,
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

void start_translation(HWND window) {
    if (!g_runtime || g_runtime->busy.exchange(true)) {
        offline_translator::app_log_warn(
            "перевод окна пропущен: уже выполняется " + runtime_flags());
        return;
    }
    const std::wstring source_text = control_text(g_source_edit);
    const auto engine_kind = selected_engine();
    const std::string source_language = selected_language(
        g_source_language_combo);
    const std::string target_language = selected_language(
        g_target_language_combo);
    const std::string text = to_utf8(source_text);
    offline_translator::app_log_info(
        "перевод окна старт chars=" + std::to_string(text.size()) + " " +
        source_language + "→" + target_language + " " + runtime_flags());
    const auto root = model_root_for_kind(engine_kind);
    const std::string engine_variant =
        engine_kind == offline_translator::EngineKind::firefox
            ? (g_runtime ? g_runtime->settings.architecture : std::string{"tiny"})
            : std::string{};
    persist_settings(window);
    set_main_busy(true);
    set_status(L"Подготовка перевода...");
    auto runtime = g_runtime;
    std::thread(
        [window, text, root, engine_kind, engine_variant, source_language, target_language, runtime]() {
            auto result = std::make_unique<StatusPayload>();
            const auto started = GetTickCount64();
            try {
                std::lock_guard lock(runtime->mutex);
                if (runtime->closing) {
                    runtime->busy = false;
                    return;
                }
                auto& application =
                    runtime->session.acquire(engine_kind, root, engine_variant);
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
            RGB(0, 0, 0),
            RGB(255, 255, 255),
            RGB(255, 255, 255),
            RGB(255, 255, 255),
            RGB(42, 42, 42),
            RGB(61, 61, 61),
            RGB(255, 255, 255),
        };
    }
    return UiTheme{
        RGB(255, 255, 255),
        RGB(0, 0, 0),
        RGB(0, 0, 0),
        RGB(0, 0, 0),
        RGB(230, 230, 230),
        RGB(208, 208, 208),
        RGB(0, 0, 0),
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

BOOL CALLBACK apply_theme_to_child(HWND child, LPARAM) {
    SetWindowTheme(child, L"", L"");
    return TRUE;
}

void apply_dark_title_bar(HWND window) {
    if (!window || !IsWindow(window)) {
        return;
    }
    // Тёмный заголовок окна в тёмной теме (Windows 10 1809+).
    const BOOL dark = is_dark_theme() ? TRUE : FALSE;
    if (FAILED(DwmSetWindowAttribute(
            window, 20, &dark, sizeof(dark)))) {
        DwmSetWindowAttribute(window, 19, &dark, sizeof(dark));
    }
}

void apply_theme_to_window(HWND window) {
    if (!window || !IsWindow(window)) {
        return;
    }
    SetWindowTheme(window, L"", L"");
    EnumChildWindows(window, apply_theme_to_child, 0);
    apply_dark_title_bar(window);
    InvalidateRect(window, nullptr, TRUE);
    RedrawWindow(
        window,
        nullptr,
        nullptr,
        RDW_ERASE | RDW_INVALIDATE | RDW_ALLCHILDREN);
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

// Готовит 40×40 premultiplied-DIB из тематической PNG для
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
    }
}

bool register_translate_hotkey(HWND window, const std::string& spec) {
    UnregisterHotKey(window, kShowWindowHotkey);
    const auto parsed = offline_translator::parse_hotkey(spec);
    if (!parsed) {
        return false;
    }
    return RegisterHotKey(
               window,
               kShowWindowHotkey,
               offline_translator::hotkey_win32_modifiers(*parsed),
               parsed->vk) != FALSE;
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
    UINT autostart_flags = MF_OWNERDRAW;
    try {
        if (offline_translator::is_app_autostart_enabled()) {
            autostart_flags |= MF_CHECKED;
        }
    } catch (const std::exception&) {
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
        autostart_flags,
        kTrayAutostart,
        reinterpret_cast<LPCWSTR>(static_cast<UINT_PTR>(kTrayAutostart)));
    AppendMenuW(
        menu,
        MF_OWNERDRAW,
        kTrayCheckUpdate,
        reinterpret_cast<LPCWSTR>(static_cast<UINT_PTR>(kTrayCheckUpdate)));
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

void toggle_app_autostart() {
    try {
        const bool enabled = !offline_translator::is_app_autostart_enabled();
        offline_translator::set_app_autostart(enabled);
        set_status(
            enabled
                ? L"Программа будет запускаться вместе с Windows"
                : L"Программа убрана из автозагрузки");
    } catch (const std::exception& error) {
        set_status(from_utf8(error.what()));
    }
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
    if (!g_runtime) {
        return;
    }
    if (!register_translate_hotkey(window, g_runtime->settings.translate_hotkey)) {
        offline_translator::app_log_error(
            "не удалось заново зарегистрировать горячую клавишу " +
            g_runtime->settings.translate_hotkey);
    }
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

void show_result_popup(
    HWND main_window,
    const std::wstring& text,
    const std::string& source_code,
    const std::string& target_code) {
    hide_selection_button();
    hide_result_popup();
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

    const std::wstring header = from_utf8(
        offline_translator::language_display_name(source_code) + " → " +
        offline_translator::language_display_name(target_code));
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
    const bool show_replace = text.rfind(L"Ошибка:", 0) != 0;
    g_popup_replace_button = nullptr;
    int next_left = copy_x;
    if (show_replace) {
        next_left = copy_x - 88 - 8;
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
                reinterpret_cast<uintptr_t>(g_popup_replace_button)));
    }
    if (!selectable) {
        // Иначе всё поле выделено и Copy может взять чужое выделение.
        SendMessageW(g_popup_result_edit, EM_SETSEL, static_cast<WPARAM>(-1), 0);
    }
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
    const auto direction = offline_translator::choose_selection_direction(text);
    offline_translator::app_log_info(
        "перевод выделения старт chars=" + std::to_string(text.size()) + " " +
        direction.first + "→" + direction.second + " " + runtime_flags());
    const auto engine_kind = selected_engine();
    const auto root = model_root_for_kind(engine_kind);
    const std::string engine_variant =
        engine_kind == offline_translator::EngineKind::firefox
            ? (g_runtime ? g_runtime->settings.architecture : std::string{"tiny"})
            : std::string{};
    auto runtime = g_runtime;
    std::thread([main_window, text, direction, engine_kind, engine_variant, root, runtime]() {
        auto result = std::make_unique<StatusPayload>();
        const auto started = GetTickCount64();
        try {
            std::lock_guard lock(runtime->mutex);
            if (runtime->closing) {
                runtime->selection_busy = false;
                return;
            }
            auto& application =
                runtime->session.acquire(engine_kind, root, engine_variant);
            result->text = from_utf8(
                application.translate(text, direction.first, direction.second)
                    .text);
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

void handle_translate_hotkey(HWND window) {
    offline_translator::app_log_info("горячая клавиша перевода " + runtime_flags());
    for (int step = 0; step < 25; ++step) {
        if (!key_down(VK_CONTROL) && !key_down(VK_SHIFT) &&
            !key_down(VK_MENU) && !key_down(VK_LWIN) && !key_down(VK_RWIN)) {
            break;
        }
        Sleep(20);
    }
    std::wstring previous;
    try {
        previous = clipboard_text();
    } catch (const std::exception&) {
    }
    std::wstring selected;
    try {
        selected = offline_translator::capture_selected_text_win32(
            window,
            g_runtime && g_runtime->settings.auto_copy_selection);
    } catch (const std::exception&) {
    }
    if (selected.size() < 2) {
        selected = previous;
    }
    if (selected.size() < 2) {
        offline_translator::app_log_warn("горячая клавиша: нет выделенного текста");
        set_status(L"Нет выделенного текста");
        return;
    }
    g_selection.selected_text = selected;
    g_selection.replace_hwnd = GetForegroundWindow();
    if (g_runtime && g_runtime->settings.auto_copy_selection) {
        try {
            offline_translator::add_clipboard_history_item(to_utf8(selected));
        } catch (const std::exception&) {
        }
    }
    if (IsWindowVisible(window) && g_source_edit) {
        SetWindowTextW(g_source_edit, selected.c_str());
    }
    start_selection_translation(window);
}

void close_settings_window();
void start_update_check();
void start_update_install();
void open_releases_page();
void open_settings_window(HWND parent, HINSTANCE instance, bool check_updates = false);
void open_packages_window(HWND parent, HINSTANCE instance);

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
            over_button(g_popup_replace_button);
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
    const std::string hotkey = to_utf8(control_text(g_settings_hotkey_edit));
    if (!offline_translator::parse_hotkey(hotkey)) {
        localized_message_box(
            settings_window,
            L"Некорректная горячая клавиша. Пример: Ctrl+Shift+T",
            L"Настройки",
            MB_ICONWARNING | MB_OK);
        return;
    }
    settings.translate_hotkey =
        offline_translator::format_hotkey(*offline_translator::parse_hotkey(hotkey));
    settings.ui_theme =
        g_settings_theme_dark &&
        SendMessageW(g_settings_theme_dark, BM_GETCHECK, 0, 0) == BST_CHECKED
            ? std::string{offline_translator::kUiThemeDark}
            : std::string{offline_translator::kUiThemeLight};
    if (g_runtime->main_window && g_engine_combo) {
        settings.engine =
            offline_translator::settings_engine_name(selected_engine());
    }
    try {
        // selected_language() бросает исключение, если в комбобоксе нет
        // выбранного пункта, поэтому читать языки нужно внутри try.
        if (g_runtime->main_window && g_engine_combo) {
            settings.source_language =
                selected_language(g_source_language_combo);
            settings.target_language =
                selected_language(g_target_language_combo);
            collect_window_size(g_runtime->main_window, settings);
        }
        if (!g_smoke_mode) {
            offline_translator::save_settings(settings);
            const bool autostart =
                SendMessageW(g_settings_autostart, BM_GETCHECK, 0, 0) ==
                BST_CHECKED;
            offline_translator::set_app_autostart(autostart);
            if (g_runtime->main_window &&
                !register_translate_hotkey(
                    g_runtime->main_window,
                    settings.translate_hotkey)) {
                localized_message_box(
                    settings_window,
                    L"Не удалось зарегистрировать горячую клавишу.",
                    L"Настройки",
                    MB_ICONWARNING | MB_OK);
            }
        }
        store_runtime_settings(settings);
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
    g_settings_hotkey_edit = nullptr;
    g_settings_theme_light = nullptr;
    g_settings_theme_dark = nullptr;
    g_settings_auto_copy = nullptr;
    g_settings_history_limit = nullptr;
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
    if (message == WM_CTLCOLORSTATIC || message == WM_CTLCOLOREDIT) {
        const HDC dc = reinterpret_cast<HDC>(w_param);
        const UiTheme theme = current_ui_theme();
        SetBkColor(dc, theme.background);
        SetTextColor(dc, theme.text);
        return reinterpret_cast<LRESULT>(theme_background_brush());
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
            CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,
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
            BS_PUSHBUTTON | WS_TABSTOP,
            16,
            128,
            180,
            28,
            kSettingsPackagesButton);
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
            CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,
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
            L"Хранить последние копирования (1–50):",
            0,
            16,
            528,
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
            524,
            48,
            24,
            kSettingsHistoryLimit);
        content(
            kSettingsPageBehavior,
            L"STATIC",
            L"Горячая клавиша перевода выделения:",
            0,
            16,
            560,
            300,
            18);
        g_settings_hotkey_edit = content(
            kSettingsPageBehavior,
            L"EDIT",
            from_utf8(settings.translate_hotkey).c_str(),
            WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP,
            16,
            582,
            240,
            24,
            kSettingsHotkeyEdit);
        HWND launch_title = content(
            kSettingsPageBehavior,
            L"STATIC",
            L"Запуск",
            0,
            16,
            620,
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
            650,
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
            CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,
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
            BS_PUSHBUTTON | WS_TABSTOP,
            16,
            202,
            180,
            28,
            kSettingsCheckUpdate);
        g_settings_install_update = content(
            kSettingsPageAbout,
            L"BUTTON",
            L"Скачать и установить",
            BS_PUSHBUTTON | WS_DISABLED | WS_TABSTOP,
            204,
            202,
            170,
            28,
            kSettingsInstallUpdate);
        g_settings_open_releases = content(
            kSettingsPageAbout,
            L"BUTTON",
            L"Страница релизов",
            BS_PUSHBUTTON | WS_TABSTOP,
            16,
            238,
            160,
            28,
            kSettingsOpenReleases);
        // ===== Кнопки фиксированной нижней панели =====
        g_settings_save = CreateWindowW(
            L"BUTTON",
            L"Сохранить",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON | WS_TABSTOP,
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
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON | WS_TABSTOP,
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
                return DefWindowProcW(window, message, w_param, l_param);
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
        WS_VISIBLE | WS_CHILD | CBS_DROPDOWNLIST | WS_TABSTOP,
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
        WS_VISIBLE | WS_CHILD | CBS_DROPDOWNLIST | WS_TABSTOP,
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
        WS_VISIBLE | WS_CHILD | CBS_DROPDOWNLIST | WS_TABSTOP,
        335,
        48,
        130,
        420,
        window,
        reinterpret_cast<HMENU>(
            static_cast<INT_PTR>(kTargetLanguageCombo)),
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
    if (g_packages_button) {
        SetWindowSubclass(g_packages_button, flat_button_subclass, 2, 0);
    }
    if (g_settings_button) {
        SetWindowSubclass(g_settings_button, flat_button_subclass, 2, 0);
    }
    if (g_translate_button) {
        SetWindowSubclass(g_translate_button, flat_button_subclass, 2, 0);
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
                WS_CHILD | CBS_DROPDOWNLIST | WS_TABSTOP,
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
                WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON | WS_TABSTOP,
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
                WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON | WS_TABSTOP,
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
                WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON | WS_TABSTOP,
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

void open_packages_window(HWND parent, HINSTANCE instance) {
    if (g_runtime && g_runtime->packages_window) {
        SetForegroundWindow(g_runtime->packages_window);
        return;
    }
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
                    RECT mark = draw->rcItem;
                    mark.right = mark.left + 22;
                    DrawTextW(
                        draw->hDC,
                        L"✓",
                        -1,
                        &mark,
                        DT_CENTER | DT_VCENTER | DT_SINGLELINE);
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
        if (message == WM_COMMAND && LOWORD(w_param) == kTrayAutostart) {
            if (!g_smoke_mode) {
                toggle_app_autostart();
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
        if (message == WM_COMMAND &&
            (LOWORD(w_param) == kEngineCombo ||
             LOWORD(w_param) == kSourceLanguageCombo ||
             LOWORD(w_param) == kTargetLanguageCombo) &&
            HIWORD(w_param) == CBN_SELCHANGE) {
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
                            L"Выбран Argos. Пакет загрузится при переводе.");
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
                        L"Для выбранной пары нет установленной модели.\n\n"
                        L"Открыть окно «Пакеты», чтобы скачать её?",
                        L"Нет модели",
                        MB_ICONQUESTION | MB_YESNO);
                    if (answer == IDYES) {
                        try {
                            open_packages_window(
                                window, window_instance(window));
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
            const auto direction = offline_translator::choose_selection_direction(
                to_utf8(g_selection.selected_text));
            show_result_popup(
                window,
                result->text,
                direction.first,
                direction.second);
            if (result->failed) {
                set_status(L"Ошибка перевода выделения");
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
        if (message == WM_HOTKEY &&
            w_param == static_cast<WPARAM>(kShowWindowHotkey)) {
            handle_translate_hotkey(window);
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
            UnregisterHotKey(window, kShowWindowHotkey);
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
        !g_packages_button || !g_settings_button) {
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
    show_result_popup(window, L"Проверка перевода", "ru", "en");
    if (!IsWindow(g_result_popup) || !g_popup_result_edit ||
        !g_popup_copy_button) {
        return fail("попап перевода не создан");
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
    DestroyWindow(window);
    return 0;
}

}  // анонимное пространство имён

int WINAPI wWinMain(
    HINSTANCE instance,
    HINSTANCE,
    PWSTR command_line,
    int show_command) {
    g_smoke_mode = command_has_flag(command_line, L"--smoke-start") ||
        command_has_flag(GetCommandLineW(), L"--smoke-start");
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
    if (!g_smoke_mode) {
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

    const auto settings = offline_translator::load_settings();
    offline_translator::set_ui_language(settings.ui_language);
    offline_translator::app_log_info(
        "настройки: engine=" + settings.engine +
        " lang=" + settings.ui_language +
        " modifier=" + settings.popup_modifier +
        " popup=" + std::to_string(settings.selection_popup_enabled) +
        " auto_copy=" + std::to_string(settings.auto_copy_selection) +
        " result=" + settings.result_window_mode +
        " theme=" + settings.ui_theme);
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
    if (!g_smoke_mode &&
        !register_translate_hotkey(window, settings.translate_hotkey)) {
        offline_translator::app_log_error(
            "не удалось зарегистрировать горячую клавишу " +
            settings.translate_hotkey);
        const std::wstring warning =
            L"Не удалось зарегистрировать горячую клавишу " +
            from_utf8(settings.translate_hotkey) + L".";
        localized_message_box(
            window,
            warning.c_str(),
            L"Предупреждение",
            MB_ICONWARNING | MB_OK);
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
