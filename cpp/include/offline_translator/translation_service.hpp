#pragma once

#include "offline_translator/route_planner.hpp"

#include <condition_variable>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <string_view>
#include <thread>

namespace offline_translator {

class TranslationService {
public:
    TranslationService(
        IsInstalled is_installed,
        DirectTranslate translate_direct,
        std::string engine_title = {});
    ~TranslationService();

    TranslationService(const TranslationService&) = delete;
    TranslationService& operator=(const TranslationService&) = delete;

    TranslationResult translate(
        std::string_view text,
        std::string_view source_code,
        std::string_view target_code);
    void warmup(std::string_view source_code, std::string_view target_code);
    void invalidate();
    void stop();

private:
    using Task = std::packaged_task<void()>;

    void worker_loop(std::stop_token stop_token);
    void enqueue(Task task);

    IsInstalled is_installed_;
    DirectTranslate translate_direct_;
    std::string engine_title_;
    std::mutex mutex_;
    std::condition_variable_any condition_;
    std::queue<Task> tasks_;
    std::jthread worker_;
    bool stopped_{false};
};

}  // пространство имён offline_translator
