#include "offline_translator/firefox_engine.hpp"

#include "offline_translator/firefox_model_manager.hpp"
#include "offline_translator/route_planner.hpp"

#include <mutex>
#include <stdexcept>
#include <utility>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace offline_translator {
namespace {

// Загруженный fxbridge.dll: функции C-ABI моста.
struct FxBridge {
    void* (*engine_create)(
        const char* model,
        const char* src_vocab,
        const char* trg_vocab,
        const char* shortlist) = nullptr;
    char* (*translate)(void* engine, const char* text) = nullptr;
    char* (*translate_long)(void* engine, const char* text) = nullptr;
    void (*string_free)(char* value) = nullptr;
    char* (*backend)() = nullptr;
    void (*engine_destroy)(void* engine) = nullptr;
    char* (*last_error)() = nullptr;

    bool valid() const {
        return engine_create && translate && translate_long &&
               string_free && engine_destroy;
    }
};

FxBridge& bridge() {
    static FxBridge instance = [] {
        FxBridge loaded{};
#ifdef _WIN32
        HMODULE module = ::LoadLibraryW(L"fxbridge.dll");
        if (!module) {
            return loaded;
        }
        auto resolve = [module](const char* name) {
            return reinterpret_cast<void*>(::GetProcAddress(module, name));
        };
        loaded.engine_create =
            reinterpret_cast<void* (*)(
                const char*, const char*, const char*, const char*)>(
                resolve("fxt_engine_create"));
        loaded.translate =
            reinterpret_cast<char* (*)(void*, const char*)>(
                resolve("fxt_translate"));
        loaded.translate_long =
            reinterpret_cast<char* (*)(void*, const char*)>(
                resolve("fxt_translate_long"));
        loaded.string_free =
            reinterpret_cast<void (*)(char*)>(resolve("fxt_string_free"));
        loaded.backend =
            reinterpret_cast<char* (*)()>(resolve("fxt_backend"));
        loaded.last_error =
            reinterpret_cast<char* (*)()>(resolve("fxt_last_error"));
        loaded.engine_destroy =
            reinterpret_cast<void (*)(void*)>(resolve("fxt_engine_destroy"));
#endif
        return loaded;
    }();
    return instance;
}

const FxBridge& require_bridge() {
    const auto& loaded = bridge();
    if (!loaded.valid()) {
        throw std::runtime_error(
            "Не найден fxbridge.dll (движок Firefox Translations). "
            "Переустановите программу.");
    }
    return loaded;
}

std::string read_bridge_string(char* (*function)()) {
    if (!function) {
        return {};
    }
    char* value = function();
    if (!value) {
        return {};
    }
    std::string result(value);
    require_bridge().string_free(value);
    return result;
}

// Диагностика из моста: последняя ошибка на текущем потоке.
std::string bridge_last_error() {
    const auto& loaded = bridge();
    if (!loaded.last_error) {
        return {};
    }
    char* value = loaded.last_error();
    if (!value) {
        return {};
    }
    std::string result(value);
    loaded.string_free(value);
    return result;
}

}  // namespace

struct FirefoxEngine::PairState {
    void* engine{nullptr};
    std::string backend;

