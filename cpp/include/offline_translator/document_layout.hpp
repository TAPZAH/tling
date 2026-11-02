#pragma once

#include "offline_translator/ocr_engine.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace offline_translator {

// Ячейка строки: для таблиц — колонка, иначе вся строка целиком.
struct LayoutCell {
    std::wstring text;
    int left{0};
    int right{0};
    bool bold{false};
    bool italic{false};
};

struct LayoutLine {
    std::vector<LayoutCell> cells;
    int left{0};
    int top{0};
    int right{0};
    int bottom{0};
    int font_height{0};
};

// Абзац Tesseract: либо обычные строки, либо строка таблицы с колонками.
struct LayoutBlock {
    bool table{false};
    std::vector<LayoutLine> lines;
    int left{0};
    int top{0};
    int right{0};
    int bottom{0};
};

// Линия сетки таблицы с протяжённостью.
struct GridLine {
    int pos{0};    // x для вертикальной, y для горизонтальной
    int start{0};  // начало отрезка по другой оси
    int end{0};    // конец (включительно)
};

struct GridSegments {
    std::vector<GridLine> vertical;
    std::vector<GridLine> horizontal;
};

// Текст ячейки таблицы: позиция в сетке и объединение соседних ячеек.
struct LayoutTableText {
    std::wstring text;
    int row{0};
    int column{0};
    int row_span{1};
    int column_span{1};
    int font_height{0};
    bool bold{false};
    bool italic{false};
    int left{0};
    int top{0};
    int right{0};
    int bottom{0};
};

// Таблица: границы строк/колонок (по пикселям изображения) и текст ячеек.
struct LayoutTable {
    std::vector<int> columns;  // x-границы, size = колонки + 1
    std::vector<int> rows;     // y-границы, size = строки + 1
    std::vector<LayoutTableText> texts;
};

struct LayoutPage {
    int width{0};
    int height{0};
    std::vector<LayoutBlock> blocks;
    // Таблицы, восстановленные по линиям сетки на изображении.
    std::vector<LayoutTable> tables;
};

// Восстанавливает разметку страницы из координат слов OCR: абзацы,
// строки, колонки и таблицы (эвристика по устойчивым вертикальным зазорам).
// forced_columns — готовые границы колонок из линий сетки на изображении.
LayoutPage build_layout_page(
    const OcrLayout& layout,
    const std::vector<int>& forced_columns = {});

// Полная версия: линии сетки (горизонтальные и вертикальные) с их протяжённостью.
LayoutPage build_layout_page(
    const OcrLayout& layout,
    const GridSegments& grid);

// Тексты ячеек, которые нужно перевести (числа и пустое пропускаются).
std::vector<std::wstring> layout_texts(const LayoutPage& page);

// Ищет вертикальные линии сетки таблицы (BGRA) и возвращает их x-координаты.
std::vector<int> detect_vertical_grid_lines(
    const std::uint8_t* bgra,
    int width,
    int height,
    int bytes_per_line);

// Ищет линии сетки таблиц с их протяжённостью (BGRA).
GridSegments detect_grid_segments(
    const std::uint8_t* bgra,
    int width,
    int height,
    int bytes_per_line);

// Раскладывает переводы по тем же ячейкам, что собрал layout_texts.
void apply_layout_translations(
    LayoutPage* page,
    const std::vector<std::wstring>& translations);

}  // пространство имён offline_translator
