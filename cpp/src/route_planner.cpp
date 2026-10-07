#include "offline_translator/route_planner.hpp"

#include <cctype>
#include <stdexcept>
#include <string>

namespace offline_translator {
namespace {

std::string trim(std::string_view value) {
    std::size_t first = 0;
    while (first < value.size() &&
           std::isspace(static_cast<unsigned char>(value[first])) != 0) {
        ++first;
    }

    std::size_t last = value.size();
    while (last > first &&
           std::isspace(static_cast<unsigned char>(value[last - 1])) != 0) {
        --last;
    }
    return std::string(value.substr(first, last - first));
}

std::string pair_name(std::string_view source_code, std::string_view target_code) {
    return std::string(source_code) + " → " + std::string(target_code);
}

}  // анонимное пространство имён

std::optional<std::string> english_pivot_route(
    const IsInstalled& is_installed,
    std::string_view source_code,
    std::string_view target_code) {
    if (source_code == target_code) {
        return std::nullopt;
    }
    if (is_installed(source_code, target_code)) {
        return "direct";
    }
    if (source_code != "en" &&
        target_code != "en" &&
        is_installed(source_code, "en") &&
        is_installed("en", target_code)) {
        return "en";
    }
    return std::nullopt;
}

std::vector<LanguagePair> needed_english_pivot_pairs(
    std::string_view source_code,
    std::string_view target_code,
    const IsInstalled& is_installed) {
    if (source_code == target_code || is_installed(source_code, target_code)) {
        return {};
    }
    if (source_code == "en" || target_code == "en") {
        return {{std::string(source_code), std::string(target_code)}};
    }

    std::vector<LanguagePair> needed;
    if (!is_installed(source_code, "en")) {
        needed.emplace_back(std::string(source_code), "en");
    }
    if (!is_installed("en", target_code)) {
        needed.emplace_back("en", std::string(target_code));
    }
    return needed;
}

TranslationResult translate_with_english_pivot(
    std::string_view text,
    std::string_view source_code,
    std::string_view target_code,
    const IsInstalled& is_installed,
    const DirectTranslate& translate_direct,
    std::string_view engine_title) {
    const std::string source_text = trim(text);
    if (source_text.empty()) {
        return {std::string(text), std::nullopt, std::nullopt};
    }

    if (is_installed(source_code, target_code)) {
        return {
            translate_direct(source_text, source_code, target_code),
            std::nullopt,
            std::nullopt,
        };
    }

    if (source_code != "en" &&
        target_code != "en" &&
        is_installed(source_code, "en") &&
        is_installed("en", target_code)) {
        const std::string intermediate =
            translate_direct(source_text, source_code, "en");
        return {
            translate_direct(intermediate, "en", target_code),
            intermediate,
            "en",
        };
    }

    const auto missing =
        needed_english_pivot_pairs(source_code, target_code, is_installed);
    const std::string prefix =
        engine_title.empty() ? std::string{} : std::string(engine_title) + " ";
    if (!missing.empty()) {
        std::string message =
            "Нет модели " + prefix + pair_name(source_code, target_code) +
            ". " + std::string(kMissingModelMarker) + " ";
        for (std::size_t index = 0; index < missing.size(); ++index) {
            if (index != 0) {
                message += ", ";
            }
            message += missing[index].first + "->" + missing[index].second;
        }
        throw std::runtime_error(message);
    }
    throw std::runtime_error(
        "Нет модели " + prefix + pair_name(source_code, target_code));
}

}  // пространство имён offline_translator
