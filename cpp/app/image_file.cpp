#include "image_file.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <gdiplus.h>

#include <algorithm>
#include <cstring>
#include <memory>

#pragma comment(lib, "gdiplus.lib")

namespace offline_translator {
namespace {

std::optional<ScreenImage> copy_bitmap(Gdiplus::Bitmap& bitmap) {
    const int width = static_cast<int>(bitmap.GetWidth());
    const int height = static_cast<int>(bitmap.GetHeight());
    if (width <= 0 || height <= 0) {
        return std::nullopt;
    }
    Gdiplus::Rect rect(0, 0, width, height);
    Gdiplus::BitmapData data{};
    if (bitmap.LockBits(
            &rect,
            Gdiplus::ImageLockModeRead,
            PixelFormat32bppARGB,
            &data) != Gdiplus::Ok) {
        return std::nullopt;
    }
    ScreenImage image;
    image.width = width;
    image.height = height;
    image.bgra.resize(
        static_cast<std::size_t>(width) *
        static_cast<std::size_t>(height) * 4);
    const int stride = data.Stride;
    const auto* base = static_cast<const std::uint8_t*>(data.Scan0);
    for (int y = 0; y < height; ++y) {
        const std::uint8_t* row = base + static_cast<std::ptrdiff_t>(y) * stride;
        std::memcpy(
            image.bgra.data() + static_cast<std::size_t>(y) * width * 4,
            row,
            static_cast<std::size_t>(width) * 4);
    }
    bitmap.UnlockBits(&data);
    return image;
}

}  // анонимное пространство имён

std::optional<ScreenImage> load_image_file(
    const std::filesystem::path& path,
    int max_side) {
    Gdiplus::Bitmap bitmap(path.wstring().c_str(), FALSE);
    if (bitmap.GetLastStatus() != Gdiplus::Ok) {
        return std::nullopt;
    }
    const int width = static_cast<int>(bitmap.GetWidth());
    const int height = static_cast<int>(bitmap.GetHeight());
    if (width <= 0 || height <= 0) {
        return std::nullopt;
    }
    const int largest = std::max(width, height);
    if (max_side <= 0 || largest <= max_side) {
        return copy_bitmap(bitmap);
    }
    const double scale =
        static_cast<double>(max_side) / static_cast<double>(largest);
    const int scaled_width = std::max(1, static_cast<int>(width * scale));
    const int scaled_height = std::max(1, static_cast<int>(height * scale));
    Gdiplus::Bitmap scaled(scaled_width, scaled_height, PixelFormat32bppARGB);
    if (scaled.GetLastStatus() != Gdiplus::Ok) {
        return copy_bitmap(bitmap);
    }
    Gdiplus::Graphics graphics(&scaled);
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    if (graphics.DrawImage(
            &bitmap,
            0,
            0,
            scaled_width,
            scaled_height) != Gdiplus::Ok) {
        return copy_bitmap(bitmap);
    }
    return copy_bitmap(scaled);
}

}  // пространство имён offline_translator