    PairState(
        const std::filesystem::path& models_root,
        const std::string& architecture,
        const std::string& source_code,
        const std::string& target_code) {
        const auto& loaded = require_bridge();
        FirefoxModelManager manager(
            models_root, architecture, source_code, target_code);
        const auto model_path = manager.resolve_model_path();
        if (!model_path) {
            throw std::runtime_error(
                "Нет установленной модели " + source_code + " -> " +
                target_code);
        }
        auto vocab_path = *model_path / "vocab.spm";
        if (!std::filesystem::is_regular_file(vocab_path)) {
            vocab_path = *model_path / "srcvocab.spm";
        }
        if (!std::filesystem::is_regular_file(vocab_path)) {
            throw std::runtime_error(
                "В пакете нет словаря: " + model_path->string());
        }
        auto trg_vocab_path = *model_path / "vocab.spm";
        if (!std::filesystem::is_regular_file(trg_vocab_path)) {
            trg_vocab_path = *model_path / "trgvocab.spm";
        }
        const auto shortlist_path = *model_path / "lex.bin";
        const bool has_shortlist =
            std::filesystem::is_regular_file(shortlist_path);
        const auto model_file = *model_path / "model.bin";
        if (!std::filesystem::is_regular_file(model_file)) {
            throw std::runtime_error(
                "В пакете нет файла модели: " + model_file.string());
        }
        engine = loaded.engine_create(
            model_file.string().c_str(),
            vocab_path.string().c_str(),
            trg_vocab_path.string().c_str(),
            has_shortlist ? shortlist_path.string().c_str() : nullptr);
        if (!engine) {
            const auto details = bridge_last_error();
            throw std::runtime_error(
                "Не удалось создать движок Firefox для " + source_code +
                " -> " + target_code +
                (details.empty() ? std::string{} : ": " + details));
        }
        backend = read_bridge_string(loaded.backend);
    }

    ~PairState() {
        if (engine) {
            // Мост может быть уже выгружен при завершении процесса —
            // проверяем указатель перед вызовом.
            if (bridge().valid()) {
                bridge().engine_destroy(engine);
            }
            engine = nullptr;
        }
    }
};

FirefoxEngine::FirefoxEngine(
    std::filesystem::path models_root,
    std::string architecture)
    : models_root_(std::move(models_root)),
      architecture_(std::move(architecture)) {
    if (!FirefoxModelManager::is_architecture(architecture_)) {
        throw std::invalid_argument("Неизвестный размер модели Firefox");
    }
}

FirefoxEngine::~FirefoxEngine() {
    stop();
}

TranslationResult FirefoxEngine::translate(
    std::string_view text,
    std::string_view source_code,
    std::string_view target_code) {
    if (stopped_) {
        throw std::runtime_error("Движок Firefox остановлен");
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
        "Firefox");
}

void FirefoxEngine::warmup(
    std::string_view source_code,
    std::string_view target_code) {
    static_cast<void>(translate("Hello", source_code, target_code));
}

void FirefoxEngine::invalidate() {
    if (!stopped_) {
        pairs_.clear();
    }
}

void FirefoxEngine::stop() {
    if (!stopped_) {
        stopped_ = true;
        pairs_.clear();
    }
}

std::optional<std::string> FirefoxEngine::translation_route(
    std::string_view source_code,
    std::string_view target_code) const {
    return english_pivot_route(
        [this](std::string_view source, std::string_view target) {
            return is_installed(source, target);
        },
        source_code,
        target_code);
}

bool FirefoxEngine::is_installed(
    std::string_view source_code,
    std::string_view target_code) const {
    return FirefoxModelManager(
               models_root_,
               architecture_,
               std::string(source_code),
               std::string(target_code))
        .is_installed();
}

std::string FirefoxEngine::translate_direct(
    std::string_view text,
    std::string_view source_code,
    std::string_view target_code) {
    auto& pair_state = get_pair_state(source_code, target_code);
    const auto& loaded = require_bridge();
    char* raw = loaded.translate_long(
        pair_state.engine,
        std::string(text).c_str());
    if (!raw) {
        throw std::runtime_error("Движок Firefox вернул пустой ответ");
    }
    std::string translated(raw);
    loaded.string_free(raw);
    while (!translated.empty() &&
           (translated.back() == ' ' || translated.back() == '\n')) {
        translated.pop_back();
    }
    if (translated.empty()) {
        throw std::runtime_error(
            "Пустой ответ переводчика для пары " +
            std::string(source_code) + " → " + std::string(target_code) +
            " (модель повреждена — переустановите её в окне «Пакеты»)");
    }
    return translated;
}

FirefoxEngine::PairState& FirefoxEngine::get_pair_state(
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
        models_root_, architecture_, key.first, key.second);
    auto* state_ptr = state.get();
    pairs_.emplace(key, std::move(state));
    return *state_ptr;
}

}  // namespace offline_translator
