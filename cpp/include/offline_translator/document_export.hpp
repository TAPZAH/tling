#pragma once

#include "offline_translator/document_layout.hpp"

#include <filesystem>

namespace offline_translator {

// Сохраняет разметку страницы как документ Word (.docx) или таблицу
// Excel (.xlsx) — формат выбирается по расширению пути.
void export_layout_page(
    const LayoutPage& page,
    const std::filesystem::path& path);

}  // пространство имён offline_translator
