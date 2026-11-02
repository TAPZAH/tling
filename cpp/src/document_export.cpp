#include "offline_translator/document_export.hpp"

#include "fs_utils.hpp"

#include <zlib.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace offline_translator {
namespace {

void append_u16(std::string& out, std::uint16_t value) {
    out.push_back(static_cast<char>(value & 0xFF));
    out.push_back(static_cast<char>((value >> 8) & 0xFF));
}

void append_u32(std::string& out, std::uint32_t value) {
    out.push_back(static_cast<char>(value & 0xFF));
    out.push_back(static_cast<char>((value >> 8) & 0xFF));
    out.push_back(static_cast<char>((value >> 16) & 0xFF));
    out.push_back(static_cast<char>((value >> 24) & 0xFF));
}

std::string xml_escape(const std::wstring& text) {
    std::string out;
    out.reserve(text.size());
    const auto append_codepoint = [&out](std::uint32_t code) {
        if (code < 0x80) {
            out.push_back(static_cast<char>(code));
        } else if (code < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (code >> 6)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        } else if (code < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (code >> 12)));
            out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (code >> 18)));
            out.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        }
    };
    for (std::size_t index = 0; index < text.size(); ++index) {
        const wchar_t symbol = text[index];
        switch (symbol) {
            case L'&':
                out += "&amp;";
                break;
            case L'<':
                out += "&lt;";
                break;
            case L'>':
                out += "&gt;";
                break;
            case L'"':
                out += "&quot;";
                break;
            case L'\'':
                out += "&apos;";
                break;
            default:
                if (symbol < 0x20 && symbol != L'\t' && symbol != L'\n') {
                    out += ' ';
                } else if (symbol >= 0xD800 && symbol <= 0xDBFF &&
                           index + 1 < text.size() &&
                           text[index + 1] >= 0xDC00 &&
                           text[index + 1] <= 0xDFFF) {
                    const std::uint32_t high =
                        static_cast<std::uint32_t>(symbol) - 0xD800;
                    const std::uint32_t low =
                        static_cast<std::uint32_t>(text[index + 1]) - 0xDC00;
                    append_codepoint(0x10000 + (high << 10) + low);
                    ++index;
                } else {
                    append_codepoint(static_cast<std::uint32_t>(symbol));
                }
                break;
        }
    }
    return out;
}

// ZIP с методом STORE: для OOXML достаточно, а CRC32 даёт zlib.
void write_zip_file(
    const std::filesystem::path& path,
    const std::vector<std::pair<std::string, std::string>>& entries) {
    std::string out;
    struct Central {
        std::string name;
        std::uint32_t crc{0};
        std::uint32_t size{0};
        std::uint32_t offset{0};
    };
    std::vector<Central> central;
    for (const auto& [name, data] : entries) {
        Central record;
        record.name = name;
        record.crc = static_cast<std::uint32_t>(::crc32(
            ::crc32(0L, Z_NULL, 0),
            reinterpret_cast<const Bytef*>(data.data()),
            static_cast<uInt>(data.size())));
        record.size = static_cast<std::uint32_t>(data.size());
        record.offset = static_cast<std::uint32_t>(out.size());
        append_u32(out, 0x04034B50);
        append_u16(out, 20);
        append_u16(out, 0);
        append_u16(out, 0);
        append_u16(out, 0);
        append_u16(out, 0x21);
        append_u32(out, record.crc);
        append_u32(out, record.size);
        append_u32(out, record.size);
        append_u16(out, static_cast<std::uint16_t>(name.size()));
        append_u16(out, 0);
        out += name;
        out += data;
        central.push_back(std::move(record));
    }
    const std::uint32_t central_offset = static_cast<std::uint32_t>(out.size());
    for (const auto& record : central) {
        append_u32(out, 0x02014B50);
        append_u16(out, 20);
        append_u16(out, 20);
        append_u16(out, 0);
        append_u16(out, 0);
        append_u16(out, 0);
        append_u16(out, 0x21);
        append_u32(out, record.crc);
        append_u32(out, record.size);
        append_u32(out, record.size);
        append_u16(out, static_cast<std::uint16_t>(record.name.size()));
        append_u16(out, 0);
        append_u16(out, 0);
        append_u16(out, 0);
        append_u16(out, 0);
        append_u32(out, 0);
        append_u32(out, record.offset);
        out += record.name;
    }
    const std::uint32_t central_size =
        static_cast<std::uint32_t>(out.size()) - central_offset;
    append_u32(out, 0x06054B50);
    append_u16(out, 0);
    append_u16(out, 0);
    append_u16(out, static_cast<std::uint16_t>(central.size()));
    append_u16(out, static_cast<std::uint16_t>(central.size()));
    append_u32(out, central_size);
    append_u32(out, central_offset);
    append_u16(out, 0);
    fs_utils::write_bytes(path, out.data(), out.size());
}

