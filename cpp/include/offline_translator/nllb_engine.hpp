#pragma once

#include "offline_translator/translation_engine.hpp"

#include <filesystem>
#include <memory>
#include <string>

namespace offline_translator {

class NllbEngine final : public TranslationEngine {
public:
    explicit NllbEngine(std::filesystem::path models_root);
    ~NllbEngine() override;

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
    class State;

    void ensure_loaded();

    std::filesystem::path models_root_;
    std::unique_ptr<State> state_;
    bool stopped_{false};
};

}  // пространство имён offline_translator
