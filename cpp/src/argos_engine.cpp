#include "offline_translator/argos_engine.hpp"

#include "offline_translator/argos_model_manager.hpp"
#include "offline_translator/ctranslate2_engine.hpp"
#include "offline_translator/route_planner.hpp"
#include "offline_translator/sentencepiece_tokenizer.hpp"

#include <stdexcept>
#include <utility>

namespace offline_translator {

class ArgosEngine::PairState {
public:
    PairState(
        const std::filesystem::path& packages_root,
        std::string source_code,
        std::string target_code) {
        ArgosModelManager model_manager(
            packages_root, source_code, target_code);
        model_manager.validate();
        tokenizer = std::make_unique<SentencePieceTokenizer>(
            model_manager.tokenizer_path());
        auto* tokenizer_ptr = tokenizer.get();
        engine = std::make_unique<CTranslate2Engine>(
            model_manager.model_path().string(),
            [source_code, target_code](
                std::string_view installed_source,
                std::string_view installed_target) {
                return installed_source == source_code &&
                       installed_target == target_code;
            },
            [tokenizer_ptr](std::string_view text) {
                return tokenizer_ptr->tokenize(text);
            },
            [tokenizer_ptr](const std::vector<std::string>& pieces) {
                return tokenizer_ptr->detokenize(pieces);
            });
    }

    std::unique_ptr<SentencePieceTokenizer> tokenizer;
    std::unique_ptr<CTranslate2Engine> engine;
};

ArgosEngine::ArgosEngine(std::filesystem::path packages_root)
    : packages_root_(std::move(packages_root)) {
    if (packages_root_.empty()) {
        throw std::invalid_argument("Не задан корень пакетов Argos");
    }
}

ArgosEngine::~ArgosEngine() {
    stop();
}

TranslationResult ArgosEngine::translate(
    std::string_view text,
    std::string_view source_code,
    std::string_view target_code) {
    if (stopped_) {
        throw std::runtime_error("Движок Argos остановлен");
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
        "Argos");
}

void ArgosEngine::warmup(
    std::string_view source_code,
    std::string_view target_code) {
    static_cast<void>(translate("Hello", source_code, target_code));
}

void ArgosEngine::invalidate() {
    if (!stopped_) {
        pairs_.clear();
    }
}

void ArgosEngine::stop() {
    if (!stopped_) {
        stopped_ = true;
        pairs_.clear();
    }
}

std::optional<std::string> ArgosEngine::translation_route(
    std::string_view source_code,
    std::string_view target_code) const {
    return english_pivot_route(
        [this](std::string_view source, std::string_view target) {
            return is_installed(source, target);
        },
        source_code,
        target_code);
}

bool ArgosEngine::is_installed(
    std::string_view source_code,
    std::string_view target_code) const {
    return ArgosModelManager(
               packages_root_,
               std::string(source_code),
               std::string(target_code))
        .is_installed();
}

std::string ArgosEngine::translate_direct(
    std::string_view text,
    std::string_view source_code,
    std::string_view target_code) {
    auto& pair_state = get_pair_state(source_code, target_code);
    return pair_state.engine->translate(text, source_code, target_code).text;
}

ArgosEngine::PairState& ArgosEngine::get_pair_state(
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
        packages_root_, key.first, key.second);
    auto* state_ptr = state.get();
    pairs_.emplace(key, std::move(state));
    return *state_ptr;
}

}  // пространство имён offline_translator