const char* kContentTypes =
    "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
    "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
    "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
    "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
    "%OVERRIDES%"
    "</Types>";

std::string content_types(const std::string& overrides) {
    std::string text(kContentTypes);
    const std::size_t position = text.find("%OVERRIDES%");
    if (position != std::string::npos) {
        text.replace(position, std::strlen("%OVERRIDES%"), overrides);
    }
    return text;
}

// --- Word ---

bool export_line_in_table(
    const std::vector<LayoutTable>& tables,
    int center_x,
    int center_y);

std::string paragraph_xml(
    const std::wstring& text,
    int font_half_points,
    int before_twips,
    const char* alignment,
    int indent_twips,
    bool bold,
    bool italic) {
    std::string out = "<w:p><w:pPr>";
    if (indent_twips > 0) {
        out += "<w:ind w:left=\"" + std::to_string(indent_twips) + "\"/>";
    }
    if (before_twips > 0) {
        out += "<w:spacing w:before=\"" + std::to_string(before_twips) +
            "\"/>";
    }
    if (alignment != nullptr) {
        out += std::string("<w:jc w:val=\"") + alignment + "\"/>";
    }
    out += "</w:pPr><w:r><w:rPr>";
    if (bold) {
        out += "<w:b/>";
    }
    if (italic) {
        out += "<w:i/>";
    }
    out += "<w:sz w:val=\"" + std::to_string(font_half_points) +
        "\"/></w:rPr>";
    std::size_t start = 0;
    while (true) {
        const std::size_t next = text.find(L'\n', start);
        const std::wstring line = text.substr(
            start,
            next == std::wstring::npos ? std::wstring::npos : next - start);
        if (!line.empty()) {
            out += "<w:t xml:space=\"preserve\">" + xml_escape(line) +
                "</w:t>";
        }
        if (next == std::wstring::npos) {
            break;
        }
        out += "<w:br/>";
        start = next + 1;
    }
    out += "</w:r></w:p>";
    return out;
}

std::string table_xml(
    const LayoutBlock& block,
    int font_half_points,
    int content_width,
    const std::vector<int>& column_widths) {
    const int columns = static_cast<int>(column_widths.size());
    int total = 0;
    for (const int width : column_widths) {
        total += width;
    }
    if (total <= 0 || columns <= 0) {
        return {};
    }
    std::vector<int> scaled(column_widths.size());
    for (std::size_t index = 0; index < column_widths.size(); ++index) {
        scaled[index] = std::max(
            200,
            static_cast<int>(
                static_cast<double>(column_widths[index]) / total *
                content_width));
    }
    std::string out =
        "<w:tbl><w:tblPr><w:tblW w:w=\"0\" w:type=\"auto\"/>"
        "<w:tblBorders>"
        "<w:top w:val=\"single\" w:sz=\"4\" w:color=\"808080\"/>"
        "<w:left w:val=\"single\" w:sz=\"4\" w:color=\"808080\"/>"
        "<w:bottom w:val=\"single\" w:sz=\"4\" w:color=\"808080\"/>"
        "<w:right w:val=\"single\" w:sz=\"4\" w:color=\"808080\"/>"
        "<w:insideH w:val=\"single\" w:sz=\"4\" w:color=\"808080\"/>"
        "<w:insideV w:val=\"single\" w:sz=\"4\" w:color=\"808080\"/>"
        "</w:tblBorders></w:tblPr><w:tblGrid>";
    for (const int width : scaled) {
        out += "<w:gridCol w:w=\"" + std::to_string(width) + "\"/>";
    }
    out += "</w:tblGrid>";
    for (const auto& line : block.lines) {
        out += "<w:tr>";
        for (int column = 0; column < columns; ++column) {
            LayoutCell cell;
            if (column < static_cast<int>(line.cells.size())) {
                cell = line.cells[static_cast<std::size_t>(column)];
            }
            out += "<w:tc><w:tcPr><w:tcW w:w=\"" +
                std::to_string(scaled[static_cast<std::size_t>(column)]) +
                "\" w:type=\"dxa\"/></w:tcPr>" +
                paragraph_xml(
                    cell.text,
                    font_half_points,
                    0,
                    nullptr,
                    0,
                    cell.bold,
                    cell.italic) +
                "</w:tc>";
        }
        out += "</w:tr>";
    }
    out += "</w:tbl>";
    return out;
}

