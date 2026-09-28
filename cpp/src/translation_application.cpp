#include "offline_translator/translation_application.hpp"

#include "offline_translator/argos_engine.hpp"
#include "offline_translator/argos_model_manager.hpp"
#include "offline_translator/firefox_engine.hpp"
#include "offline_translator/firefox_model_manager.hpp"
#include "offline_translator/marian_engine.hpp"
#include "offline_translator/marian_model_manager.hpp"
#include "offline_translator/nllb_engine.hpp"
#include "offline_translator/nllb_model_manager.hpp"
#include "offline_translator/translation_service.hpp"

#include <stdexcept>
#include <utility>

namespace offline_translator {

class TranslationApplication::State {
public:
    State(EngineKind kind, std::filesystem::path root, std::string variant)
        : engine_kind(kind),
          models_root(std::move(root)),
          engine_variant(std::move(variant)) {
        if (models_root.empty()) {
            throw std::invalid_argument("Не задан корень моделей");
        }
        if (engine_kind == EngineKind::argos) {
            engine = std::make_unique<ArgosEngine>(models_root);
        } else if (engine_kind == EngineKind::nllb) {
            engine = std::make_unique<NllbEngine>(models_root);
        } else if (engine_kind == EngineKind::firefox) {
            engine = std::make_unique<FirefoxEngine>(
                models_root,
                engine_variant.empty() ? std::string{"tiny"} : engine_variant);
        } else if (engine_kind == EngineKind::marian) {
            engine = std::make_unique<MarianEngine>(models_root);
        } else {
            throw std::invalid_argument("Неизвестный тип движка");
        }

        service = std::make_unique<TranslationService>(
            [this](std::string_view source, std::string_view target) {
                if (engine_kind == EngineKind::argos) {
                    return ArgosModelManager(
                               models_root,
                               std::string(source),
                               std::string(target))
                        .is_installed();
                }
                if (engine_kind == EngineKind::firefox) {
                    return FirefoxModelManager(
                               models_root,
                               engine_variant.empty() ? std::string{"tiny"}
                                                      : engine_variant,
                               std::string(source),
                               std::string(target))
                        .is_installed();
                }
                if (engine_kind == EngineKind::marian) {
                    return MarianModelManager(
                               models_root,
                               std::string(source),
                               std::string(target))
                        .is_installed();
                }
                return NllbModelManager(models_root).is_installed() &&
                       source != target;
            },
            [this](
                std::string_view text,
                std::string_view source,
                std::string_view target) {
                return engine->translate(text, source, target).text;
            },
            TranslationApplication::engine_name(engine_kind));
    }

    void stop() {
        service->stop();
        engine->stop();
    }

    EngineKind engine_kind;
    std::filesystem::path models_root;
    std::string engine_variant;
    std::unique_ptr<TranslationEngine> engine;
    std::unique_ptr<TranslationService> service;
};

TranslationApplication::TranslationApplication(
    EngineKind engine_kind,
    std::filesystem::path models_root)
    : TranslationApplication(
          engine_kind,
          std::move(models_root),
          std::string{}) {}

TranslationApplication::TranslationApplication(
    EngineKind engine_kind,
    std::filesystem::path models_root,
    std::string engine_variant)
    : state_(std::make_unique<State>(
          engine_kind,
          std::move(models_root),
          std::move(engine_variant))) {}

TranslationApplication::~TranslationApplication() {
    if (state_) {
        state_->stop();
    }
}

TranslationResult TranslationApplication::translate(
    std::string_view text,
    std::string_view source_code,
    std::string_view target_code) {
    if (!state_) {
        throw std::runtime_error("Приложение перевода не инициализировано");
    }
    return state_->service->translate(text, source_code, target_code);
}

void TranslationApplication::warmup(
    std::string_view source_code,
    std::string_view target_code) {
    state_->service->warmup(source_code, target_code);
}

void TranslationApplication::invalidate() {
    if (state_) {
        state_->service->invalidate();
        state_->engine->invalidate();
    }
}

void TranslationApplication::stop() {
    if (state_) {
        state_->stop();
    }
}

void TranslationApplication::uninstall_package(
    std::string_view source_code,
    std::string_view target_code) {
    if (!state_) {
        throw std::runtime_error("Приложение перевода не инициализировано");
    }
    invalidate();
    if (state_->engine_kind == EngineKind::nllb) {
        NllbModelManager(state_->models_root).uninstall();
        return;
    }
    if (state_->engine_kind == EngineKind::firefox) {
        if (source_code.empty() || target_code.empty()) {
            throw std::invalid_argument(
                "Для удаления пакета Firefox нужны коды языков");
        }
        FirefoxModelManager(
            state_->models_root,
            state_->engine_variant.empty() ? std::string{"tiny"}
                                           : state_->engine_variant,
            std::string(source_code),
            std::string(target_code))
            .uninstall();
        return;
    }
    if (state_->engine_kind == EngineKind::marian) {
        if (source_code.empty() || target_code.empty()) {
            throw std::invalid_argument(
                "Для удаления пакета MarianMT нужны коды языков");
        }
        MarianModelManager(
            state_->models_root,
            std::string(source_code),
            std::string(target_code))
            .uninstall();
        return;
    }
    if (source_code.empty() || target_code.empty()) {
        throw std::invalid_argument(
            "Для удаления пакета Argos нужны коды языков");
    }
    ArgosModelManager(
        state_->models_root,
        std::string(source_code),
        std::string(target_code))
        .uninstall();
}

std::string TranslationApplication::engine_name(EngineKind engine_kind) {
    if (engine_kind == EngineKind::argos) {
        return "Argos";
    }
    if (engine_kind == EngineKind::nllb) {
        return "NLLB";
    }
    if (engine_kind == EngineKind::firefox) {
        return "Firefox";
    }
    if (engine_kind == EngineKind::marian) {
        return "MarianMT";
    }
    throw std::invalid_argument("Неизвестный тип движка");
}

EngineKind TranslationApplication::engine_kind() const {
    if (!state_) {
        throw std::runtime_error("Приложение перевода не инициализировано");
    }
    return state_->engine_kind;
}

const std::filesystem::path& TranslationApplication::models_root() const {
    if (!state_) {
        throw std::runtime_error("Приложение перевода не инициализировано");
    }
    return state_->models_root;
}

TranslationApplication& TranslationSession::acquire(
    EngineKind engine_kind,
    const std::filesystem::path& models_root,
    const std::string& engine_variant) {
    if (application_ && engine_kind_ == engine_kind &&
        models_root_ == models_root && engine_variant_ == engine_variant) {
        return *application_;
    }
    application_.reset();
    loaded_ = false;
    application_ =
        std::make_unique<TranslationApplication>(
            engine_kind, models_root, engine_variant);
    engine_kind_ = engine_kind;
    models_root_ = models_root;
    engine_variant_ = engine_variant;
    return *application_;
}

void TranslationSession::reset() {
    application_.reset();
    loaded_ = false;
    models_root_.clear();
    engine_variant_.clear();
}

TranslationApplication* TranslationSession::get() noexcept {
    return application_.get();
}

const TranslationApplication* TranslationSession::get() const noexcept {
    return application_.get();
}

bool TranslationSession::is_loaded() const noexcept {
    return loaded_ && application_ != nullptr;
}

void TranslationSession::mark_loaded() noexcept {
    loaded_ = application_ != nullptr;
}

}  // пространство имён offline_translator
