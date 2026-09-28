#include "offline_translator/nllb_engine.hpp"

#include "offline_translator/ctranslate2_engine.hpp"
#include "offline_translator/nllb_model_manager.hpp"
#include "offline_translator/sentencepiece_tokenizer.hpp"

#include <stdexcept>
#include <utility>

namespace offline_translator {

class NllbEngine::State {
public:
    explicit State(const std::filesystem::path& models_root) {
        NllbModelManager model_manager(models_root);
        model_manager.validate();
        tokenizer = std::make_unique<SentencePieceTokenizer>(
            model_manager.model_path() / "sentencepiece.bpe.model");
        auto* tokenizer_ptr = tokenizer.get();
        engine = std::make_unique<CTranslate2Engine>(
            model_manager.model_path().string(),
            [](std::string_view, std::string_view) { return true; },
            [tokenizer_ptr](std::string_view text) {
                return tokenizer_ptr->tokenize(text);
            },
            [tokenizer_ptr](const std::vector<std::string>& pieces) {
                return tokenizer_ptr->detokenize(pieces);
            },
            true);
    }

    std::unique_ptr<SentencePieceTokenizer> tokenizer;
    std::unique_ptr<CTranslate2Engine> engine;
};

NllbEngine::NllbEngine(std::filesystem::path models_root)
    : models_root_(std::move(models_root)) {
    if (models_root_.empty()) {
        throw std::invalid_argument("Не задан корень моделей NLLB");
    }
}

NllbEngine::~NllbEngine() {
    stop();
}

TranslationResult NllbEngine::translate(
    std::string_view text,
    std::string_view source_code,
    std::string_view target_code) {
    if (stopped_) {
        throw std::runtime_error("Движок NLLB остановлен");
    }
    ensure_loaded();
    return state_->engine->translate(text, source_code, target_code);
}

void NllbEngine::warmup(
    std::string_view source_code,
    std::string_view target_code) {
    static_cast<void>(translate("Hello", source_code, target_code));
}

void NllbEngine::invalidate() {
    if (!stopped_) {
        state_.reset();
    }
}

void NllbEngine::stop() {
    if (!stopped_) {
        stopped_ = true;
        state_.reset();
    }
}

std::optional<std::string> NllbEngine::translation_route(
    std::string_view source_code,
    std::string_view target_code) const {
    if (source_code == target_code) {
        return std::nullopt;
    }
    return "direct";
}

void NllbEngine::ensure_loaded() {
    if (!state_) {
        state_ = std::make_unique<State>(models_root_);
    }
}

}  // пространство имён offline_translator
