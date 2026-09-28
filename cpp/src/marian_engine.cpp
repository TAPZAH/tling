#include "offline_translator/marian_engine.hpp"

#include "offline_translator/ctranslate2_engine.hpp"
#include "offline_translator/marian_model_manager.hpp"
#include "offline_translator/route_planner.hpp"
#include "offline_translator/sentencepiece_tokenizer.hpp"

#include <stdexcept>
#include <utility>

namespace offline_translator {

class MarianEngine::PairState {
public:
    PairState(
        const std::filesystem::path& models_root,
        std::string source_code,
        std::string target_code) {
        MarianModelManager model_manager(
            models_root, source_code, target_code);
        model_manager.validate();
        source_tokenizer = std::make_unique<SentencePieceTokenizer>(
            model_manager.source_tokenizer_path());
        target_tokenizer = std::make_unique<SentencePieceTokenizer>(
            model_manager.target_tokenizer_path());
        auto* source_ptr = source_tokenizer.get();
        auto* target_ptr = target_tokenizer.get();
        engine = std::make_unique<CTranslate2Engine>(
            model_manager.model_path().string(),
            [source_code, target_code](
                std::string_view installed_source,
                std::string_view installed_target) {
                return installed_source == source_code &&
                       installed_target == target_code;
            },
            [source_ptr](std::string_view text) {
                auto tokens = source_ptr->tokenize(text);
                if (tokens.empty() || tokens.back() != "</s>") {
                    tokens.emplace_back("</s>");
                }
                return tokens;
            },
            [target_ptr](const std::vector<std::string>& pieces) {
                auto cleaned = pieces;
                while (!cleaned.empty() &&
                       (cleaned.back() == "</s>" || cleaned.back() == "<s>")) {
                    cleaned.pop_back();
                }
                if (!cleaned.empty() && cleaned.front() == "</s>") {
                    cleaned.erase(cleaned.begin());
                }
                if (cleaned.empty()) {
                    return std::string{};
                }
                return target_ptr->detokenize(cleaned);
            });
    }

    std::unique_ptr<SentencePieceTokenizer> source_tokenizer;
    std::unique_ptr<SentencePieceTokenizer> target_tokenizer;
    std::unique_ptr<CTranslate2Engine> engine;
};

MarianEngine::MarianEngine(std::filesystem::path models_root)
    : models_root_(std::move(models_root)) {
    if (models_root_.empty()) {
        throw std::invalid_argument("Не задан корень моделей MarianMT");
    }
}

MarianEngine::~MarianEngine() {
    stop();
}

TranslationResult MarianEngine::translate(
    std::string_view text,
    std::string_view source_code,
    std::string_view target_code) {
    if (stopped_) {
        throw std::runtime_error("Движок MarianMT остановлен");
    }
    return translate_with_english_pivot(
        text,
        source_code,
        target_code,
        [this](std::string_view source, std::string_view target) {
            return is_installed(source, target);
        },
        [this](
            std::string_view direct_text,
            std::string_view direct_source,
            std::string_view direct_target) {
            return translate_direct(direct_text, direct_source, direct_target);
        },
        "MarianMT");
}

void MarianEngine::warmup(
    std::string_view source_code,
    std::string_view target_code) {
    static_cast<void>(translate("Hello", source_code, target_code));
}

void MarianEngine::invalidate() {
    if (!stopped_) {
        pairs_.clear();
    }
}

void MarianEngine::stop() {
    if (!stopped_) {
        stopped_ = true;
        pairs_.clear();
    }
}

std::optional<std::string> MarianEngine::translation_route(
    std::string_view source_code,
    std::string_view target_code) const {
    return english_pivot_route(
        [this](std::string_view source, std::string_view target) {
            return is_installed(source, target);
        },
        source_code,
        target_code);
}

bool MarianEngine::is_installed(
    std::string_view source_code,
    std::string_view target_code) const {
    return MarianModelManager(
               models_root_,
               std::string(source_code),
               std::string(target_code))
        .is_installed();
}

std::string MarianEngine::translate_direct(
    std::string_view text,
    std::string_view source_code,
    std::string_view target_code) {
    auto& pair_state = get_pair_state(source_code, target_code);
    return pair_state.engine->translate(text, source_code, target_code).text;
}

MarianEngine::PairState& MarianEngine::get_pair_state(
    std::string_view source_code,
    std::string_view target_code) {
    const LanguagePair key{
        std::string(source_code),
        std::string(target_code),
    };
    const auto found = pairs_.find(key);
    if (found != pairs_.end()) {
        return *found->second;
    }
    auto state = std::make_unique<PairState>(
        models_root_, key.first, key.second);
    auto* state_ptr = state.get();
    pairs_.emplace(key, std::move(state));
    return *state_ptr;
}

}  // пространство имён offline_translator
