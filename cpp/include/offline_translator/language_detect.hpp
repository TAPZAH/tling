#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace offline_translator {

// Определение языка текста через Rust-мост fxbridge (whatlang).
// allowed — языки, среди которых выбирать (обычно те, для которых есть
// модели); пустой список снимает ограничение. Возвращает ISO 639-1 код или
// пустую строку, если определить не удалось (тогда вызывающий использует
// определение по письму). Модуль не бросает исключений.
std::string detect_language_code(
    std::string_view text,
    const std::vector<std::string>& allowed);

// Доступен ли детектор (загрузился ли fxbridge.dll с нужной функцией).
bool language_detector_available();

}  // namespace offline_translator
