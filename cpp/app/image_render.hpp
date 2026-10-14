#pragma once

#include "offline_translator/document_layout.hpp"

#include <cstdint>
#include <filesystem>
#include <vector>

namespace offline_translator {

// Рисует перевод поверх изображения: затирает оригинал цветом фона и
// пишет переведённый текст системным шрифтом. Координаты разметки
// масштабируются из page.width в ширину изображения. Сохраняет PNG.
bool render_translated_image(
    const LayoutPage& page,
    const std::vector<std::uint8_t>& bgra,
    int width,
    int height,
    const std::filesystem::path& output_png);

}  // пространство имён offline_translator
