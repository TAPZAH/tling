#include "offline_translator/app_settings.hpp"
#include "offline_translator/argos_model_manager.hpp"
#include "offline_translator/nllb_model_manager.hpp"
#include "offline_translator/window_policy.hpp"
#include "fs_utils.hpp"

#ifdef ENABLE_CTRANSLATE2
#include "offline_translator/translation_application.hpp"
#endif

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <future>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

using namespace offline_translator;

namespace {

void require(bool ok, const std::string& message) {
    if (!ok) {
        throw std::runtime_error("FAIL: " + message);
    }
}

std::filesystem::path find_cpp_root() {
    auto dir = std::filesystem::current_path();
    for (int i = 0; i < 8; ++i) {
        if (std::filesystem::exists(dir / "CMakeLists.txt") &&
            std::filesystem::exists(
                dir / "cmake" / "CheckPortablePackage.cmake")) {
            return dir;
        }
        const auto parent = dir.parent_path();
        if (parent == dir) {
            break;
        }
        dir = parent;
    }
#ifdef _WIN32
    wchar_t buffer[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    if (length > 0 && length < MAX_PATH) {
        dir = std::filesystem::path(std::wstring(buffer, length)).parent_path();
        for (int i = 0; i < 8; ++i) {
            if (std::filesystem::exists(dir / "CMakeLists.txt") &&
                std::filesystem::exists(
                    dir / "cmake" / "CheckPortablePackage.cmake")) {
                return dir;
            }
            const auto parent = dir.parent_path();
            if (parent == dir) {
                break;
            }
            dir = parent;
        }
    }
#endif
    return {};
}

bool run_command(const std::wstring& command, unsigned timeout_ms) {
#ifdef _WIN32
    STARTUPINFOW startup{};
    PROCESS_INFORMATION process{};
    startup.cb = sizeof(startup);
    std::wstring mutable_command = command;
    if (!CreateProcessW(
            nullptr,
            mutable_command.data(),
            nullptr,
            nullptr,
            FALSE,
            CREATE_NO_WINDOW,
            nullptr,
            nullptr,
            &startup,
            &process)) {
        throw std::runtime_error("не удалось запустить внешнюю проверку");
    }
    const DWORD wait = WaitForSingleObject(process.hProcess, timeout_ms);
    DWORD exit_code = 1;
    if (wait == WAIT_OBJECT_0) {
        GetExitCodeProcess(process.hProcess, &exit_code);
    } else {
        TerminateProcess(process.hProcess, 1);
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    if (wait != WAIT_OBJECT_0) {
        throw std::runtime_error("таймаут внешней проверки упаковки");
    }
    return exit_code == 0;
#else
    static_cast<void>(command);
    static_cast<void>(timeout_ms);
    return false;
#endif
}

std::wstring quote_wide(const std::filesystem::path& path) {
    return L"\"" + path.wstring() + L"\"";
}

void test_packaging() {
    const auto cpp_root = find_cpp_root();
    if (cpp_root.empty()) {
        std::cout << "skip packaging: не найден каталог cpp/\n";
        return;
    }
    const auto package_dir = cpp_root / "portable-lite";
    const auto exe = package_dir / "TLing.exe";
    if (!std::filesystem::is_regular_file(exe)) {
        std::cout << "skip packaging: нет " << package_dir.string() << "\n";
        return;
    }
    const auto script = cpp_root / "cmake" / "CheckPortablePackage.cmake";
    require(
        std::filesystem::is_regular_file(script),
        "есть CheckPortablePackage.cmake");
#ifdef _WIN32
    const auto cmake_exe = std::filesystem::path(L"C:/Program Files/CMake/bin/cmake.exe");
    std::filesystem::path cmake = cmake_exe;
    if (!std::filesystem::is_regular_file(cmake)) {
        cmake = L"cmake";
    }
    std::wstring command = quote_wide(cmake) + L" -DPACKAGE_DIR=" +
        quote_wide(package_dir) + L" -P " + quote_wide(script);
    require(run_command(command, 60000), "cmake -P CheckPortablePackage");
    std::cout << "packaging: ok " << package_dir.string() << "\n";
#else
    std::cout << "skip packaging: не Windows\n";
#endif
}

void test_crash_recovery_settings() {
    const auto recover_dir =
        std::filesystem::temp_directory_path() /
        "offline-translator-integration-settings";
    std::filesystem::remove_all(recover_dir);
    std::filesystem::create_directories(recover_dir);
    const auto path = recover_dir / "settings.json";
    fs_utils::write_text_file(path, "{\"engine\":\"nllb\"");
    auto settings = load_settings(path);
    require(settings.engine == "argos", "обрезанный JSON → engine по умолчанию");
    fs_utils::write_text_file(path, "{{{{");
    settings = load_settings(path);
    require(
        settings.source_language == "en" && settings.target_language == "ru",
        "битый JSON → языки по умолчанию");
    std::filesystem::remove_all(recover_dir);
}

void test_gui_policy() {
    require(
        effect_for(UiCommand::smoke_start) == UiEffect::run_smoke_then_destroy,
        "GUI smoke уничтожает окно после проверки");
    require(!smoke_may_enable_autostart(), "smoke не включает автозагрузку");
}

#ifdef ENABLE_CTRANSLATE2
template <typename Fn>
auto run_with_timeout(Fn fn, std::chrono::seconds timeout, const char* label)
    -> decltype(fn()) {
    std::packaged_task<decltype(fn())()> task(std::move(fn));
    auto future = task.get_future();
    std::thread worker(std::move(task));
    if (future.wait_for(timeout) != std::future_status::ready) {
        worker.detach();
        throw std::runtime_error(
            std::string("таймаут перевода: ") + label);
    }
    worker.join();
    return future.get();
}

TranslationResult timed_translate(
    TranslationApplication& application,
    std::string_view text,
    std::string_view source,
    std::string_view target,
    std::chrono::seconds timeout,
    const char* label) {
    return run_with_timeout(
        [&application, text, source, target]() {
            return application.translate(text, source, target);
        },
        timeout,
        label);
}

void test_real_engines() {
    const auto argos_root = ArgosModelManager::default_packages_root();
    const auto nllb_root = NllbModelManager::default_models_root();
    const bool argos_en_ru =
        ArgosModelManager(argos_root, "en", "ru").is_installed();
    const bool argos_ru_en =
        ArgosModelManager(argos_root, "ru", "en").is_installed();
    const bool nllb_ok = NllbModelManager(nllb_root).is_installed();

    if (!argos_en_ru && !nllb_ok) {
        std::cout << "skip engines: нет пакетов Argos en→ru и NLLB\n";
        return;
    }

    TranslationSession session;
    constexpr auto kCallTimeout = std::chrono::seconds{45};

    if (argos_en_ru) {
        auto& argos = session.acquire(EngineKind::argos, argos_root);
        session.mark_loaded();
        require(session.is_loaded(), "сессия Argos помечена загруженной");
        std::string last;
        for (int i = 0; i < 4; ++i) {
            const auto result = timed_translate(
                argos,
                "Hello world",
                "en",
                "ru",
                kCallTimeout,
                "argos-repeat");
            require(!result.text.empty(), "повторный Argos возвращает текст");
            if (i == 0) {
                last = result.text;
            }
        }
        require(!last.empty(), "первый повторный Argos не пустой");
        std::cout << "argos repeat: " << last << "\n";

        const std::string long_text =
            "Hello world. This is a second sentence. "
            "The third sentence is longer than the first. "
            "Fourth sentence keeps going. Fifth sentence ends here.";
        const auto long_result = timed_translate(
            argos,
            long_text,
            "en",
            "ru",
            std::chrono::seconds{60},
            "argos-long");
        require(
            !long_result.text.empty(),
            "длинный текст Argos не пустой и не падает");
        require(
            long_result.text.size() > 40,
            "длинный текст Argos не обрезан до одного короткого фрагмента");
        std::cout << "argos long bytes in=" << long_text.size()
                  << " out=" << long_result.text.size() << "\n";

        if (argos_ru_en) {
            const auto ru_en = timed_translate(
                argos,
                "Привет мир",
                "ru",
                "en",
                kCallTimeout,
                "argos-ru-en");
            require(!ru_en.text.empty(), "Argos ru→en не пустой");
            std::cout << "argos ru->en: " << ru_en.text << "\n";
        } else {
            std::cout << "skip argos ru->en: пакет не установлен\n";
        }
    } else {
        std::cout << "skip argos: пакет en→ru не установлен\n";
    }

    if (nllb_ok) {
        auto& nllb = session.acquire(EngineKind::nllb, nllb_root);
        session.mark_loaded();
        for (int i = 0; i < 3; ++i) {
            const auto result = timed_translate(
                nllb,
                "Hello world",
                "en",
                "ru",
                kCallTimeout,
                "nllb-repeat");
            require(!result.text.empty(), "повторный NLLB возвращает текст");
            if (i == 0) {
                std::cout << "nllb repeat: " << result.text << "\n";
            }
        }
        const auto ru_en = timed_translate(
            nllb,
            "Привет мир",
            "ru",
            "en",
            kCallTimeout,
            "nllb-ru-en");
        require(!ru_en.text.empty(), "NLLB ru→en не пустой");
        std::cout << "nllb ru->en: " << ru_en.text << "\n";
        if (!argos_en_ru) {
            const std::string long_text =
                "Hello world. This is a second sentence. "
                "The third sentence is longer than the first.";
            const auto long_result = timed_translate(
                nllb,
                long_text,
                "en",
                "ru",
                std::chrono::seconds{60},
                "nllb-long");
            require(
                !long_result.text.empty(),
                "длинный текст NLLB не пустой и не падает");
            require(
                long_result.text.size() > 20,
                "длинный текст NLLB не обрезан 32 токенами");
        }
    } else {
        std::cout << "skip nllb: модель не установлена\n";
    }

    if (argos_en_ru && nllb_ok) {
        {
            auto& argos = session.acquire(EngineKind::argos, argos_root);
            const auto first = timed_translate(
                argos,
                "Hello world",
                "en",
                "ru",
                kCallTimeout,
                "switch-argos-1");
            require(!first.text.empty(), "переключение Argos→… первый вызов");
        }
        {
            auto& nllb = session.acquire(EngineKind::nllb, nllb_root);
            const auto mid = timed_translate(
                nllb,
                "Hello world",
                "en",
                "ru",
                kCallTimeout,
                "switch-nllb");
            require(!mid.text.empty(), "переключение …→NLLB→…");
        }
        {
            auto& argos = session.acquire(EngineKind::argos, argos_root);
            const auto again = timed_translate(
                argos,
                "Hello world",
                "en",
                "ru",
                kCallTimeout,
                "switch-argos-2");
            require(!again.text.empty(), "переключение …→Argos");
        }
        std::cout << "engine switch Argos→NLLB→Argos: ok\n";
    } else {
        std::cout << "skip engine switch: нужны Argos en→ru и NLLB\n";
    }

    session.reset();
    require(!session.is_loaded(), "reset сессии сбрасывает loaded");
}
#endif

}  // анонимное пространство имён

int main() {
    try {
        test_gui_policy();
        test_crash_recovery_settings();
        test_packaging();
#ifdef ENABLE_CTRANSLATE2
        test_real_engines();
#else
        std::cout << "skip engines: сборка без ENABLE_CTRANSLATE2\n";
#endif
        std::cout << "integration_tests: ok\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
