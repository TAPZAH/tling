#include "offline_translator/document_export.hpp"
#include "offline_translator/document_layout.hpp"
#include "offline_translator/excel_cells.hpp"

#include "pdf_file.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

void append_object(
    std::string& pdf,
    std::vector<std::size_t>& offsets,
    int index,
    const std::string& body) {
    offsets[static_cast<std::size_t>(index)] = pdf.size();
    pdf += std::to_string(index) + " 0 obj\n" + body + "\nendobj\n";
}

std::string build_sample_pdf() {
    std::string pdf = "%PDF-1.4\n";
    std::vector<std::size_t> offsets(6, 0);
    append_object(pdf, offsets, 1, "<< /Type /Catalog /Pages 2 0 R >>");
    append_object(
        pdf,
        offsets,
        2,
        "<< /Type /Pages /Kids [3 0 R] /Count 1 >>");
    append_object(
        pdf,
        offsets,
        3,
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] "
        "/Contents 4 0 R /Resources << /Font << /F1 5 0 R >> >> >>");
    const std::string stream =
        "BT /F1 24 Tf 72 700 Td (Hello world) Tj ET";
    append_object(
        pdf,
        offsets,
        4,
        "<< /Length " + std::to_string(stream.size()) + " >>\nstream\n" +
            stream + "\nendstream");
    append_object(
        pdf,
        offsets,
        5,
        "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>");
    const std::size_t xref = pdf.size();
    pdf += "xref\n0 6\n";
    pdf += "0000000000 65535 f \n";
    for (int index = 1; index <= 5; ++index) {
        std::string offset =
            std::to_string(offsets[static_cast<std::size_t>(index)]);
        while (offset.size() < 10) {
            offset.insert(offset.begin(), '0');
        }
        pdf += offset + " 00000 n \n";
    }
    pdf += "trailer\n<< /Size 6 /Root 1 0 R >>\nstartxref\n" +
        std::to_string(xref) + "\n%%EOF\n";
    return pdf;
}

}  // анонимное пространство имён

int main(int argc, char** argv) {
    using namespace offline_translator;
    // Диагностика: pdf_tests --file <pdf>.
    if (argc >= 3 && std::string(argv[1]) == "--file") {
        const std::filesystem::path input(argv[2]);
        PdfDocument document(input);
        if (!document.valid()) {
            std::cout << "file: PDF не открылся\n";
            return 1;
        }
        std::cout << "file: pages=" << document.page_count() << "\n";
        std::size_t total_words = 0;
        for (int index = 0; index < document.page_count(); ++index) {
            const auto layer = document.extract_page_layer(index);
            total_words += layer.words.size();
            std::cout << "  page " << index << " " << layer.width << "x"
                      << layer.height << " words=" << layer.words.size()
                      << "\n";
        }
        std::cout << "file: total_words=" << total_words << "\n";
        if (document.page_count() > 0 && tessdata_installed("en")) {
            auto raster = document.render_page(0, 2.0f);
            if (raster) {
                OcrEngine engine;
                std::vector<std::string> codes{"en"};
                if (tessdata_installed("ru")) {
                    codes.push_back("ru");
                }
                engine.initialize_multi(codes);
                int angle = 0;
                const auto layout = recognize_best_orientation(
                    engine,
                    raster->bgra.data(),
                    raster->width,
                    raster->height,
                    raster->width * 4,
                    4,
                    &angle);
                const auto page = build_layout_page(layout);
                int tables = 0;
                std::size_t max_cells = 0;
                for (const auto& block : page.blocks) {
                    if (block.table) {
                        ++tables;
                    }
                    for (const auto& line : block.lines) {
                        max_cells = std::max(max_cells, line.cells.size());
                    }
                }
                std::cout << "file: scanned page0 angle=" << angle
                          << " words=" << layout.words.size()
                          << " tables=" << tables
                          << " max_cells=" << max_cells << "\n";
                if (argc >= 4) {
                    const int want = std::atoi(argv[3]);
                    auto dump_raster = document.render_page(want, 2.0f);
                    if (dump_raster) {
                        const auto dump_layout = recognize_best_orientation(
                            engine,
                            dump_raster->bgra.data(),
                            dump_raster->width,
                            dump_raster->height,
                            dump_raster->width * 4,
                            4,
                            &angle);
                        const auto dump_page = build_layout_page(dump_layout);
                        std::cout << "--- page " << want << " cells ---\n";
                        for (const auto& block : dump_page.blocks) {
                            std::cout << (block.table ? "[table] " : "[text] ")
                                      << block.lines.size() << " lines\n";
                            for (const auto& line : block.lines) {
                                for (const auto& cell : line.cells) {
                                    const std::string utf8 = [&] {
                                        if (cell.text.empty()) {
                                            return std::string{};
                                        }
                                        const int size = WideCharToMultiByte(
                                            CP_UTF8, 0, cell.text.data(),
                                            static_cast<int>(cell.text.size()),
                                            nullptr, 0, nullptr, nullptr);
                                        std::string out(
                                            static_cast<std::size_t>(size), '\0');
                                        WideCharToMultiByte(
                                            CP_UTF8, 0, cell.text.data(),
                                            static_cast<int>(cell.text.size()),
                                            out.data(), size, nullptr, nullptr);
                                        return out;
                                    }();
                                    std::cout << (excel_cell_translatable(cell.text)
                                            ? "  T "
                                            : "  - ")
                                              << utf8 << "\n";
                                }
                            }
                        }
                    }
                }
            }
        }
        return 0;
    }
    const auto directory = std::filesystem::temp_directory_path() /
        "offline-translator-pdf-test";
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);
    const auto pdf_path = directory / "sample.pdf";
    {
        std::ofstream out(pdf_path, std::ios::binary);
        const std::string pdf = build_sample_pdf();
        out.write(pdf.data(), static_cast<std::streamsize>(pdf.size()));
    }

    PdfDocument document(pdf_path);
    if (!document.valid()) {
        std::cout << "pdf_tests: FAIL - PDF не открылся\n";
        return 1;
    }
    if (document.page_count() != 1) {
        std::cout << "pdf_tests: FAIL - неверное число страниц\n";
        return 1;
    }
    const auto layer = document.extract_page_layer(0);
    if (layer.words.empty()) {
        std::cout << "pdf_tests: FAIL - текстовый слой пуст\n";
        return 1;
    }
    std::string joined;
    for (const auto& word : layer.words) {
        joined += word.text;
        joined += ' ';
    }
    if (joined.find("Hello") == std::string::npos ||
        joined.find("world") == std::string::npos) {
        std::cout << "pdf_tests: FAIL - текст не извлечён: " << joined
                  << "\n";
        return 1;
    }
    const auto raster = document.render_page(0, 2.0f);
    if (!raster || raster->bgra.empty() || raster->width <= 0) {
        std::cout << "pdf_tests: FAIL - страница не отрисована\n";
        return 1;
    }
    const auto page = build_layout_page(layer);
    const auto docx = directory / "out.docx";
    const auto xlsx = directory / "out.xlsx";
    export_layout_page(page, docx);
    export_layout_page(page, xlsx);
    if (!std::filesystem::is_regular_file(docx) ||
        !std::filesystem::is_regular_file(xlsx)) {
        std::cout << "pdf_tests: FAIL - документы не созданы\n";
        return 1;
    }
    std::cout << "pdf_tests: ok - " << joined << "\n";
    std::filesystem::remove_all(directory);
    return 0;
}
