#include "offline_translator/translation_service.hpp"

#include <stdexcept>
#include <utility>

namespace offline_translator {

TranslationService::TranslationService(
    IsInstalled is_installed,
    DirectTranslate translate_direct,
    std::string engine_title)
    : is_installed_(std::move(is_installed)),
      translate_direct_(std::move(translate_direct)),
      engine_title_(std::move(engine_title)),
      worker_([this](std::stop_token stop_token) { worker_loop(stop_token); }) {}

TranslationService::~TranslationService() {
    stop();
}

TranslationResult TranslationService::translate(
    std::string_view text,
    std::string_view source_code,
    std::string_view target_code) {
    auto result = std::make_shared<std::promise<TranslationResult>>();
    auto future = result->get_future();
    const std::string text_copy(text);
    const std::string source_copy(source_code);
    const std::string target_copy(target_code);

    enqueue(Task([this, result, text_copy, source_copy, target_copy]() {
        try {
            result->set_value(translate_with_english_pivot(
                text_copy,
                source_copy,
                target_copy,
                is_installed_,
                translate_direct_,
                engine_title_));
        } catch (...) {
            result->set_exception(std::current_exception());
        }
    }));
    return future.get();
}

void TranslationService::warmup(
    std::string_view source_code,
    std::string_view target_code) {
    const std::string source_copy(source_code);
    const std::string target_copy(target_code);
    auto result = std::make_shared<std::promise<void>>();
    auto future = result->get_future();

    enqueue(Task([this, result, source_copy, target_copy]() {
        try {
            static_cast<void>(translate_with_english_pivot(
                "Hello",
                source_copy,
                target_copy,
                is_installed_,
                translate_direct_,
                engine_title_));
            result->set_value();
        } catch (...) {
            result->set_exception(std::current_exception());
        }
    }));
    future.get();
}

void TranslationService::invalidate() {
    auto result = std::make_shared<std::promise<void>>();
    auto future = result->get_future();
    enqueue(Task([result]() { result->set_value(); }));
    future.get();
}

void TranslationService::stop() {
    {
        std::lock_guard lock(mutex_);
        if (stopped_) {
            return;
        }
        stopped_ = true;
    }
    worker_.request_stop();
    condition_.notify_all();
}

void TranslationService::worker_loop(std::stop_token stop_token) {
    while (true) {
        Task task;
        {
            std::unique_lock lock(mutex_);
            condition_.wait(lock, stop_token, [this] {
                return !tasks_.empty() || stopped_;
            });
            if (tasks_.empty()) {
                return;
            }
            task = std::move(tasks_.front());
            tasks_.pop();
        }
        task();
    }
}

void TranslationService::enqueue(Task task) {
    {
        std::lock_guard lock(mutex_);
        if (stopped_) {
            throw std::runtime_error("Сервис перевода остановлен");
        }
        tasks_.push(std::move(task));
    }
    condition_.notify_one();
}

}  // пространство имён offline_translator
