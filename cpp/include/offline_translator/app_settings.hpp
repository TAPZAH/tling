#pragma once

#include "offline_translator/translation_application.hpp"

#include <filesystem>
#include <string>
#include <string_view>

namespace offline_translator {

// Настройки главного окна. Файл — JSON, совместимый с Python
// (`engine` и прочие ключи сохраняются при записи).
// Путь по умолчанию:
//   %OFFLINE_TRANSLATOR_HOME%/settings.json
//   или <каталог exe>/data/settings.json  (portable, если есть папка data)
//   иначе %USERPROFILE%/.local/share/offline-translator/settings.json
struct AppSettings {
    std::string engine{"argos"};
    std::string source_language{"en"};
    std::string target_language{"ru"};
    // Язык перевода, когда «С языка» = Авто и определённый язык совпал с
    // выбранным «На язык» (иначе получилось бы translate в тот же язык).
    std::string fallback_target_language{"en"};
    int window_width{0};
    int window_height{0};
    bool popup_requires_ctrl{false};
    // none | ctrl | alt | shift — клавиша удержания для кнопки перевода.
    std::string popup_modifier{"none"};
    bool double_ctrl_c_translation{false};
    // false — кнопка при выделении не показывается, перевод только Ctrl+C+C.
    bool selection_popup_enabled{true};
    std::string result_window_mode{"click_to_close"};
    std::string translate_hotkey{"Ctrl+Shift+T"};
    // Размер моделей Firefox Translations: tiny | base (как в Python).
    std::string architecture{"tiny"};
    // Язык интерфейса: auto | ru | en | de | fr | es | uk.
    std::string ui_language{"auto"};
    // light | dark — оформление окна перевода, меню и иконок.
    std::string ui_theme{"light"};
    bool auto_copy_selection{false};
    // Турбо-перевод: переводить выделенный текст сразу, без кнопки у курсора.
    bool turbo_translation{false};
    // Ключи онлайн-переводчиков (пусто — не задан).
    std::string google_api_key;
    // Проверять обновления на GitHub при запуске (тихо, если их нет).
    bool check_updates_on_startup{true};
    int clipboard_history_limit{10};
    // Разделитель кнопки «Добавить»: оригинал + разделитель + перевод.
    std::string append_separator{" / "};
    // Excel: перевод выделенных ячеек.
    bool excel_translation{true};
    // hotkey | double_ctrl_c — чем вызывается перевод ячеек.
    std::string excel_trigger{"hotkey"};
    std::string excel_hotkey{"Ctrl+Alt+T"};
    // clipboard | com | auto — как читать/писать ячейки Excel.
    std::string excel_write_method{"clipboard"};
    // Плавающая кнопка перевода изображений: файл перетаскивается на неё.
    bool image_float_button{false};
    int image_float_button_x{-1};
    int image_float_button_y{-1};
    // auto | tesseract | paddle — движок распознавания текста (OCR).
    std::string ocr_engine{"auto"};
    // Только OCR: вместо перевода показывать распознанный оригинал.
    bool ocr_only{false};
};

inline constexpr std::string_view kUiThemeLight = "light";
inline constexpr std::string_view kUiThemeDark = "dark";
inline constexpr std::string_view kAppendSeparatorSlash = " / ";
inline constexpr std::string_view kAppendSeparatorDash = " - ";
inline constexpr std::string_view kExcelWriteClipboard = "clipboard";
inline constexpr std::string_view kExcelWriteCom = "com";
inline constexpr std::string_view kExcelWriteAuto = "auto";
inline constexpr std::string_view kExcelTriggerHotkey = "hotkey";
inline constexpr std::string_view kExcelTriggerDoubleCtrlC = "double_ctrl_c";
inline constexpr std::string_view kOcrEngineAuto = "auto";
inline constexpr std::string_view kOcrEngineTesseract = "tesseract";
inline constexpr std::string_view kOcrEnginePaddle = "paddle";

std::filesystem::path default_data_root();
std::filesystem::path default_settings_path();

AppSettings load_settings();
AppSettings load_settings(const std::filesystem::path& path);
void save_settings(const AppSettings& settings);
void save_settings(const std::filesystem::path& path, const AppSettings& settings);

EngineKind engine_kind_from_settings(std::string_view engine);
std::string settings_engine_name(EngineKind engine_kind);
std::string normalize_ui_theme(std::string_view theme);
std::string normalize_append_separator(std::string_view separator);
std::string normalize_excel_write_method(std::string_view method);
std::string normalize_excel_trigger(std::string_view trigger);
std::string normalize_ocr_engine(std::string_view engine);

}  // пространство имён offline_translator
