#pragma once

#include "offline_translator/package_types.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace offline_translator {

// Скачивает файл с докачкой через .part. Локальный путь тоже считается
// источником — так тесты обходятся без сети и без 622 МБ модели.
void download_resumable(
    const std::vector<std::string>& urls,
    const std::filesystem::path& destination,
    const ProgressCallback& progress,
    std::string_view message,
    std::optional<std::uintmax_t> skip_if_at_least = std::nullopt);

}  // пространство имён offline_translator
