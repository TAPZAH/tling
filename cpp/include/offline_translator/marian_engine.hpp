#pragma once

#include "offline_translator/translation_engine.hpp"

#include <filesystem>
#include <map>
#include <memory>
#include <string>

namespace offline_translator {

class MarianEngine final : public TranslationEngine {
public:
    explicit MarianEngine(std::filesystem::path models_root);
    ~MarianEngine() override;

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
    class PairState;

    bool is_installed(std::string_view source_code, std::string_view target_code)
        const;
    std::string translate_direct(
        std::string_view text,
        std::string_view source_code,
        std::string_view target_code);
    PairState& get_pair_state(
        std::string_view source_code,
        std::string_view target_code);

    std::filesystem::path models_root_;
    std::map<LanguagePair, std::unique_ptr<PairState>> pairs_;
    bool stopped_{false};
};

}  // пространство имён offline_translator
