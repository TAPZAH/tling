#include "image_render.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <gdiplus.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

#pragma comment(lib, "gdiplus.lib")

namespace offline_translator {
namespace {

bool has_cjk(const std::wstring& text) {
    for (const wchar_t symbol : text) {
        const unsigned code = static_cast<unsigned>(symbol);
        if ((code >= 0x4E00 && code <= 0x9FFF) ||
            (code >= 0x3400 && code <= 0x4DBF) ||
            (code >= 0x3040 && code <= 0x30FF) ||
            (code >= 0xAC00 && code <= 0xD7AF)) {
            return true;
        }
    }
    return false;
}

std::wstring pick_font_name(const std::wstring& text) {
    const wchar_t* preferred =
        has_cjk(text) ? L"Microsoft YaHei" : L"Segoe UI";
    const wchar_t* candidates[4] = {
        preferred,
        L"Arial",
        L"Tahoma",
        L"Microsoft Sans Serif"};
    for (const wchar_t* name : candidates) {
        Gdiplus::FontFamily family(name);
        if (family.IsAvailable()) {
            return name;
        }
    }
    return L"Arial";
}

Gdiplus::Color sample_background(
    Gdiplus::Bitmap& bitmap,
    const Gdiplus::Rect& rect) {
    long red = 0;
    long green = 0;
    long blue = 0;
    long count = 0;
    const int step = std::max(1, std::min(rect.Width, rect.Height) / 8);
    const auto sample = [&](int x, int y) {
        if (x < 0 || y < 0 || x >= static_cast<int>(bitmap.GetWidth()) ||
            y >= static_cast<int>(bitmap.GetHeight())) {
            return;
        }
        Gdiplus::Color color;
        if (bitmap.GetPixel(x, y, &color) == Gdiplus::Ok) {
            red += color.GetR();
            green += color.GetG();
            blue += color.GetB();
            ++count;
        }
    };
    for (int x = rect.X; x < rect.X + rect.Width; x += step) {
        sample(x, rect.Y);
        sample(x, rect.Y + rect.Height - 1);
    }
    for (int y = rect.Y; y < rect.Y + rect.Height; y += step) {
        sample(rect.X, y);
        sample(rect.X + rect.Width - 1, y);
    }
    if (count == 0) {
        return Gdiplus::Color(255, 255, 255, 255);
    }
    return Gdiplus::Color(
        255,
        static_cast<BYTE>(red / count),
        static_cast<BYTE>(green / count),
        static_cast<BYTE>(blue / count));
}

bool png_encoder_clsid(CLSID* clsid) {
    UINT count = 0;
    UINT size = 0;
    Gdiplus::GetImageEncodersSize(&count, &size);
    if (size == 0) {
        return false;
    }
    std::vector<BYTE> buffer(size);
    auto* codecs =
        reinterpret_cast<Gdiplus::ImageCodecInfo*>(buffer.data());
    Gdiplus::GetImageEncoders(count, size, codecs);
    for (UINT index = 0; index < count; ++index) {
        if (wcscmp(codecs[index].MimeType, L"image/png") == 0) {
            *clsid = codecs[index].Clsid;
            return true;
        }
    }
    return false;
}

}  // анонимное пространство имён

bool render_translated_image(
    const LayoutPage& page,
    const std::vector<std::uint8_t>& bgra,
    int width,
    int height,
    const std::filesystem::path& output_png) {
    if (bgra.empty() || width <= 0 || height <= 0) {
        return false;
    }
    Gdiplus::Bitmap bitmap(
        width,
        height,
        width * 4,
        PixelFormat32bppARGB,
        const_cast<BYTE*>(reinterpret_cast<const BYTE*>(bgra.data())));
    if (bitmap.GetLastStatus() != Gdiplus::Ok) {
        return false;
    }
    Gdiplus::Graphics graphics(&bitmap);
    if (graphics.GetLastStatus() != Gdiplus::Ok) {
        return false;
    }
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
    graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);

