#include "offline_translator/document_export.hpp"
#include "offline_translator/document_layout.hpp"
#include "offline_translator/ocr_engine.hpp"

#include "image_file.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <gdiplus.h>

#include <filesystem>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#pragma comment(lib, "gdiplus.lib")

namespace {

int encoder_clsid(const wchar_t* mime, CLSID* clsid) {
    UINT count = 0;
    UINT size = 0;
    Gdiplus::GetImageEncodersSize(&count, &size);
    if (size == 0) {
        return -1;
    }
    std::vector<BYTE> buffer(size);
    auto* codecs =
        reinterpret_cast<Gdiplus::ImageCodecInfo*>(buffer.data());
    Gdiplus::GetImageEncoders(count, size, codecs);
    for (UINT index = 0; index < count; ++index) {
        if (wcscmp(codecs[index].MimeType, mime) == 0) {
            *clsid = codecs[index].Clsid;
            return static_cast<int>(index);
        }
    }
    return -1;
}

std::wstring page_text(const offline_translator::LayoutPage& page) {
    std::wstring text;
    for (const auto& block : page.blocks) {
        for (const auto& line : block.lines) {
            for (const auto& cell : line.cells) {
                text += cell.text;
                text += L'\n';
            }
        }
    }
    return text;
}

std::string to_utf8(const std::wstring& text) {
    if (text.empty()) {
        return {};
    }
    const int size = WideCharToMultiByte(
        CP_UTF8,
        0,
        text.data(),
        static_cast<int>(text.size()),
        nullptr,
        0,
        nullptr,
        nullptr);
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(
        CP_UTF8,
        0,
        text.data(),
        static_cast<int>(text.size()),
        result.data(),
        size,
        nullptr,
        nullptr);
    return result;
}

}  // анонимное пространство имён

