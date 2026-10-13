#include "offline_translator/excel_cells.hpp"

#include "offline_translator/selection.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace offline_translator {
namespace {

std::string narrow_utf8(std::wstring_view text) {
#ifdef _WIN32
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
#else
    std::string result;
    result.reserve(text.size());
    for (const wchar_t symbol : text) {
        result.push_back(
            symbol < 0x80 ? static_cast<char>(symbol) : '?');
    }
    return result;
#endif
}

void trim_trailing(std::wstring& text) {
    while (!text.empty() &&
           (text.back() == L' ' || text.back() == L'\t' ||
            text.back() == L'\r' || text.back() == L'\n')) {
        text.pop_back();
    }
}

bool needs_quoting(std::wstring_view cell) {
    for (const wchar_t symbol : cell) {
        if (symbol == L'\t' || symbol == L'\n' || symbol == L'\r' ||
            symbol == L'"') {
            return true;
        }
    }
    return false;
}

}  // анонимное пространство имён

bool ExcelGrid::empty() const {
    for (const auto& row : rows) {
        for (const auto& cell : row) {
            if (!cell.empty()) {
                return false;
            }
        }
    }
    return true;
}

std::size_t ExcelGrid::cell_count() const {
    std::size_t count = 0;
    for (const auto& row : rows) {
        count += row.size();
    }
    return count;
}

std::size_t ExcelGrid::translatable_count() const {
    std::size_t count = 0;
    for (const auto& row : rows) {
        for (const auto& cell : row) {
            if (excel_cell_translatable(cell)) {
                ++count;
            }
        }
    }
    return count;
}

ExcelGrid parse_excel_tsv(std::wstring_view text) {
    ExcelGrid grid;
    std::vector<std::wstring> row;
    std::wstring cell;
    bool in_quotes = false;
    bool row_dirty = false;
    for (std::size_t index = 0; index < text.size(); ++index) {
        const wchar_t symbol = text[index];
        if (in_quotes) {
            if (symbol == L'"') {
                if (index + 1 < text.size() && text[index + 1] == L'"') {
                    cell += L'"';
                    ++index;
                } else {
                    in_quotes = false;
                }
            } else {
                cell += symbol;
            }
            continue;
        }
        if (symbol == L'"' && cell.empty()) {
            in_quotes = true;
            row_dirty = true;
        } else if (symbol == L'\t') {
            row.push_back(std::move(cell));
            cell.clear();
            row_dirty = true;
        } else if (symbol == L'\r' || symbol == L'\n') {
            if (symbol == L'\r' && index + 1 < text.size() &&
                text[index + 1] == L'\n') {
                ++index;
            }
            row.push_back(std::move(cell));
            cell.clear();
            grid.rows.push_back(std::move(row));
            row.clear();
            row_dirty = false;
        } else {
            cell += symbol;
            row_dirty = true;
        }
    }
    if (row_dirty || !cell.empty() || !row.empty()) {
        row.push_back(std::move(cell));
        grid.rows.push_back(std::move(row));
    }
    return grid;
}

std::wstring serialize_excel_tsv(const ExcelGrid& grid) {
    std::wstring out;
    for (std::size_t row_index = 0; row_index < grid.rows.size();
         ++row_index) {
        const auto& row = grid.rows[row_index];
        for (std::size_t cell_index = 0; cell_index < row.size();
             ++cell_index) {
            const std::wstring& cell = row[cell_index];
            if (needs_quoting(cell)) {
                out += L'"';
                for (const wchar_t symbol : cell) {
                    if (symbol == L'"') {
                        out += L'"';
                    }
                    out += symbol;
                }
                out += L'"';
            } else {
                out += cell;
            }
            if (cell_index + 1 < row.size()) {
                out += L'\t';
            }
        }
        if (row_index + 1 < grid.rows.size()) {
            out += L"\r\n";
        }
    }
    return out;
}

bool excel_cell_translatable(std::wstring_view cell) {
    std::wstring trimmed(cell);
    trim_trailing(trimmed);
    std::size_t begin = 0;
    while (begin < trimmed.size() &&
           (trimmed[begin] == L' ' || trimmed[begin] == L'\t')) {
        ++begin;
    }
    if (begin > 0) {
        trimmed.erase(0, begin);
    }
    if (trimmed.empty()) {
        return false;
    }
    return !detect_script_language(narrow_utf8(trimmed)).empty();
}

std::wstring excel_combined_cell(
    std::wstring_view original,
    std::wstring_view translation,
    std::wstring_view separator) {
    std::wstring base(original);
    trim_trailing(base);
    if (base.empty()) {
        return std::wstring(translation);
    }
    if (translation.empty()) {
        return base;
    }
    std::wstring combined = base;
    combined += separator;
    combined += translation;
    return combined;
}

bool excel_selection_supported(std::size_t cells, std::size_t translatable) {
    if (cells == 0 || cells > kExcelMaxCells) {
        return false;
    }
    if (translatable == 0 || translatable > kExcelMaxTranslatableCells) {
        return false;
    }
    return true;
}

}  // пространство имён offline_translator
