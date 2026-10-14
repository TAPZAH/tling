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
        has_cjk(text) ? L"Microsoft YaHei" : L"Arial";
    const wchar_t* candidates[4] = {
        preferred,
        L"Segoe UI",
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
    std::vector<int> red;
    std::vector<int> green;
    std::vector<int> blue;
    const int step = std::max(1, std::min(rect.Width, rect.Height) / 8);
    const auto sample = [&](int x, int y) {
        if (x < 0 || y < 0 || x >= static_cast<int>(bitmap.GetWidth()) ||
            y >= static_cast<int>(bitmap.GetHeight())) {
            return;
        }
        Gdiplus::Color color;
        if (bitmap.GetPixel(x, y, &color) == Gdiplus::Ok) {
            red.push_back(color.GetR());
            green.push_back(color.GetG());
            blue.push_back(color.GetB());
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
    if (red.empty()) {
        return Gdiplus::Color(255, 255, 255, 255);
    }
    // Медиана устойчива к тёмным линиям/глифам на рамке выборки: фон берём
    // как доминирующий цвет, иначе получались серые плашки.
    const auto median = [](std::vector<int>& values) {
        std::sort(values.begin(), values.end());
        return values[values.size() / 2];
    };
    return Gdiplus::Color(
        255,
        static_cast<BYTE>(median(red)),
        static_cast<BYTE>(median(green)),
        static_cast<BYTE>(median(blue)));
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

    struct DrawItem {
        const LayoutCell* cell{nullptr};
        int fill_left{0};
        int fill_right{0};
        int left{0};
        int right{0};
        int top{0};
        int bottom{0};
        int fill_bottom{0};
        float original_size{12.0f};
        bool wrap{false};
        bool centered{false};
    };
    std::vector<DrawItem> items;

    for (const auto& block : page.blocks) {
        for (std::size_t index = 0; index < block.lines.size(); ++index) {
            const auto& line = block.lines[index];
            // Абзац (склеенные строки) — переводится целиком с переносом.
            const bool line_paragraph =
                (line.bottom - line.top) > line.font_height * 3 / 2;
            // Нижняя граница: для абзаца тянем вниз в промежуток до
            // следующего абзаца, чтобы перенесённый текст влез целиком.
            double line_box_bottom = static_cast<double>(line.bottom);
            if (line_paragraph) {
                const double next_top = index + 1 < block.lines.size()
                    ? static_cast<double>(block.lines[index + 1].top)
                    : static_cast<double>(block.bottom) + line.font_height;
                line_box_bottom = std::max(
                    static_cast<double>(line.bottom), next_top - 2.0);
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
                const bool paragraph = line_paragraph;
                double natural_right =
                    position + 1 < filled.size()
                        ? static_cast<double>(
                              line.cells[filled[position + 1]].left)
                        : (block.table ? static_cast<double>(block.right)
                                       : static_cast<double>(page.width));
                const double draw_right = (paragraph || !block.table)
                    ? static_cast<double>(cell.right)
                    : std::max(
                          natural_right,
                          static_cast<double>(cell.right));

                DrawItem item;
                item.cell = &cell;
                item.wrap = paragraph;
                // Строка, которая в оригинале стоит по центру (название,
                // подпись компании) — рисуем перевод тоже по центру.
                {
                    const int line_center = (line.left + line.right) / 2;
                    const int block_center = (block.left + block.right) / 2;
                    const int line_width = std::max(1, line.right - line.left);
                    const int block_width =
                        std::max(1, block.right - block.left);
                    item.centered = !paragraph &&
                        line_width < block_width * 3 / 4 &&
                        std::abs(line_center - block_center) <
                            std::max(30, block_width / 12);
                }
                item.left = std::max(
                    0,
                    static_cast<int>(std::floor(cell.left * scale)) - 1);
                item.fill_right = std::min(
                    width,
                    static_cast<int>(std::ceil(cell.right * scale)) + 1);
                item.right = std::min(
                    width,
                    static_cast<int>(std::ceil(draw_right * scale)) + 1);
                item.top = std::max(
                    0,
                    static_cast<int>(std::floor(line.top * scale)) - 1);
                item.bottom = std::min(
                    height,
                    static_cast<int>(std::ceil(line_box_bottom * scale)) + 1);
                // Затираем: у абзаца — всю выделенную область, у строки/ячейки
                // — только текст (чтобы не задеть линии таблицы).
                item.fill_bottom = item.wrap
                    ? item.bottom
                    : std::min(
                          height,
                          static_cast<int>(
                              std::ceil(line.bottom * scale)) + 1);
                // Однострочный текст рисуем боксом высотой с его кегль
                // (заголовки с тонким OCR-боксом иначе обрезались).
                if (!item.wrap) {
                    const int text_bottom =
                        item.top +
                        static_cast<int>(std::ceil(
                            line.font_height * scale * 1.3)) +
                        2;
                    item.bottom = std::min(
                        height, std::max(item.bottom, text_bottom));
                }
                item.original_size = std::max(
                    9.0f,
                    static_cast<float>(line.font_height) *
                        static_cast<float>(scale));
                if (item.right > item.left && item.bottom > item.top) {
                    items.push_back(item);
                }
            }
        }
    }

    // Сначала затираем весь исходный текст, потом рисуем переводы: иначе
    // заливка следующей строки стирает уже нарисованный перевод предыдущей.
    for (const auto& item : items) {
        if (item.fill_right <= item.left) {
            continue;
        }
        const Gdiplus::Rect fill_rect(
            item.left,
            item.top,
            item.fill_right - item.left,
            item.fill_bottom - item.top);
        // Цвет фона берём чуть за пределами текста: рамка текстового бокса
        // сама задевает глифы, из-за чего заливка становилась серой.
        const int sample_pad = 2;
        Gdiplus::Rect sample_rect(
            std::max(0, fill_rect.X - sample_pad),
            std::max(0, fill_rect.Y - sample_pad),
            fill_rect.Width + sample_pad * 2,
            fill_rect.Height + sample_pad * 2);
        const Gdiplus::Color background =
            sample_background(bitmap, sample_rect);
        Gdiplus::SolidBrush fill(background);
        graphics.FillRectangle(&fill, fill_rect);
    }

    for (const auto& item : items) {
        const LayoutCell& cell = *item.cell;
        auto family = std::make_unique<Gdiplus::FontFamily>(
            pick_font_name(cell.text).c_str());
        const float box_width = static_cast<float>(item.right - item.left);
        const float box_height = static_cast<float>(item.bottom - item.top);
        const int style = cell.bold ? Gdiplus::FontStyleBold
                                    : Gdiplus::FontStyleRegular;
        // Строка — «строка-в-строку» без переноса; абзац — с переносом внутри
        // своей области. В обоих случаях шрифт уменьшается до вписывания.
        const float min_size = std::max(6.0f, item.original_size * 0.35f);
        float size = item.original_size;
        std::unique_ptr<Gdiplus::Font> font;
        for (int attempt = 0; attempt < 160; ++attempt) {
            font = std::make_unique<Gdiplus::Font>(
                family.get(),
                size,
                style,
                Gdiplus::UnitPixel);
            Gdiplus::RectF needed(0, 0, 0, 0);
            if (item.wrap) {
                Gdiplus::RectF layout_rect(0, 0, box_width, box_height);
                graphics.MeasureString(
                    cell.text.c_str(),
                    -1,
                    font.get(),
                    layout_rect,
                    &needed);
                if (needed.Height <= box_height + 0.5f || size <= min_size) {
                    break;
                }
            } else {
                graphics.MeasureString(
                    cell.text.c_str(),
                    -1,
                    font.get(),
                    Gdiplus::PointF(0, 0),
                    &needed);
                if (needed.Width <= box_width + 0.5f || size <= min_size) {
                    break;
                }
            }
            size = std::max(min_size, size - 0.5f);
        }
        Gdiplus::StringFormat draw_format;
        if (!item.wrap) {
            draw_format.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
        }
        draw_format.SetAlignment(
            item.centered ? Gdiplus::StringAlignmentCenter
                          : Gdiplus::StringAlignmentNear);
        draw_format.SetLineAlignment(
            item.wrap ? Gdiplus::StringAlignmentNear
                      : Gdiplus::StringAlignmentCenter);
        draw_format.SetTrimming(item.wrap
                                    ? Gdiplus::StringTrimmingNone
                                    : Gdiplus::StringTrimmingEllipsisCharacter);
        Gdiplus::SolidBrush text_brush(Gdiplus::Color(255, 0, 0, 0));
        graphics.DrawString(
            cell.text.c_str(),
            -1,
            font.get(),
            Gdiplus::RectF(
                static_cast<float>(item.left),
                static_cast<float>(item.top),
                box_width,
                box_height),
            &draw_format,
            &text_brush);
    }

    CLSID clsid{};
    if (!png_encoder_clsid(&clsid)) {
        return false;
    }
    return bitmap.Save(output_png.wstring().c_str(), &clsid, nullptr) ==
        Gdiplus::Ok;
}

}  // пространство имён offline_translator