int main(int argc, char** argv) {
    using namespace offline_translator;
    // Диагностика: image_layout_tests --file <вход> [выход].
    if (argc >= 3 && std::string(argv[1]) == "--file") {
        Gdiplus::GdiplusStartupInput input;
        ULONG_PTR token = 0;
        if (Gdiplus::GdiplusStartup(&token, &input, nullptr) !=
            Gdiplus::Ok) {
            return 1;
        }
        auto image = load_image_file(std::filesystem::path(argv[2]));
        if (!image) {
            std::cout << "file: не загрузилось\n";
            Gdiplus::GdiplusShutdown(token);
            return 1;
        }
        int result = 0;
        try {
            OcrEngine engine;
            std::vector<std::string> codes{"en"};
            if (tessdata_installed("ru")) {
                codes.push_back("ru");
            }
            engine.initialize_multi(codes);
            int angle = 0;
            const auto layout = recognize_best_orientation(
                engine,
                image->bgra.data(),
                image->width,
                image->height,
                image->width * 4,
                4,
                &angle);
            int rotated_width = image->width;
            int rotated_height = image->height;
            std::vector<std::uint8_t> rotated;
            if (angle != 0) {
                rotated = rotate_bgra_image(
                    image->bgra.data(),
                    image->width,
                    image->height,
                    image->width * 4,
                    angle,
                    &rotated_width,
                    &rotated_height);
            }
            const std::uint8_t* grid_image =
                angle != 0 ? rotated.data() : image->bgra.data();
            const auto grid_columns = detect_vertical_grid_lines(
                grid_image,
                rotated_width,
                rotated_height,
                rotated_width * 4);
            const auto page = build_layout_page(layout, grid_columns);
            int tables = 0;
            std::size_t max_cells = 0;
            std::size_t multi_cell_lines = 0;
            for (const auto& block : page.blocks) {
                if (block.table) {
                    ++tables;
                }
                for (const auto& line : block.lines) {
                    if (line.cells.size() > max_cells) {
                        max_cells = line.cells.size();
                    }
                    if (line.cells.size() >= 2) {
                        ++multi_cell_lines;
                    }
                }
            }
            std::cout << "file: angle=" << angle
                      << " words=" << layout.words.size()
                      << " blocks=" << page.blocks.size()
                      << " grid=" << grid_columns.size()
                      << " tables=" << tables
                      << " max_cells=" << max_cells
                      << " multi_cell_lines=" << multi_cell_lines << "\n";
            // Диагностика строк: сколько слов и какие зазоры.
            {
                std::vector<const OcrWordBox*> ordered;
                ordered.reserve(layout.words.size());
                for (const auto& word : layout.words) {
                    ordered.push_back(&word);
                }
                std::sort(
                    ordered.begin(),
                    ordered.end(),
                    [](const OcrWordBox* left, const OcrWordBox* right) {
                        if (left->block != right->block) {
                            return left->block < right->block;
                        }
                        if (left->paragraph != right->paragraph) {
                            return left->paragraph < right->paragraph;
                        }
                        if (left->line != right->line) {
                            return left->line < right->line;
                        }
                        return left->left < right->left;
                    });
                std::vector<int> heights;
                for (const auto* word : ordered) {
                    heights.push_back(word->height);
                }
                std::sort(heights.begin(), heights.end());
                const int median_height = heights.empty()
                    ? 0
                    : heights[heights.size() / 2];
                std::cout << "file: median_word_height=" << median_height
                          << "\n";
                int printed = 0;
                for (std::size_t index = 0;
                     index < ordered.size() && printed < 10;) {
                    std::size_t end = index + 1;
                    while (end < ordered.size() &&
                           ordered[end]->block == ordered[index]->block &&
                           ordered[end]->paragraph ==
                               ordered[index]->paragraph &&
                           ordered[end]->line == ordered[index]->line) {
                        ++end;
                    }
                    std::cout << "  line words=" << (end - index);
                    for (std::size_t k = index + 1; k < end; ++k) {
                        std::cout << " gap="
                                  << (ordered[k]->left -
                                      (ordered[k - 1]->left +
                                       ordered[k - 1]->width));
                    }
                    std::cout << "\n";
                    ++printed;
                    index = end;
                }
            }
            if (argc >= 4) {
                export_layout_page(page, std::filesystem::path(argv[3]));
                std::cout << "file: exported " << argv[3] << "\n";
            }
        } catch (const std::exception& error) {
            std::cout << "file: FAIL - " << error.what() << "\n";
            result = 1;
        }
        Gdiplus::GdiplusShutdown(token);
        return result;
    }
    if (!tessdata_installed("en")) {
        std::cout << "image_layout_tests: skip - нет eng.traineddata\n";
        return 0;
    }
    Gdiplus::GdiplusStartupInput input;
    ULONG_PTR token = 0;
    if (Gdiplus::GdiplusStartup(&token, &input, nullptr) != Gdiplus::Ok) {
        std::cout << "image_layout_tests: FAIL - GDI+ не стартовал\n";
        return 1;
    }
    const auto directory =
        std::filesystem::temp_directory_path() /
        "offline-translator-image-test";
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);
    const auto png = directory / "sample.png";
    {
        Gdiplus::Bitmap bitmap(900, 300, PixelFormat32bppARGB);
        Gdiplus::Graphics graphics(&bitmap);
        graphics.Clear(Gdiplus::Color(255, 255, 255, 255));
        Gdiplus::Font font(
            L"Segoe UI",
            36.0f,
            Gdiplus::FontStyleRegular,
            Gdiplus::UnitPixel);
        Gdiplus::SolidBrush brush(Gdiplus::Color(255, 0, 0, 0));
        Gdiplus::RectF first(30, 30, 840, 100);
        graphics.DrawString(L"Hello world", -1, &font, first, nullptr, &brush);
        Gdiplus::RectF second(30, 160, 840, 100);
        graphics.DrawString(L"Привет мир", -1, &font, second, nullptr, &brush);
        CLSID png_clsid{};
        if (encoder_clsid(L"image/png", &png_clsid) < 0 ||
            bitmap.Save(png.wstring().c_str(), &png_clsid, nullptr) !=
                Gdiplus::Ok) {
            std::cout << "image_layout_tests: FAIL - PNG не сохранён\n";
            Gdiplus::GdiplusShutdown(token);
            return 1;
        }
    }
    auto image = load_image_file(png);
    if (!image || image->bgra.empty()) {
        std::cout << "image_layout_tests: FAIL - изображение не загрузилось\n";
        Gdiplus::GdiplusShutdown(token);
        return 1;
    }
    int result = 0;
    try {
        OcrEngine engine;
        std::vector<std::string> codes{"en"};
        if (tessdata_installed("ru")) {
            codes.push_back("ru");
        }
        engine.initialize_multi(codes);
        int rotation = 0;
        const auto layout = recognize_best_orientation(
            engine,
            image->bgra.data(),
            image->width,
            image->height,
            image->width * 4,
            4,
            &rotation);
        if (layout.words.empty()) {
            std::cout << "image_layout_tests: FAIL - OCR не нашёл слов\n";
            result = 1;
        } else if (rotation != 0) {
            std::cout << "image_layout_tests: FAIL - ровное изображение "
                         "повёрнуто на " << rotation << "\n";
            result = 1;
        } else {
            auto page = build_layout_page(layout);
            const std::wstring text = page_text(page);
            if (text.find(L"Hello") == std::wstring::npos ||
                text.find(L"world") == std::wstring::npos ||
                text.find(L"Привет") == std::wstring::npos ||
                text.find(L"мир") == std::wstring::npos) {
                std::cout << "image_layout_tests: FAIL - текст не распознан: "
                          << to_utf8(text) << "\n";
                result = 1;
            } else {
                const auto texts = layout_texts(page);
                if (texts.empty()) {
                    std::cout << "image_layout_tests: FAIL - нет текстов\n";
                    result = 1;
                } else {
                    std::vector<std::wstring> translated;
                    for (const auto& item : texts) {
                        translated.push_back(L"[" + item + L"]");
                    }
                    apply_layout_translations(&page, translated);
                    const auto docx = directory / "out.docx";
                    const auto xlsx = directory / "out.xlsx";
                    export_layout_page(page, docx);
                    export_layout_page(page, xlsx);
                    if (!std::filesystem::is_regular_file(docx) ||
                        !std::filesystem::is_regular_file(xlsx)) {
                        std::cout
                            << "image_layout_tests: FAIL - файлы не созданы\n";
                        result = 1;
                    } else {
                        std::cout << "image_layout_tests: ok - "
                                  << to_utf8(page_text(page)) << "\n";
                    }
                }
            }
        }
        if (result == 0) {
            // Таблица: рисуем сетку с текстом и проверяем, что разметка
            // собрана в таблицу.
            const auto table_png = directory / "table.png";
            {
                Gdiplus::Bitmap bitmap(900, 420, PixelFormat32bppARGB);
                Gdiplus::Graphics graphics(&bitmap);
                graphics.Clear(Gdiplus::Color(255, 255, 255, 255));
                Gdiplus::Pen pen(Gdiplus::Color(255, 0, 0, 0), 2.0f);
                const int xs[4] = {40, 340, 640, 860};
                const int ys[4] = {20, 140, 270, 400};
                for (const int x : xs) {
                    graphics.DrawLine(&pen, x, ys[0], x, ys[3]);
                }
                for (const int y : ys) {
                    graphics.DrawLine(&pen, xs[0], y, xs[3], y);
                }
                Gdiplus::Font font(
                    L"Segoe UI",
                    28.0f,
                    Gdiplus::FontStyleRegular,
                    Gdiplus::UnitPixel);
                Gdiplus::SolidBrush brush(Gdiplus::Color(255, 0, 0, 0));
                const wchar_t* cells[9] = {
                    L"Name", L"Qty", L"Price",
                    L"Apple", L"10", L"5.00",
                    L"Pear", L"20", L"7.50"};
                for (int row = 0; row < 3; ++row) {
                    for (int column = 0; column < 3; ++column) {
                        Gdiplus::RectF bounds(
                            static_cast<float>(xs[column] + 12),
                            static_cast<float>(ys[row] + 30),
                            260.0f,
                            60.0f);
                        graphics.DrawString(
                            cells[row * 3 + column],
                            -1,
                            &font,
                            bounds,
                            nullptr,
                            &brush);
                    }
                }
                CLSID png_clsid{};
                if (encoder_clsid(L"image/png", &png_clsid) < 0 ||
                    bitmap.Save(
                        table_png.wstring().c_str(),
                        &png_clsid,
                        nullptr) != Gdiplus::Ok) {
                    std::cout << "image_layout_tests: FAIL - таблица "
                                 "не сохранена\n";
                    result = 1;
                }
            }
            if (result == 0) {
                auto table_image = load_image_file(table_png);
                if (!table_image) {
                    std::cout
                        << "image_layout_tests: FAIL - таблица не загрузилась\n";
                    result = 1;
                } else {
                    int angle = 0;
                    const auto table_layout = recognize_best_orientation(
                        engine,
                        table_image->bgra.data(),
                        table_image->width,
                        table_image->height,
                        table_image->width * 4,
                        4,
                        &angle);
                    const auto table_page = build_layout_page(table_layout);
                    bool has_table = false;
                    for (const auto& block : table_page.blocks) {
                        if (block.table) {
                            has_table = true;
                        }
                    }
                    if (!has_table) {
                        std::cout
                            << "image_layout_tests: FAIL - таблица не "
                               "распознана как таблица\n";
                        result = 1;
                    } else {
                        const auto xlsx = directory / "table.xlsx";
                        export_layout_page(table_page, xlsx);
                        std::cout << "image_layout_tests: table ok\n";
                    }
                }
            }
        }
        if (result == 0) {
            // Повёрнутый скан: автоповорот должен вернуть нормальный текст.
            const int width = image->width;
            const int height = image->height;
            std::vector<std::uint8_t> rotated(
                static_cast<std::size_t>(width) * height * 4);
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    const std::uint8_t* source = image->bgra.data() +
                        (static_cast<std::size_t>(y) * width + x) * 4;
                    std::uint8_t* target = rotated.data() +
                        (static_cast<std::size_t>(x) * height +
                         (height - 1 - y)) * 4;
                    std::memcpy(target, source, 4);
                }
            }
            int rotated_angle = 0;
            const auto rotated_layout = recognize_best_orientation(
                engine,
                rotated.data(),
                height,
                width,
                height * 4,
                4,
                &rotated_angle);
            const std::wstring rotated_text =
                page_text(build_layout_page(rotated_layout));
            if (rotated_angle == 0 ||
                rotated_text.find(L"Hello") == std::wstring::npos ||
                rotated_text.find(L"Привет") == std::wstring::npos) {
                std::cout << "image_layout_tests: FAIL - поворот не "
                             "распознан (angle=" << rotated_angle << "): "
                          << to_utf8(rotated_text) << "\n";
                result = 1;
            } else {
                std::cout << "image_layout_tests: rotated ok - angle="
                          << rotated_angle << "\n";
            }
        }
    } catch (const std::exception& error) {
        std::cout << "image_layout_tests: FAIL - " << error.what() << "\n";
        result = 1;
    }
    Gdiplus::GdiplusShutdown(token);
    std::filesystem::remove_all(directory);
    return result;
}
