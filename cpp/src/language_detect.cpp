#include "offline_translator/language_detect.hpp"

#include <windows.h>

#include <filesystem>
#include <string>

namespace offline_translator {
namespace {

using DetectFn = char* (*)(const char* text, const char* allowlist);
using FreeFn = void (*)(char* string);

struct Bridge {
    HMODULE module{nullptr};
    DetectFn detect{nullptr};
    FreeFn string_free{nullptr};

    bool valid() const {
        return detect != nullptr && string_free != nullptr;
    }
};

std::filesystem::path module_directory() {
    std::wstring buffer(MAX_PATH, L'\0');
    while (true) {
        const DWORD length = ::GetModuleFileNameW(
            nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) {
            return {};
        }
        if (length < buffer.size()) {
            buffer.resize(length);
            break;
        }
        buffer.resize(buffer.size() * 2);
    }
    return std::filesystem::path(buffer).parent_path();
}

const Bridge& bridge() {
    static Bridge loaded = [] {
        Bridge result;
        // Грузим строго из каталога приложения: иначе Windows может найти
        // постороннюю fxbridge.dll в текущем каталоге или PATH.
        const auto directory = module_directory();
        const auto path = directory.empty()
            ? std::filesystem::path(L"fxbridge.dll")
            : directory / L"fxbridge.dll";
        result.module = ::LoadLibraryW(path.wstring().c_str());
        if (!result.module) {
            return result;
        }
        result.detect = reinterpret_cast<DetectFn>(reinterpret_cast<void*>(
            ::GetProcAddress(result.module, "fxt_detect_language")));
        result.string_free = reinterpret_cast<FreeFn>(reinterpret_cast<void*>(
            ::GetProcAddress(result.module, "fxt_string_free")));
        return result;
    }();
    return loaded;
}

}  // namespace

bool language_detector_available() {
    return bridge().valid();
}

std::string detect_language_code(
    std::string_view text,
    const std::vector<std::string>& allowed) {
    const Bridge& loaded = bridge();
    if (!loaded.valid() || text.empty()) {
        return {};
    }
    std::string joined;
    for (const auto& code : allowed) {
        if (code.empty()) {
            continue;
        }
        if (!joined.empty()) {
            joined += ',';
        }
        joined += code;
    }
    const std::string input(text);
    char* raw = loaded.detect(input.c_str(), joined.c_str());
    if (!raw) {
        return {};
    }
    std::string result(raw);
    loaded.string_free(raw);
    return result;
}

}  // namespace offline_translator
