#pragma once

#include "offline_translator/translation_engine.hpp"

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace offline_translator {

using Tokenize = std::function<std::vector<std::string>(std::string_view)>;
using Detokenize = std::function<std::string(const std::vector<std::string>&)>;

class CTranslate2Engine final : public TranslationEngine {
public:
    CTranslate2Engine(
        std::string model_path,
        IsInstalled is_installed,
        Tokenize tokenize,
        Detokenize detokenize,
        bool nllb_model = false);
    ~CTranslate2Engine() override;

    TranslationResult translate(
        std::string_view text,
        std::string_view source_code,
        std::string_view target_code) override;
    void warmup(std::string_view source_code, std::string_view target_code) override;
    void invalidate() override;
    void stop() override;

    std::optional<std::string> translation_route(
        std::string_view source_code,
        std::string_view target_code) const override;

private:
    void ensure_loaded();
    std::string translate_direct(
        std::string_view text,
        std::string_view source_code,
        std::string_view target_code);

    std::string model_path_;
    IsInstalled is_installed_;
    Tokenize tokenize_;
    Detokenize detokenize_;
    bool nllb_model_{false};
    std::unique_ptr<class CTranslate2State> state_;
    bool stopped_{false};
};

}  // пространство имён offline_translator