std::string table_grid_xml(const LayoutTable& table, double scale) {
    const int columns = static_cast<int>(table.columns.size()) - 1;
    const int rows = static_cast<int>(table.rows.size()) - 1;
    if (columns <= 0 || rows <= 0) {
        return {};
    }
    const auto clamp_span = [columns](int column, int span) {
        return std::clamp(span, 1, columns - column);
    };
    std::vector<int> widths(static_cast<std::size_t>(columns));
    for (int column = 0; column < columns; ++column) {
        widths[static_cast<std::size_t>(column)] = std::max(
            200,
            static_cast<int>(std::lround(
                (table.columns[static_cast<std::size_t>(column + 1)] -
                 table.columns[static_cast<std::size_t>(column)]) *
                scale)));
    }
    std::string out =
        "<w:tbl><w:tblPr><w:tblW w:w=\"0\" w:type=\"auto\"/>"
        "<w:tblBorders>"
        "<w:top w:val=\"single\" w:sz=\"4\" w:color=\"808080\"/>"
        "<w:left w:val=\"single\" w:sz=\"4\" w:color=\"808080\"/>"
        "<w:bottom w:val=\"single\" w:sz=\"4\" w:color=\"808080\"/>"
        "<w:right w:val=\"single\" w:sz=\"4\" w:color=\"808080\"/>"
        "<w:insideH w:val=\"single\" w:sz=\"4\" w:color=\"808080\"/>"
        "<w:insideV w:val=\"single\" w:sz=\"4\" w:color=\"808080\"/>"
        "</w:tblBorders></w:tblPr><w:tblGrid>";
    for (const int width : widths) {
        out += "<w:gridCol w:w=\"" + std::to_string(width) + "\"/>";
    }
    out += "</w:tblGrid>";
    const auto find_text = [&table](int row, int column) {
        for (const auto& text : table.texts) {
            if (text.row == row && text.column == column) {
                return &text;
            }
        }
        return static_cast<const LayoutTableText*>(nullptr);
    };
    const auto find_vertical_source = [&table](int row, int column) {
        for (const auto& text : table.texts) {
            if (text.column == column && text.row < row &&
                text.row + text.row_span > row) {
                return &text;
            }
        }
        return static_cast<const LayoutTableText*>(nullptr);
    };
    for (int row = 0; row < rows; ++row) {
        out += "<w:tr>";
        int column = 0;
        while (column < columns) {
            const LayoutTableText* text = find_text(row, column);
            const LayoutTableText* continued = text != nullptr
                ? nullptr
                : find_vertical_source(row, column);
            const int span = clamp_span(
                column,
                text != nullptr
                    ? text->column_span
                    : (continued != nullptr ? continued->column_span : 1));
            int width = 0;
            for (int c = column;
                 c < column + span && c < columns;
                 ++c) {
                width += widths[static_cast<std::size_t>(c)];
            }
            std::string cell = "<w:tcPr><w:tcW w:w=\"" +
                std::to_string(width) + "\" w:type=\"dxa\"/>";
            if (span > 1) {
                cell += "<w:gridSpan w:val=\"" + std::to_string(span) +
                    "\"/>";
            }
            if (text != nullptr && text->row_span > 1) {
                cell += "<w:vMerge w:val=\"restart\"/>";
            } else if (continued != nullptr) {
                cell += "<w:vMerge/>";
            }
            cell += "</w:tcPr>";
            std::string paragraph = "<w:p/>";
            if (text != nullptr) {
                const int font_half_points = std::clamp(
                    static_cast<int>(std::lround(
                        text->font_height * scale / 10.0)),
                    16,
                    44);
                paragraph = paragraph_xml(
                    text->text,
                    font_half_points,
                    0,
                    nullptr,
                    0,
                    text->bold,
                    text->italic);
            }
            out += "<w:tc>" + cell + paragraph + "</w:tc>";
            column += span;
        }
        out += "</w:tr>";
    }
    out += "</w:tbl>";
    return out;
}

