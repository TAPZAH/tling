#include "offline_translator/ctranslate2_engine.hpp"

#include "offline_translator/app_log.hpp"
#include "offline_translator/nllb_language.hpp"
#include "offline_translator/route_planner.hpp"
#include "offline_translator/text_split.hpp"

#include <ctranslate2/translator.h>

#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace offline_translator {

class CTranslate2State {
public:
    CTranslate2State(const std::string& model_path, bool nllb_model) {
        // Как в Python: NLLB использует int8. На сборках CTranslate2 без
        // эффективного int8 (OpenBLAS без oneDNN) откатываемся на AUTO.
        if (nllb_model) {
            try {
                translator = create(model_path, ctranslate2::ComputeType::INT8);
                return;
            } catch (const std::exception&) {
            }
        }
        translator = create(model_path, ctranslate2::ComputeType::AUTO);
    }

    std::unique_ptr<ctranslate2::Translator> translator;

private:
    static std::unique_ptr<ctranslate2::Translator> create(
        const std::string& model_path,
        ctranslate2::ComputeType compute_type) {
        // Потоки не задаём: 0 = как в Python (intra_threads по умолчанию).
        // На OpenMP+oneDNN это omp_set_num_threads(auto); на noomp-сборке
        // ручная раскладка по всем ядрам только усиливает конфликт пулов.
        return std::make_unique<ctranslate2::Translator>(
            model_path,
            ctranslate2::Device::CPU,
            compute_type,
            std::vector<int>{0},
            false);
    }
};

CTranslate2Engine::CTranslate2Engine(
    std::string model_path,
    IsInstalled is_installed,
    Tokenize tokenize,
    Detokenize detokenize,
    bool nllb_model)
    : model_path_(std::move(model_path)),
      is_installed_(std::move(is_installed)),
      tokenize_(std::move(tokenize)),
      detokenize_(std::move(detokenize)),
      nllb_model_(nllb_model) {
    if (!is_installed_ || !tokenize_ || !detokenize_) {
        throw std::invalid_argument("Неполный набор callback-ов CTranslate2");
    }
}

CTranslate2Engine::~CTranslate2Engine() {
    stop();
}

TranslationResult CTranslate2Engine::translate(
    std::string_view text,
    std::string_view source_code,
    std::string_view target_code) {
    if (stopped_) {
        throw std::runtime_error("Движок CTranslate2 остановлен");
    }
    return translate_with_english_pivot(
        text,
        source_code,
        target_code,
        is_installed_,
        [this](std::string_view direct_text,
               std::string_view direct_source,
               std::string_view direct_target) {
            return translate_direct(direct_text, direct_source, direct_target);
        },
        "CTranslate2");
}

void CTranslate2Engine::warmup(
    std::string_view source_code,
    std::string_view target_code) {
    static_cast<void>(translate("Hello", source_code, target_code));
}

void CTranslate2Engine::invalidate() {
    if (!stopped_) {
        state_.reset();
    }
}

void CTranslate2Engine::stop() {
    if (stopped_) {
        return;
    }
    stopped_ = true;
    state_.reset();
}

std::optional<std::string> CTranslate2Engine::translation_route(
    std::string_view source_code,
    std::string_view target_code) const {
    return english_pivot_route(is_installed_, source_code, target_code);
}

void CTranslate2Engine::ensure_loaded() {
    if (!state_) {
        app_log_info(
            std::string("загрузка CTranslate2 ") +
            (nllb_model_ ? "nllb " : "argos ") + model_path_);
        const auto started = std::chrono::steady_clock::now();
        state_ = std::make_unique<CTranslate2State>(model_path_, nllb_model_);
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - started)
                            .count();
        app_log_info("модель CTranslate2 загружена ms=" + std::to_string(ms));
    }
}

std::string CTranslate2Engine::translate_direct(
    std::string_view text,
    std::string_view source_code,
    std::string_view target_code) {
    ensure_loaded();

    const std::string nllb_source =
        nllb_model_ ? nllb_language_code(source_code) : std::string(source_code);
    const std::string nllb_target =
        nllb_model_ ? nllb_language_code(target_code) : std::string(target_code);

    std::vector<std::string> pieces;
    if (nllb_model_) {
        pieces.emplace_back(std::string(text));
    } else {
        pieces = split_sentences(text);
        if (pieces.empty()) {
            pieces.emplace_back(std::string(text));
        }
    }

    std::vector<std::vector<std::string>> batch;
    std::vector<std::vector<std::string>> prefixes;
    batch.reserve(pieces.size());
    for (const auto& piece : pieces) {
        auto tokens = tokenize_(piece);
        if (tokens.empty()) {
            continue;
        }
        if (nllb_model_) {
            tokens.insert(tokens.begin(), nllb_source);
            tokens.emplace_back("</s>");
            prefixes.push_back({nllb_target});
        }
        batch.push_back(std::move(tokens));
    }
    if (batch.empty()) {
        throw std::runtime_error("Токенизатор вернул пустой результат");
    }

    // Как в Python: Argos и NLLB — beam_size=2; у NLLB ещё max_decoding_length=512.
    ctranslate2::TranslationOptions options;
    options.beam_size = 2;
    options.replace_unknowns = true;
    if (nllb_model_) {
        options.max_decoding_length = 512;
    }
    const auto results = state_->translator->translate_batch(
        batch,
        prefixes.empty() ? std::vector<std::vector<std::string>>{} : prefixes,
        options);
    if (results.size() != batch.size()) {
        throw std::runtime_error("CTranslate2 вернул неожиданное число гипотез");
    }

    std::vector<std::string> parts;
    parts.reserve(results.size());
    for (const auto& result : results) {
        if (result.hypotheses.empty()) {
            throw std::runtime_error("CTranslate2 вернул пустой результат");
        }
        auto hypothesis = result.hypotheses.front();
        if (nllb_model_) {
            hypothesis.erase(
                std::remove(
                    hypothesis.begin(),
                    hypothesis.end(),
                    nllb_target),
                hypothesis.end());
            while (!hypothesis.empty() &&
                   (hypothesis.back() == "</s>" || hypothesis.back() == "<s>")) {
                hypothesis.pop_back();
            }
        }
        auto decoded = detokenize_(hypothesis);
        while (!decoded.empty() &&
               (decoded.back() == ' ' || decoded.back() == '\n')) {
            decoded.pop_back();
        }
        if (!decoded.empty()) {
            parts.push_back(std::move(decoded));
        }
    }
    std::string translated;
    for (std::size_t index = 0; index < parts.size(); ++index) {
        if (index > 0) {
            translated.push_back(' ');
        }
        translated += parts[index];
    }
    if (translated.empty()) {
        throw std::runtime_error("Детокенизатор вернул пустой результат");
    }
    return translated;
}

}  // пространство имён offline_translator
