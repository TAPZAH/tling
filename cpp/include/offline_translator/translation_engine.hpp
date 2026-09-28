#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace offline_translator {

struct TranslationResult {
    std::string text;
    std::optional<std::string> intermediate;
    std::optional<std::string> pivot_code;
};

using LanguagePair = std::pair<std::string, std::string>;
using IsInstalled = std::function<bool(std::string_view, std::string_view)>;
using DirectTranslate = std::function<std::string(
    std::string_view,
    std::string_view,
    std::string_view)>;

class TranslationEngine {
public:
    virtual ~TranslationEngine() = default;

    virtual TranslationResult translate(
        std::string_view text,
        std::string_view source_code,
        std::string_view target_code) = 0;

    virtual void warmup(
        std::string_view source_code,
        std::string_view target_code) = 0;

    virtual void invalidate() = 0;
    virtual void stop() = 0;

    virtual std::optional<std::string> translation_route(
        std::string_view source_code,
        std::string_view target_code) const = 0;
};

}  // пространство имён offline_translator
