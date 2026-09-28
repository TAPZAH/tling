#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace offline_translator {

// Язык интерфейса приложения. Ключи перевода — исходные русские строки
// (tr(L"Настройки")); словари лежат в src/i18n/*.inc.
struct UiLanguageOption {
    const char* code;
    const wchar_t* label;  // самоназвание: Русский, English, Deutsch...
};

// Поддерживаемые языки интерфейса (код и подпись).
const std::vector<UiLanguageOption>& ui_language_options();

// Нормализует код: "auto"/"" -> язык системы; неизвестный -> язык системы.
std::string normalize_ui_language(std::string_view code);

// Язык интерфейса системы (ru/en/de/fr/es/uk, иначе en).
std::string detect_ui_language();

// Текущий язык интерфейса (по умолчанию — язык системы).
std::string current_ui_language();
void set_ui_language(std::string_view code);

// Перевод русской строки. Русский возвращает исходную строку,
// неизвестные ключи — исходный текст.
std::wstring tr(std::wstring_view russian);

}  // namespace offline_translator
