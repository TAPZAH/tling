#pragma once

#include "offline_translator/translation_engine.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace offline_translator {

// Маркер в тексте ошибки: для пары нет установленной модели. По нему
// интерфейс предлагает открыть окно «Пакеты» и скачать её.
inline constexpr std::string_view kMissingModelMarker = "Установите:";

std::optional<std::string> english_pivot_route(
    const IsInstalled& is_installed,
    std::string_view source_code,
    std::string_view target_code);

std::vector<LanguagePair> needed_english_pivot_pairs(
    std::string_view source_code,
    std::string_view target_code,
    const IsInstalled& is_installed);

TranslationResult translate_with_english_pivot(
    std::string_view text,
    std::string_view source_code,
    std::string_view target_code,
    const IsInstalled& is_installed,
    const DirectTranslate& translate_direct,
    std::string_view engine_title = {});

}  // пространство имён offline_translator
