#pragma once

#include "offline_translator/translation_engine.hpp"

#include <string>
#include <string_view>

namespace offline_translator {

enum class OnlineProvider { google, yandex };

// Онлайн-перевод через сервисы Google и Яндекс. Ключ API не обязателен для
// Google (есть запасной бесплатный эндпоинт) и обязателен для Яндекса:
// без ключа сервис отвечает «Invalid parameter: key».
class OnlineEngine : public TranslationEngine {
public:
    OnlineEngine(OnlineProvider provider, std::string api_key);

    TranslationResult translate(
        std::string_view text,
        std::string_view source_code,
        std::string_view target_code) override;
    void warmup(std::string_view, std::string_view) override {}
    void invalidate() override {}
    void stop() override {}
    std::optional<std::string> translation_route(
        std::string_view,
        std::string_view) const override {
        return "direct";
    }

private:
    OnlineProvider provider_;
    std::string api_key_;
};

// Сборка URL и разбор ответов вынесены отдельно, чтобы их можно было
// проверить тестами без сети.
std::string url_encode(std::string_view text);
std::string online_language_code(
    OnlineProvider provider,
    std::string_view code);
std::string build_google_translate_url(
    std::string_view source_code,
    std::string_view target_code,
    std::string_view text,
    std::string_view api_key);
std::string build_yandex_translate_url(
    std::string_view source_code,
    std::string_view target_code,
    std::string_view text,
    std::string_view api_key);
std::string parse_google_translation(std::string_view json_text);
std::string parse_yandex_translation(std::string_view json_text);

}  // namespace offline_translator