std::string build_docx_body(
    const LayoutPage& page,
    double scale,
    int margin_left,
    int content_width,
    int image_width) {
    std::string body;
    int first_table_top = 1 << 30;
    for (const auto& table : page.tables) {
        if (!table.rows.empty()) {
            first_table_top = std::min(first_table_top, table.rows.front());
        }
    }
    const auto emit_blocks = [&](bool above_table) {
    int previous_bottom = -1;
    for (const auto& block : page.blocks) {
        if ((block.top < first_table_top) != above_table) {
            continue;
        }
        int font_half_points = 22;
        if (!block.lines.empty()) {
            const int line_height = std::max(block.lines.front().font_height, 1);
            font_half_points = std::clamp(
                static_cast<int>(line_height * scale / 10.0),
                18,
                44);
        }
        if (block.table && page.tables.empty()) {
            std::vector<int> widths;
            const std::size_t columns = block.lines.empty()
                ? 0
                : block.lines.front().cells.size();
            widths.assign(columns, 0);
            for (const auto& line : block.lines) {
                for (std::size_t column = 0; column < line.cells.size() &&
                     column < columns; ++column) {
                    widths[column] = std::max(
                        widths[column],
                        line.cells[column].right - line.cells[column].left);
                }
            }
            body += table_xml(
                block,
                font_half_points,
                content_width,
                widths);
            previous_bottom = block.bottom;
            continue;
        }
        for (const auto& line : block.lines) {
            if (export_line_in_table(
                    page.tables,
                    (line.left + line.right) / 2,
                    (line.top + line.bottom) / 2)) {
                continue;
            }
            const LayoutCell cell = line.cells.empty()
                ? LayoutCell{}
                : line.cells.front();
            const int line_width = line.right - line.left;
            const char* alignment = nullptr;
            const int center = (line.left + line.right) / 2;
            if (line_width < image_width * 3 / 5 &&
                std::abs(center - image_width / 2) < image_width / 16) {
                alignment = "center";
            } else if (
                line.left > image_width * 2 / 5 &&
                std::abs(line.right - image_width) < image_width / 16) {
                alignment = "right";
            }
            int before = 0;
            if (previous_bottom >= 0 && line.top > previous_bottom + 4) {
                before = std::clamp(
                    static_cast<int>((line.top - previous_bottom) * scale),
                    0,
                    600);
            }
            int indent = 0;
            if (alignment == nullptr) {
                indent = std::clamp(
                    static_cast<int>((line.left - margin_left / scale) * scale),
                    0,
                    content_width / 2);
            }
            body += paragraph_xml(
                cell.text,
                font_half_points,
                before,
                alignment,
                indent,
                cell.bold,
                cell.italic);
            previous_bottom = line.bottom;
        }
    }
    };
    emit_blocks(true);
    for (const auto& table : page.tables) {
        body += table_grid_xml(table, scale);
    }
    emit_blocks(false);
    return body;
}

// Word не открывает документ, где таблица идёт сразу за таблицей или
// стоит последним элементом body: после каждой таблицы обязателен абзац.
void ensure_paragraphs_after_tables(std::string* body) {
    const std::string adjacent = "</w:tbl><w:tbl>";
    for (std::size_t at = body->find(adjacent);
         at != std::string::npos;
         at = body->find(adjacent, at + adjacent.size() + 6)) {
        body->insert(at + 8, "<w:p/>");
    }
    const std::string closing = "</w:tbl>";
    if (body->size() >= closing.size() &&
        body->compare(body->size() - closing.size(), closing.size(), closing) ==
            0) {
        *body += "<w:p/>";
    }
}

