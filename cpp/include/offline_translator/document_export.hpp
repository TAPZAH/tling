#pragma once

#include "offline_translator/document_layout.hpp"

#include <filesystem>
#include <vector>

namespace offline_translator {

// Сохраняет разметку страниц как документ Word (.docx) или таблицу
// Excel (.xlsx) — формат выбирается по расширению пути.
void export_layout_document(
    const std::vector<LayoutPage>& pages,
    const std::filesystem::path& path);

// То же для одной страницы.
void export_layout_page(
    const LayoutPage& page,
    const std::filesystem::path& path);

}  // пространство имён offline_translator
