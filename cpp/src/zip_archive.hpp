#pragma once

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace offline_translator {

// Распаковывает ZIP (STORE и DEFLATE) в каталог назначения.
void extract_zip(
    const std::filesystem::path& zip_path,
    const std::filesystem::path& destination);

// Пишет несжатый ZIP — для тестов установки Argos без сети.
void write_store_zip(
    const std::filesystem::path& zip_path,
    const std::vector<std::pair<std::string, std::filesystem::path>>&
        named_files);

}  // пространство имён offline_translator