    const double scale = page.width > 0
        ? static_cast<double>(width) / page.width
        : 1.0;
    for (const auto& block : page.blocks) {
        for (std::size_t index = 0; index < block.lines.size(); ++index) {
            const auto& line = block.lines[index];
            // Высота строки: до следующей строки блока (или низа блока) —
            // тогда перевод переносится по словам, а не ужимается.
            double row_bottom = index + 1 < block.lines.size()
                ? static_cast<double>(block.lines[index + 1].top)
                : static_cast<double>(block.bottom) + line.font_height;
            // Не даём заливке разрастись: не больше одной дополнительной
            // строки под текстом.
            row_bottom = std::min(
                row_bottom,
                static_cast<double>(line.bottom) + line.font_height);
            if (row_bottom <= line.top) {
                row_bottom = line.bottom;
            }
            std::vector<std::size_t> filled;
            for (std::size_t cell_index = 0;
                 cell_index < line.cells.size();
                 ++cell_index) {
                if (!line.cells[cell_index].text.empty()) {
                    filled.push_back(cell_index);
                }
            }
            for (std::size_t position = 0; position < filled.size();
                 ++position) {
                const auto& cell = line.cells[filled[position]];
                // Текст можно тянуть до следующей фразы справа, а если её
                // нет — до края таблицы/страницы (с запасом), но затираем
                // только исходную ячейку, чтобы не портить соседние.
                double draw_bound = position + 1 < filled.size()
                    ? static_cast<double>(
                          line.cells[filled[position + 1]].left)
                    : (block.table
                           ? static_cast<double>(block.right)
                           : static_cast<double>(page.width));
                const double original_width = std::max(
                    1.0,
                    static_cast<double>(cell.right - cell.left));
                draw_bound = std::min(
                    draw_bound,
                    static_cast<double>(cell.left) + original_width * 3.0 +
                        40.0);
                const double draw_right = std::max(
                    draw_bound,
                    static_cast<double>(cell.right));
                int left = static_cast<int>(std::floor(cell.left * scale)) - 1;
                int fill_right =
                    static_cast<int>(std::ceil(cell.right * scale)) + 1;
                int right =
                    static_cast<int>(std::ceil(draw_right * scale)) + 1;
                int top = static_cast<int>(std::floor(line.top * scale)) - 1;
                int bottom =
                    static_cast<int>(std::ceil(row_bottom * scale)) + 1;
                left = std::max(0, left);
                top = std::max(0, top);
                fill_right = std::min(width, fill_right);
                right = std::min(width, right);
                bottom = std::min(height, bottom);
                if (right <= left || bottom <= top) {
                    continue;
                }
                if (fill_right > left) {
                    const Gdiplus::Rect fill_rect(
                        left,
                        top,
                        fill_right - left,
                        bottom - top);
                    const Gdiplus::Color background =
                        sample_background(bitmap, fill_rect);
                    Gdiplus::SolidBrush fill(background);
                    graphics.FillRectangle(&fill, fill_rect);
                }

                auto family = std::make_unique<Gdiplus::FontFamily>(
                    pick_font_name(cell.text).c_str());
                const float box_width = static_cast<float>(right - left);
                const float box_height = static_cast<float>(bottom - top);
                const int style = cell.bold ? Gdiplus::FontStyleBold
                                            : Gdiplus::FontStyleRegular;
                // Шрифт как в оригинале; при нехватке ширины уменьшаем до
                // минимально приемлемого, и только потом переносим.
                const float original_size = std::max(
                    9.0f,
                    static_cast<float>(line.font_height) *
                        static_cast<float>(scale));
                const float min_size = std::max(8.0f, original_size * 0.6f);
                float size = original_size;
                std::unique_ptr<Gdiplus::Font> font;
                for (int attempt = 0; attempt < 80; ++attempt) {
                    font = std::make_unique<Gdiplus::Font>(
                        family.get(),
                        size,
                        style,
                        Gdiplus::UnitPixel);
                    Gdiplus::RectF single(0, 0, 0, 0);
                    graphics.MeasureString(
                        cell.text.c_str(),
                        -1,
                        font.get(),
                        Gdiplus::PointF(0, 0),
                        &single);
                    if (single.Width <= box_width + 0.5f ||
                        size <= min_size) {
                        break;
                    }
                    size = std::max(min_size, size - 0.5f);
                }
                Gdiplus::StringFormat draw_format;
                draw_format.SetAlignment(Gdiplus::StringAlignmentNear);
                draw_format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
                draw_format.SetTrimming(Gdiplus::StringTrimmingEllipsisWord);
                Gdiplus::SolidBrush text_brush(Gdiplus::Color(255, 0, 0, 0));
                graphics.DrawString(
                    cell.text.c_str(),
                    -1,
                    font.get(),
                    Gdiplus::RectF(
                        static_cast<float>(left),
                        static_cast<float>(top),
                        box_width,
                        box_height),
                    &draw_format,
                    &text_brush);
            }
        }
    }

    CLSID clsid{};
    if (!png_encoder_clsid(&clsid)) {
        return false;
    }
    return bitmap.Save(output_png.wstring().c_str(), &clsid, nullptr) ==
        Gdiplus::Ok;
}

}  // пространство имён offline_translator
