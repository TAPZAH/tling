#pragma once

#include "offline_translator/ocr_engine.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

namespace offline_translator {

struct PdfRaster {
    std::vector<std::uint8_t> bgra;
    int width{0};
    int height{0};
};

// Обёртка PDFium: текстовый слой с координатами (в пунктах) и рендер
// страниц в BGRA. Один документ — один поток.
class PdfDocument {
public:
    explicit PdfDocument(const std::filesystem::path& path);
    ~PdfDocument();
    PdfDocument(const PdfDocument&) = delete;
    PdfDocument& operator=(const PdfDocument&) = delete;

    bool valid() const;
    int page_count() const;
    // Текстовый слой страницы в пунктах, top-down. Пусто, если это скан.
    OcrLayout extract_page_layer(int page_index);
    std::optional<PdfRaster> render_page(int page_index, float scale);

private:
    struct Impl;
    Impl* impl_{nullptr};
};

}  // пространство имён offline_translator