void export_docx(
    const std::vector<LayoutPage>& pages,
    const std::filesystem::path& path) {
    const LayoutPage empty;
    const LayoutPage& first = pages.empty() ? empty : pages.front();
    const int image_width = std::max(first.width, 1);
    const int page_width_twips = 11906;  // A4
    double page_height_twips = static_cast<double>(page_width_twips) *
        std::max(first.height, 1) / image_width;
    page_height_twips = std::clamp(page_height_twips, 6000.0, 20000.0);
    const double scale =
        static_cast<double>(page_width_twips) / image_width;

    int min_left = page_width_twips;
    for (const auto& block : first.blocks) {
        min_left = std::min(min_left, block.left);
    }
    if (min_left == page_width_twips) {
        min_left = 0;
    }
    const int margin_left =
        std::clamp(static_cast<int>(min_left * scale), 567, 2268);
    const int margin_top = 567;
    const int margin_right = 567;
    const int margin_bottom = 567;
    const int content_width =
        page_width_twips - margin_left - margin_right;

    std::string body;
    if (pages.empty()) {
        body = paragraph_xml(L"", 22, 0, nullptr, 0, false, false);
    } else {
        for (std::size_t index = 0; index < pages.size(); ++index) {
            if (index > 0) {
                body +=
                    "<w:p><w:r><w:br w:type=\"page\"/></w:r></w:p>";
            }
            body += build_docx_body(
                pages[index],
                scale,
                margin_left,
                content_width,
                image_width);
        }
    }
    ensure_paragraphs_after_tables(&body);
    const std::string section =
        "<w:sectPr><w:pgSz w:w=\"" + std::to_string(page_width_twips) +
        "\" w:h=\"" + std::to_string(static_cast<int>(page_height_twips)) +
        "\"/><w:pgMar w:top=\"" + std::to_string(margin_top) +
        "\" w:right=\"" + std::to_string(margin_right) +
        "\" w:bottom=\"" + std::to_string(margin_bottom) +
        "\" w:left=\"" + std::to_string(margin_left) +
        "\"/></w:sectPr>";
    const std::string document =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<w:document xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">"
        "<w:body>" + body + section + "</w:body></w:document>";
    const std::string styles =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<w:styles xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">"
        "<w:docDefaults><w:rPrDefault><w:rPr>"
        "<w:rFonts w:ascii=\"Calibri\" w:hAnsi=\"Calibri\" w:eastAsia=\"Microsoft YaHei\" w:cs=\"Calibri\"/>"
        "<w:sz w:val=\"22\"/></w:rPr></w:rPrDefault></w:docDefaults>"
        "</w:styles>";
    const std::string rels =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"word/document.xml\"/>"
        "</Relationships>";
    const std::string document_rels =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles\" Target=\"styles.xml\"/>"
        "</Relationships>";
    const std::string overrides =
        "<Override PartName=\"/word/document.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml\"/>"
        "<Override PartName=\"/word/styles.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.styles+xml\"/>";
    write_zip_file(path, {
        {"[Content_Types].xml", content_types(overrides)},
        {"_rels/.rels", rels},
        {"word/document.xml", document},
        {"word/styles.xml", styles},
        {"word/_rels/document.xml.rels", document_rels},
    });
}

// --- Excel ---

std::string column_name(int index) {
    std::string name;
    ++index;
    while (index > 0) {
        const int remainder = (index - 1) % 26;
        name.insert(name.begin(), static_cast<char>('A' + remainder));
        index = (index - 1) / 26;
    }
    return name;
}

bool numeric_cell_text(const std::wstring& text) {
    if (text.empty()) {
        return false;
    }
    std::size_t index = 0;
    if (text[0] == L'-' || text[0] == L'+') {
        ++index;
    }
    bool digit = false;
    bool dot = false;
    for (; index < text.size(); ++index) {
        const wchar_t symbol = text[index];
        if (symbol >= L'0' && symbol <= L'9') {
            digit = true;
            continue;
        }
        if (symbol == L'.' && !dot) {
            dot = true;
            continue;
        }
        return false;
    }
    if (!digit) {
        return false;
    }
    const std::size_t start = text[0] == L'-' || text[0] == L'+' ? 1 : 0;
    // Ведущие нули (ИНН, коды) — только текстом.
    if (start + 1 < text.size() && text[start] == L'0' &&
        text[start + 1] != L'.') {
        return false;
    }
    return true;
}

std::string ascii_number(const std::wstring& text) {
    std::string value;
    value.reserve(text.size());
    for (const wchar_t symbol : text) {
        if (symbol >= L'0' && symbol <= L'9') {
            value.push_back(static_cast<char>(symbol));
        } else if (symbol == L'-') {
            value.push_back('-');
        } else if (symbol == L'.') {
            value.push_back('.');
        }
    }
    return value;
}

std::string xlsx_cell(
    const std::string& reference,
    const std::wstring& text,
    int style) {
    if (numeric_cell_text(text)) {
        return "<c r=\"" + reference + "\" s=\"" + std::to_string(style) +
            "\"><v>" + ascii_number(text) + "</v></c>";
    }
    return "<c r=\"" + reference + "\" t=\"inlineStr\" s=\"" +
        std::to_string(style) + "\"><is><t xml:space=\"preserve\">" +
        xml_escape(text) + "</t></is></c>";
}

