#include "pdf_file.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "fpdf_text.h"
#include "fpdfview.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string>
#include <vector>

namespace offline_translator {
namespace {

std::once_flag g_pdfium_once;

void ensure_pdfium() {
    std::call_once(g_pdfium_once, []() { FPDF_InitLibrary(); });
}

struct MemoryFile {
    std::vector<std::uint8_t> data;
};

int read_block(
    void* param,
    unsigned long position,
    unsigned char* buffer,
    unsigned long size) {
    auto* file = static_cast<MemoryFile*>(param);
    if (position >= file->data.size()) {
        return 0;
    }
    const std::size_t available = file->data.size() - position;
    const std::size_t to_copy = std::min<std::size_t>(available, size);
    std::memcpy(buffer, file->data.data() + position, to_copy);
    return to_copy == size ? 1 : 1;
}

struct CharBox {
    int unicode{0};
    double left{0};
    double right{0};
    double top{0};
    double bottom{0};
    double size{0};
    bool bold{false};
    bool italic{false};
};

bool font_is_bold(const char* name) {
    std::string lowered(name);
    std::transform(
        lowered.begin(),
        lowered.end(),
        lowered.begin(),
        [](unsigned char symbol) {
            return static_cast<char>(std::tolower(symbol));
        });
    return lowered.find("bold") != std::string::npos ||
        lowered.find("-bd") != std::string::npos ||
        lowered.find("black") != std::string::npos;
}

bool font_is_italic(const char* name) {
    std::string lowered(name);
    std::transform(
        lowered.begin(),
        lowered.end(),
        lowered.begin(),
        [](unsigned char symbol) {
            return static_cast<char>(std::tolower(symbol));
        });
    return lowered.find("italic") != std::string::npos ||
        lowered.find("oblique") != std::string::npos;
}

std::string wide_to_utf8(const std::wstring& text) {
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

double median(std::vector<double> values) {
    if (values.empty()) {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
}

}  // анонимное пространство имён

struct PdfDocument::Impl {
    FPDF_DOCUMENT document{nullptr};
    MemoryFile file;

    ~Impl() {
        if (document) {
            FPDF_CloseDocument(document);
        }
    }
};

PdfDocument::PdfDocument(const std::filesystem::path& path)
    : impl_(new Impl()) {
    ensure_pdfium();
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return;
    }
    impl_->file.data.assign(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
    if (impl_->file.data.empty()) {
        return;
    }
    FPDF_FILEACCESS access{};
    access.m_FileLen = static_cast<unsigned long>(impl_->file.data.size());
    access.m_GetBlock = &read_block;
    access.m_Param = &impl_->file;
    impl_->document = FPDF_LoadCustomDocument(&access, nullptr);
}

PdfDocument::~PdfDocument() {
    delete impl_;
}

bool PdfDocument::valid() const {
    return impl_ && impl_->document != nullptr;
}

int PdfDocument::page_count() const {
    if (!impl_ || !impl_->document) {
        return 0;
    }
    return FPDF_GetPageCount(impl_->document);
}

OcrLayout PdfDocument::extract_page_layer(int page_index) {
    OcrLayout layout;
    if (!impl_ || !impl_->document) {
        return layout;
    }
    FPDF_PAGE page = FPDF_LoadPage(impl_->document, page_index);
    if (!page) {
        return layout;
    }
    const double page_width = FPDF_GetPageWidth(page);
    const double page_height = FPDF_GetPageHeight(page);
    layout.width = static_cast<int>(std::lround(page_width));
    layout.height = static_cast<int>(std::lround(page_height));

    FPDF_TEXTPAGE text = FPDFText_LoadPage(page);
    if (text) {
        const int count = FPDFText_CountChars(text);
        std::vector<CharBox> chars;
        chars.reserve(static_cast<std::size_t>(std::max(count, 0)));
        for (int index = 0; index < count; ++index) {
            const unsigned int code = FPDFText_GetUnicode(text, index);
            if (code == 0 || code == L' ' || code == L'\r' || code == L'\n' ||
                code == L'\t') {
                continue;
            }
            double left = 0;
            double right = 0;
            double bottom = 0;
            double top = 0;
            if (!FPDFText_GetCharBox(
                    text,
                    index,
                    &left,
                    &right,
                    &bottom,
                    &top)) {
                continue;
            }
            CharBox box;
            box.unicode = static_cast<int>(code);
            // PDF: начало координат снизу; переводим в top-down.
            box.left = left;
            box.right = right;
            box.top = page_height - top;
            box.bottom = page_height - bottom;
            box.size = FPDFText_GetFontSize(text, index);
            char name[128]{};
            int flags = 0;
            if (FPDFText_GetFontInfo(
                    text,
                    index,
                    name,
                    static_cast<unsigned long>(sizeof(name)),
                    &flags)) {
                box.bold = font_is_bold(name);
                box.italic = font_is_italic(name);
            }
            chars.push_back(box);
        }
        FPDFText_ClosePage(text);

        if (!chars.empty()) {
            std::vector<double> heights;
            for (const auto& box : chars) {
                heights.push_back(box.bottom - box.top);
            }
            const double font_height = std::max(median(heights), 1.0);
            // Строки: кластеризуем символы по вертикальному центру.
            std::vector<CharBox> ordered = chars;
            std::sort(
                ordered.begin(),
                ordered.end(),
                [](const CharBox& left, const CharBox& right) {
                    return (left.top + left.bottom) <
                        (right.top + right.bottom);
                });
            const double line_tolerance = font_height * 0.6;
            std::vector<std::vector<const CharBox*>> lines;
            for (const auto& box : ordered) {
                const double center = (box.top + box.bottom) / 2.0;
                bool placed = false;
                if (!lines.empty()) {
                    auto& last = lines.back();
                    const double last_center =
                        (last.front()->top + last.front()->bottom) / 2.0;
                    if (std::abs(center - last_center) <= line_tolerance) {
                        last.push_back(&box);
                        placed = true;
                    }
                }
                if (!placed) {
                    lines.push_back({&box});
                }
            }
            int paragraph = 0;
            for (const auto& line : lines) {
                std::vector<const CharBox*> sorted = line;
                std::sort(
                    sorted.begin(),
                    sorted.end(),
                    [](const CharBox* left, const CharBox* right) {
                        return left->left < right->left;
                    });
                OcrWordBox word;
                word.block = 1;
                word.paragraph = paragraph;
                word.line = 0;
                word.left = static_cast<int>(std::lround(sorted.front()->left));
                double right = sorted.front()->right;
                word.top = static_cast<int>(std::lround(sorted.front()->top));
                double bottom = sorted.front()->bottom;
                std::wstring text;
                bool bold = false;
                bool italic = false;
                double previous_right = 0;
                bool first = true;
                for (const CharBox* box : sorted) {
                    if (!first) {
                        const double gap = box->left - previous_right;
                        if (gap > font_height * 0.25) {
                            text += L' ';
                        }
                    }
                    const wchar_t symbol =
                        static_cast<wchar_t>(box->unicode);
                    text += symbol;
                    previous_right = box->right;
                    right = std::max(right, box->right);
                    bottom = std::max(bottom, box->bottom);
                    bold = bold || box->bold;
                    italic = italic || box->italic;
                    first = false;
                }
                word.text = wide_to_utf8(text);
                word.width = static_cast<int>(std::lround(right - word.left));
                word.height =
                    static_cast<int>(std::lround(bottom - word.top));
                word.confidence = 100.0;
                if (!word.text.empty() && word.width > 0 && word.height > 0) {
                    layout.words.push_back(std::move(word));
                }
                ++paragraph;
            }
        }
    }
    FPDF_ClosePage(page);
    return layout;
}

std::optional<PdfRaster> PdfDocument::render_page(
    int page_index,
    float scale) {
    if (!impl_ || !impl_->document) {
        return std::nullopt;
    }
    FPDF_PAGE page = FPDF_LoadPage(impl_->document, page_index);
    if (!page) {
        return std::nullopt;
    }
    const double page_width = FPDF_GetPageWidth(page);
    const double page_height = FPDF_GetPageHeight(page);
    int width = static_cast<int>(std::lround(page_width * scale));
    int height = static_cast<int>(std::lround(page_height * scale));
    width = std::max(width, 1);
    height = std::max(height, 1);
    FPDF_BITMAP bitmap = FPDFBitmap_CreateEx(
        width,
        height,
        FPDFBitmap_BGRA,
        nullptr,
        0);
    if (!bitmap) {
        FPDF_ClosePage(page);
        return std::nullopt;
    }
    FPDFBitmap_FillRect(bitmap, 0, 0, width, height, 0xFFFFFFFF);
    FPDF_RenderPageBitmap(
        bitmap,
        page,
        0,
        0,
        width,
        height,
        0,
        FPDF_ANNOT | FPDF_LCD_TEXT);
    const auto* buffer =
        static_cast<const std::uint8_t*>(FPDFBitmap_GetBuffer(bitmap));
    const int stride = FPDFBitmap_GetStride(bitmap);
    PdfRaster raster;
    raster.width = width;
    raster.height = height;
    raster.bgra.resize(
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);
    for (int y = 0; y < height; ++y) {
        std::memcpy(
            raster.bgra.data() + static_cast<std::size_t>(y) * width * 4,
            buffer + static_cast<std::size_t>(y) * stride,
            static_cast<std::size_t>(width) * 4);
    }
    FPDFBitmap_Destroy(bitmap);
    FPDF_ClosePage(page);
    return raster;
}

}  // пространство имён offline_translator
