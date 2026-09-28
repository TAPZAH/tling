#pragma once

#include "offline_translator/translation_engine.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace offline_translator {

enum class EngineKind {
    argos,
    nllb,
    firefox,
    marian,
};

class TranslationApplication {
public:
    TranslationApplication(
        EngineKind engine_kind,
        std::filesystem::path models_root);
    // Вариант движка: для firefox — размер модели (tiny/base).
    TranslationApplication(
        EngineKind engine_kind,
        std::filesystem::path models_root,
        std::string engine_variant);
    ~TranslationApplication();

    TranslationApplication(const TranslationApplication&) = delete;
    TranslationApplication& operator=(const TranslationApplication&) = delete;

    TranslationResult translate(
        std::string_view text,
        std::string_view source_code,
        std::string_view target_code);
    void warmup(std::string_view source_code, std::string_view target_code);
    void invalidate();
    void stop();
    // Сначала выгружает движок, затем удаляет пакет, чтобы файлы не были заняты.
    void uninstall_package(
        std::string_view source_code = {},
        std::string_view target_code = {});

    EngineKind engine_kind() const;
    const std::filesystem::path& models_root() const;

    static std::string engine_name(EngineKind engine_kind);

private:
    class State;
    std::unique_ptr<State> state_;
};

// Долгоживущая сессия: одно приложение на движок, корень моделей и вариант.
class TranslationSession {
public:
    TranslationApplication& acquire(
        EngineKind engine_kind,
        const std::filesystem::path& models_root,
        const std::string& engine_variant = {});
    void reset();
    TranslationApplication* get() noexcept;
    const TranslationApplication* get() const noexcept;
    bool is_loaded() const noexcept;
    void mark_loaded() noexcept;

private:
    std::unique_ptr<TranslationApplication> application_;
    EngineKind engine_kind_{EngineKind::argos};
    std::filesystem::path models_root_;
    std::string engine_variant_;
    bool loaded_{false};
};

}  // пространство имён offline_translator
