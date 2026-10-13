#pragma once

#include "screen_ocr.hpp"

#include <filesystem>
#include <optional>

namespace offline_translator {

// Загружает изображение (PNG/JPG/BMP/GIF/TIFF) и при необходимости
// уменьшает его так, чтобы большая сторона не превышала max_side.
// Возвращает BGRA-изображение (top-down) или nullopt, если файл не открылся.
std::optional<ScreenImage> load_image_file(
    const std::filesystem::path& path,
    int max_side = 3500);

}  // пространство имён offline_translator