bool export_line_in_table(
    const std::vector<LayoutTable>& tables,
    int center_x,
    int center_y) {
    for (const auto& table : tables) {
        if (table.columns.empty() || table.rows.empty()) {
            continue;
        }
        if (center_x >= table.columns.front() &&
            center_x <= table.columns.back() &&
            center_y >= table.rows.front() &&
            center_y <= table.rows.back()) {
            return true;
        }
    }
    return false;
}

void export_xlsx(
    const std::vector<LayoutPage>& pages,
    const std::filesystem::path& path) {
    std::string rows_xml;
    std::string merges_xml;
    int merge_count = 0;
    std::vector<double> column_widths;
    int next_row = 1;
    const auto ensure_column = [&](std::size_t index, double width) {
        if (column_widths.size() <= index) {
            column_widths.resize(index + 1, 8.0);
        }
        column_widths[index] = std::max(column_widths[index], width);
    };
    for (const auto& page : pages) {
        int first_table_top = 1 << 30;
        for (const auto& table : page.tables) {
            if (!table.rows.empty()) {
                first_table_top = std::min(first_table_top, table.rows.front());
            }
        }
        const auto emit_blocks = [&](bool above_table) {
            for (const auto& block : page.blocks) {
                for (const auto& line : block.lines) {
                    if (export_line_in_table(
                            page.tables,
                            (line.left + line.right) / 2,
                            (line.top + line.bottom) / 2)) {
                        continue;
                    }
                    if ((line.top < first_table_top) != above_table) {
                        continue;
                    }
                    rows_xml += "<row r=\"" + std::to_string(next_row) + "\">";
                    const std::size_t cells =
                        line.cells.empty() ? 1 : line.cells.size();
                    for (std::size_t column = 0; column < cells; ++column) {
                        const std::wstring text = line.cells.empty()
                            ? std::wstring{}
                            : line.cells[column].text;
                        ensure_column(
                            column,
                            std::clamp(
                                static_cast<double>(text.size()) * 1.1 + 2.0,
                                8.0,
                                90.0));
                        const std::string reference =
                            column_name(static_cast<int>(column)) +
                            std::to_string(next_row);
                        rows_xml += xlsx_cell(reference, text, 1);
                    }
                    rows_xml += "</row>";
                    ++next_row;
                }
            }
        };
        emit_blocks(true);
        for (const auto& table : page.tables) {
            if (table.texts.empty() || table.columns.size() < 2 ||
                table.rows.size() < 2) {
                continue;
            }
            const int columns = static_cast<int>(table.columns.size()) - 1;
            const int table_rows = static_cast<int>(table.rows.size()) - 1;
            for (int column = 0; column < columns; ++column) {
                const double pixels = static_cast<double>(
                    table.columns[static_cast<std::size_t>(column + 1)] -
                    table.columns[static_cast<std::size_t>(column)]);
                ensure_column(
                    static_cast<std::size_t>(column),
                    std::clamp(pixels / 7.0, 8.0, 120.0));
            }
            for (int row = 0; row < table_rows; ++row) {
                const int excel_row = next_row + row;
                const int height = std::clamp(
                    static_cast<int>(std::lround(
                        (table.rows[static_cast<std::size_t>(row + 1)] -
                         table.rows[static_cast<std::size_t>(row)]) *
                        0.75)),
                    12,
                    300);
                std::string row_cells;
                std::vector<const LayoutTableText*> row_texts;
                for (const auto& text : table.texts) {
                    if (text.row == row) {
                        row_texts.push_back(&text);
                    }
                }
                std::sort(
                    row_texts.begin(),
                    row_texts.end(),
                    [](const LayoutTableText* left, const LayoutTableText* right) {
                        return left->column < right->column;
                    });
                for (const LayoutTableText* text : row_texts) {
                    const std::string reference =
                        column_name(text->column) + std::to_string(excel_row);
                    row_cells += xlsx_cell(reference, text->text, 2);
                    if (text->column_span > 1 || text->row_span > 1) {
                        const std::string last =
                            column_name(text->column + text->column_span - 1) +
                            std::to_string(excel_row + text->row_span - 1);
                        merges_xml += "<mergeCell ref=\"" + reference + ":" +
                            last + "\"/>";
                        ++merge_count;
                    }
                }
                if (row_cells.empty()) {
                    continue;
                }
                rows_xml += "<row r=\"" + std::to_string(excel_row) +
                    "\" ht=\"" + std::to_string(height) +
                    "\" customHeight=\"1\">" + row_cells + "</row>";
            }
            next_row += table_rows + 1;
        }
        emit_blocks(false);
        if (!pages.empty()) {
            ++next_row;  // пустая строка между страницами
        }
    }
    std::string cols_xml;
    for (std::size_t index = 0; index < column_widths.size(); ++index) {
        cols_xml += "<col min=\"" + std::to_string(index + 1) +
            "\" max=\"" + std::to_string(index + 1) + "\" width=\"" +
            std::to_string(column_widths[index]).substr(0, 5) +
            "\" customWidth=\"1\"/>";
    }
    const std::string sheet =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">"
        "<sheetViews><sheetView workbookViewId=\"0\"/></sheetViews>" +
        (cols_xml.empty() ? std::string{}
                          : "<cols>" + cols_xml + "</cols>") +
        "<sheetData>" + rows_xml + "</sheetData>" +
        (merge_count > 0
             ? "<mergeCells count=\"" + std::to_string(merge_count) + "\">" +
                 merges_xml + "</mergeCells>"
             : std::string{}) +
        "</worksheet>";
    const std::string workbook =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<workbook xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" "
        "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">"
        "<sheets><sheet name=\"Перевод\" sheetId=\"1\" r:id=\"rId1\"/></sheets>"
        "</workbook>";
    const std::string styles =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<styleSheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">"
        "<fonts count=\"1\"><font><sz val=\"11\"/><name val=\"Calibri\"/></font></fonts>"
        "<fills count=\"1\"><fill><patternFill patternType=\"none\"/></fill></fills>"
        "<borders count=\"2\"><border/>"
        "<border><left style=\"thin\"><color rgb=\"FF808080\"/></left>"
        "<right style=\"thin\"><color rgb=\"FF808080\"/></right>"
        "<top style=\"thin\"><color rgb=\"FF808080\"/></top>"
        "<bottom style=\"thin\"><color rgb=\"FF808080\"/></bottom>"
        "<diagonal/></border></borders>"
        "<cellStyleXfs count=\"1\"><xf/></cellStyleXfs>"
        "<cellXfs count=\"3\"><xf xfId=\"0\"/><xf xfId=\"0\" applyAlignment=\"1\">"
        "<alignment wrapText=\"1\" vertical=\"top\"/></xf>"
        "<xf xfId=\"0\" applyAlignment=\"1\" applyBorder=\"1\" borderId=\"1\">"
        "<alignment wrapText=\"1\" vertical=\"top\"/></xf></cellXfs>"
        "</styleSheet>";
    const std::string rels =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"xl/workbook.xml\"/>"
        "</Relationships>";
    const std::string workbook_rels =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet\" Target=\"worksheets/sheet1.xml\"/>"
        "<Relationship Id=\"rId2\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles\" Target=\"styles.xml\"/>"
        "</Relationships>";
    const std::string overrides =
        "<Override PartName=\"/xl/workbook.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml\"/>"
        "<Override PartName=\"/xl/worksheets/sheet1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml\"/>"
        "<Override PartName=\"/xl/styles.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml\"/>";
    write_zip_file(path, {
        {"[Content_Types].xml", content_types(overrides)},
        {"_rels/.rels", rels},
        {"xl/workbook.xml", workbook},
        {"xl/_rels/workbook.xml.rels", workbook_rels},
        {"xl/worksheets/sheet1.xml", sheet},
        {"xl/styles.xml", styles},
    });
}

std::string lower_extension(const std::filesystem::path& path) {
    std::string extension = path.extension().string();
    std::transform(
        extension.begin(),
        extension.end(),
        extension.begin(),
        [](unsigned char symbol) {
            return static_cast<char>(std::tolower(symbol));
        });
    return extension;
}

}  // анонимное пространство имён

void export_layout_document(
    const std::vector<LayoutPage>& pages,
    const std::filesystem::path& path) {
    if (lower_extension(path) == ".xlsx") {
        export_xlsx(pages, path);
    } else {
        export_docx(pages, path);
    }
}

void export_layout_page(
    const LayoutPage& page,
    const std::filesystem::path& path) {
    export_layout_document(std::vector<LayoutPage>{page}, path);
}

}  // пространство имён offline_translator
