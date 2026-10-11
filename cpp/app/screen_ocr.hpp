#pragma once

#include <cstdint>
#include <optional>
#include <vector>

namespace offline_translator {

// Изображение области экрана в формате BGRA (top-down, 4 байта на пиксель).
struct ScreenImage {
    std::vector<std::uint8_t> bgra;
    int width{0};
    int height{0};
};

// Показывает на весь виртуальный экран замороженный снимок, поверх которого
// пользователь выделяет прямоугольник. Возвращает изображение выбранной
// области (увеличенное в scale раз для OCR) или nullopt при отмене (Esc/ПКМ).
std::optional<ScreenImage> select_screen_region_image(int scale = 2);

}  // namespace offline_translator
