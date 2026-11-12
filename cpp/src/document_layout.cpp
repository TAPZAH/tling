#include "offline_translator/document_layout.hpp"

#include "offline_translator/excel_cells.hpp"

#include <algorithm>
#include <cstdlib>
#include <map>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace offline_translator {
namespace {

std::wstring wide_text(std::string_view utf8) {
    if (utf8.empty()) {
        return {};
    }
#ifdef _WIN32
    const int size = MultiByteToWideChar(
        CP_UTF8,
        0,
        utf8.data(),
        static_cast<int>(utf8.size()),
        nullptr,
        0);
    if (size <= 0) {
        return {};
    }
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(
        CP_UTF8,
        0,
        utf8.data(),
        static_cast<int>(utf8.size()),
        result.data(),
        size);
    return result;
#else
    std::wstring result;
    result.reserve(utf8.size());
    for (const char symbol : utf8) {
        result.push_back(static_cast<unsigned char>(symbol));
    }
    return result;
#endif
}

struct WordInfo {
    std::wstring text;
    int left{0};
    int top{0};
    int right{0};
    int bottom{0};
    int height{0};
    int block{0};
    int paragraph{0};
    int line{0};
};

int median(std::vector<int> values) {
    if (values.empty()) {
        return 0;
    }
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
}

// Текст строки из слов: пробел ставим только там, где между словами был
// заметный зазор (иначе CJK-текст разъедется пробелами).
std::wstring join_words(
    const std::vector<const WordInfo*>& words,
    int font_height) {
    std::wstring text;
    const WordInfo* previous = nullptr;
    for (const WordInfo* word : words) {
        if (previous != nullptr) {
            const int gap = word->left - previous->right;
            if (gap > std::max(4, font_height / 5)) {
                text += L' ';
            }
        }
        text += word->text;
        previous = word;
    }
    return text;
}

LayoutCell make_line_cell(
    const std::vector<const WordInfo*>& words,
    int font_height) {
    LayoutCell cell;
    cell.text = join_words(words, font_height);
    if (!words.empty()) {
        int left = words.front()->left;
        int right = words.front()->right;
        for (const WordInfo* word : words) {
            left = std::min(left, word->left);
            right = std::max(right, word->right);
        }
        cell.left = left;
        cell.right = right;
    }
    return cell;
}

// Делит строку на ячейки по границам колонок (последняя забирает остаток).
std::vector<LayoutCell> split_line_by_columns(
    const std::vector<const WordInfo*>& line_words,
    const std::vector<int>& columns,
    int font_height) {
    std::vector<LayoutCell> cells;
    std::size_t word_index = 0;
    int column = 0;
    while (word_index < line_words.size()) {
        std::vector<const WordInfo*> cell_words;
        const int boundary = column < static_cast<int>(columns.size())
            ? columns[static_cast<std::size_t>(column)]
            : 0;
        while (word_index < line_words.size()) {
            if (column < static_cast<int>(columns.size()) &&
                line_words[word_index]->left > boundary) {
                break;
            }
            cell_words.push_back(line_words[word_index]);
            ++word_index;
        }
        cells.push_back(make_line_cell(cell_words, font_height));
        ++column;
        if (column > static_cast<int>(columns.size())) {
            while (word_index < line_words.size()) {
                const WordInfo* word = line_words[word_index];
                if (cells.back().text.empty()) {
                    cells.back().left = word->left;
                    cells.back().text = word->text;
                } else {
                    cells.back().text += L' ' + word->text;
                }
                cells.back().right = word->right;
                ++word_index;
            }
        }
    }
    return cells;
}

// Колонки сетки, которые пересекают эту строку по реальному зазору между
// словами: тогда строка — это строка таблицы, а не предложение.
std::vector<int> line_columns_with_gaps(
    const std::vector<const WordInfo*>& line_words,
    const std::vector<int>& forced_columns,
    int min_gap) {
    std::vector<int> result;
    if (forced_columns.empty() || line_words.size() < 2) {
        return result;
    }
    // Допуск: колонка таблицы и зазор конкретной строки могут немного не
    // совпадать по x.
    const int tolerance = 15;
    for (const int column : forced_columns) {
        for (std::size_t i = 1; i < line_words.size(); ++i) {
            const int gap_start = line_words[i - 1]->right;
            const int gap_end = line_words[i]->left;
            if (gap_end - gap_start < min_gap) {
                continue;
            }
            if (column >= gap_start - tolerance &&
                column <= gap_end + tolerance) {
                // Границей берём середину зазора именно этой строки.
                result.push_back((gap_start + gap_end) / 2);
                break;
            }
        }
    }
    return result;
}

}  // анонимное пространство имён

// --- линии сетки и таблицы -------------------------------------------------

std::vector<GridLine> extract_grid_runs(
    const std::uint8_t* bgra,
    int width,
    int height,
    int bytes_per_line,
    bool vertical) {
    std::vector<GridLine> raw;
    const int outer = vertical ? width : height;
    const int inner = vertical ? height : width;
    const int min_len = std::max(8, inner / 25);
    // Фон оцениваем по светлым пикселям: линии сетки часто светло-серые или
    // светло-оранжевые и не проходят фиксированный порог.
    std::vector<int> samples;
    samples.reserve(4096);
    for (int y = 0; y < height; y += 7) {
        const std::uint8_t* row =
            bgra + static_cast<std::size_t>(y) * bytes_per_line;
        for (int x = 0; x < width; x += 7) {
            const std::uint8_t* p = row + static_cast<std::size_t>(x) * 4;
            samples.push_back(p[0] + p[1] + p[2]);
        }
    }
    std::sort(samples.begin(), samples.end());
    const int background = samples.empty()
        ? 765
        : std::max(samples[samples.size() * 9 / 10], 1);
    const int threshold = background * 95 / 100;
    for (int o = 0; o < outer; ++o) {
        int run_start = -1;
        int run_end = -1;
        int gap = 0;
        for (int i = 0; i <= inner; ++i) {
            bool dark = false;
            if (i < inner) {
                const int x = vertical ? o : i;
                const int y = vertical ? i : o;
                const std::uint8_t* p =
                    bgra + static_cast<std::size_t>(y) * bytes_per_line +
                    static_cast<std::size_t>(x) * 4;
                dark = p[0] + p[1] + p[2] < threshold;
            }
            if (dark) {
                if (run_start < 0) {
                    run_start = i;
                }
                run_end = i;
                gap = 0;
                continue;
            }
            if (run_start < 0) {
                continue;
            }
            ++gap;
            if (gap > 3 || i == inner) {
                if (run_end - run_start + 1 >= min_len) {
                    raw.push_back(GridLine{o, run_start, run_end});
                }
                run_start = -1;
                run_end = -1;
                gap = 0;
            }
        }
    }
    std::sort(
        raw.begin(),
        raw.end(),
        [](const GridLine& left, const GridLine& right) {
            return left.pos < right.pos;
        });
    std::vector<GridLine> lines;
    for (const auto& line : raw) {
        bool merged = false;
        for (auto it = lines.rbegin();
             it != lines.rend() && line.pos - it->pos <= 2;
             ++it) {
            if (it->start <= line.end + 4 && line.start <= it->end + 4) {
                it->start = std::min(it->start, line.start);
                it->end = std::max(it->end, line.end);
                merged = true;
                break;
            }
        }
        if (!merged) {
            lines.push_back(line);
        }
    }
    return lines;
}

bool grid_lines_cross(const GridLine& vertical, const GridLine& horizontal) {
    return vertical.pos >= horizontal.start - 2 &&
        vertical.pos <= horizontal.end + 2 &&
        horizontal.pos >= vertical.start - 2 &&
        horizontal.pos <= vertical.end + 2;
}

bool has_vertical_border(
    const std::vector<GridLine>& lines,
    int pos,
    int top,
    int bottom) {
    for (const auto& line : lines) {
        if (std::abs(line.pos - pos) <= 2 && line.start <= bottom + 2 &&
            line.end >= top - 2) {
            return true;
        }
    }
    return false;
}

bool has_horizontal_border(
    const std::vector<GridLine>& lines,
    int pos,
    int left,
    int right) {
    for (const auto& line : lines) {
        if (std::abs(line.pos - pos) <= 2 && line.start <= right + 2 &&
            line.end >= left - 2) {
            return true;
        }
    }
    return false;
}

int grid_find(std::vector<int>& parent, int value) {
    while (parent[static_cast<std::size_t>(value)] != value) {
        parent[static_cast<std::size_t>(value)] =
            parent[static_cast<std::size_t>(parent[static_cast<std::size_t>(value)])];
        value = parent[static_cast<std::size_t>(value)];
    }
    return value;
}

struct TableFrame {
    std::vector<int> xs;
    std::vector<int> ys;
    std::vector<GridLine> vertical;
    std::vector<GridLine> horizontal;
};

std::vector<TableFrame> find_table_frames(const GridSegments& grid) {
    const auto& v = grid.vertical;
    const auto& h = grid.horizontal;
    const std::size_t count = v.size() + h.size();
    std::vector<int> parent(count);
    for (std::size_t i = 0; i < count; ++i) {
        parent[i] = static_cast<int>(i);
    }
    for (std::size_t i = 0; i < v.size(); ++i) {
        for (std::size_t j = 0; j < h.size(); ++j) {
            if (!grid_lines_cross(v[i], h[j])) {
                continue;
            }
            const int a = grid_find(parent, static_cast<int>(i));
            const int b = grid_find(
                parent,
                static_cast<int>(v.size() + j));
            if (a != b) {
                parent[static_cast<std::size_t>(b)] = a;
            }
        }
    }
    std::map<int, TableFrame> frames;
    for (std::size_t i = 0; i < v.size(); ++i) {
        frames[grid_find(parent, static_cast<int>(i))].vertical.push_back(v[i]);
    }
    for (std::size_t j = 0; j < h.size(); ++j) {
        frames[grid_find(parent, static_cast<int>(v.size() + j))]
            .horizontal.push_back(h[j]);
    }
    std::vector<TableFrame> result;
    for (auto& [root, frame] : frames) {
        (void)root;
        if (frame.vertical.size() < 2 || frame.horizontal.size() < 2) {
            continue;
        }
        for (const auto& line : frame.vertical) {
            frame.xs.push_back(line.pos);
        }
        for (const auto& line : frame.horizontal) {
            frame.ys.push_back(line.pos);
        }
        std::sort(frame.xs.begin(), frame.xs.end());
        frame.xs.erase(
            std::unique(
                frame.xs.begin(),
                frame.xs.end(),
                [](int left, int right) { return std::abs(left - right) <= 2; }),
            frame.xs.end());
        std::sort(frame.ys.begin(), frame.ys.end());
        frame.ys.erase(
            std::unique(
                frame.ys.begin(),
                frame.ys.end(),
                [](int left, int right) { return std::abs(left - right) <= 2; }),
            frame.ys.end());
        if (frame.xs.size() < 2 || frame.ys.size() < 2) {
            continue;
        }
        result.push_back(std::move(frame));
    }
    return result;
}

std::wstring join_cell_words(const std::vector<const WordInfo*>& words) {
    std::vector<const WordInfo*> sorted = words;
    std::sort(
        sorted.begin(),
        sorted.end(),
        [](const WordInfo* left, const WordInfo* right) {
            if (left->line != right->line) {
                return left->line < right->line;
            }
            return left->left < right->left;
        });
    std::wstring text;
    int previous_line = -1000000;
    for (const WordInfo* word : sorted) {
        if (word->text.empty()) {
            continue;
        }
        if (!text.empty()) {
            text += word->line != previous_line ? L'\n' : L' ';
        }
        text += word->text;
        previous_line = word->line;
    }
    return text;
}

std::vector<LayoutTable> build_tables_from_grid(
    const GridSegments& grid,
    const std::vector<WordInfo>& words) {
    std::vector<LayoutTable> tables;
    for (const auto& frame : find_table_frames(grid)) {
        int rows = static_cast<int>(frame.ys.size()) - 1;
        int columns = static_cast<int>(frame.xs.size()) - 1;
        if (rows < 2 || columns < 2) {
            continue;
        }
        // Штрихи и рамки дают ложные линии: в сетке появляются десятки
        // пустых колонок/строк. Схлопываем подряд идущие пустые полосы
        // (граница остаётся, пока хотя бы с одной стороны есть слова).
        std::vector<int> xs = frame.xs;
        std::vector<int> ys = frame.ys;
        {
            std::vector<int> column_hits(xs.size(), 0);
            std::vector<int> row_hits(ys.size(), 0);
            for (const auto& word : words) {
                if (word.text.empty()) {
                    continue;
                }
                const int cx = (word.left + word.right) / 2;
                const int cy = (word.top + word.bottom) / 2;
                if (cx < xs.front() || cx > xs.back() || cy < ys.front() ||
                    cy > ys.back()) {
                    continue;
                }
                const int column = std::clamp(
                    static_cast<int>(
                        std::upper_bound(xs.begin(), xs.end(), cx) -
                        xs.begin()) - 1,
                    0,
                    columns - 1);
                const int row = std::clamp(
                    static_cast<int>(
                        std::upper_bound(ys.begin(), ys.end(), cy) -
                        ys.begin()) - 1,
                    0,
                    rows - 1);
                ++column_hits[static_cast<std::size_t>(column)];
                ++row_hits[static_cast<std::size_t>(row)];
            }
            std::vector<int> pruned_xs;
            pruned_xs.push_back(xs.front());
            for (std::size_t boundary = 1; boundary + 1 < xs.size();
                 ++boundary) {
                if (column_hits[boundary - 1] > 0 ||
                    column_hits[boundary] > 0) {
                    pruned_xs.push_back(xs[boundary]);
                }
            }
            pruned_xs.push_back(xs.back());
            std::vector<int> pruned_ys;
            pruned_ys.push_back(ys.front());
            for (std::size_t boundary = 1; boundary + 1 < ys.size();
                 ++boundary) {
                if (row_hits[boundary - 1] > 0 || row_hits[boundary] > 0) {
                    pruned_ys.push_back(ys[boundary]);
                }
            }
            pruned_ys.push_back(ys.back());
            xs = std::move(pruned_xs);
            ys = std::move(pruned_ys);
            rows = static_cast<int>(ys.size()) - 1;
            columns = static_cast<int>(xs.size()) - 1;
            if (rows < 2 || columns < 2) {
                continue;
            }
        }
        // Слова по (строка, грубая колонка) — колонку потом уточняем интервалом.
        std::vector<std::vector<const WordInfo*>> by_row(
            static_cast<std::size_t>(rows));
        std::vector<std::vector<int>> word_columns(
            static_cast<std::size_t>(rows));
        for (const auto& word : words) {
            if (word.text.empty()) {
                continue;
            }
            const int cx = (word.left + word.right) / 2;
            const int cy = (word.top + word.bottom) / 2;
            if (cx < xs.front() || cx > xs.back() || cy < ys.front() ||
                cy > ys.back()) {
                continue;
            }
            int row = static_cast<int>(
                std::upper_bound(ys.begin(), ys.end(), cy) - ys.begin()) - 1;
            row = std::clamp(row, 0, rows - 1);
            by_row[static_cast<std::size_t>(row)].push_back(&word);
            word_columns[static_cast<std::size_t>(row)].push_back(cx);
        }
        std::size_t word_count = 0;
        for (const auto& row_words : by_row) {
            word_count += row_words.size();
        }
        if (word_count < 4) {
            continue;
        }
        // Границы ячеек в полосе строки: только там, где реально проходит
        // вертикальный отрезок. Иначе соседние колонки — объединённая ячейка.
        struct RowCells {
            std::vector<int> starts;  // начало интервала (колонка)
            std::vector<int> ends;    // конец интервала (включительно)
            std::vector<int> spans;
            std::vector<std::wstring> texts;
        };
        std::vector<RowCells> row_cells(static_cast<std::size_t>(rows));
        for (int row = 0; row < rows; ++row) {
            RowCells& cells = row_cells[static_cast<std::size_t>(row)];
            int start = 0;
            for (int boundary = 1; boundary <= columns; ++boundary) {
                const bool active = boundary == columns ||
                    has_vertical_border(
                        frame.vertical,
                        xs[static_cast<std::size_t>(boundary)],
                        ys[static_cast<std::size_t>(row)],
                        ys[static_cast<std::size_t>(row + 1)]);
                if (!active) {
                    continue;
                }
                const int end = boundary - 1;
                std::vector<const WordInfo*> cell_words;
                for (std::size_t index = 0;
                     index < by_row[static_cast<std::size_t>(row)].size();
                     ++index) {
                    const int column = static_cast<int>(
                        std::upper_bound(xs.begin(), xs.end(),
                                         word_columns[static_cast<std::size_t>(row)][index]) -
                        xs.begin()) - 1;
                    if (column >= start && column <= end) {
                        cell_words.push_back(
                            by_row[static_cast<std::size_t>(row)][index]);
                    }
                }
                cells.starts.push_back(start);
                cells.ends.push_back(end);
                cells.spans.push_back(end - start + 1);
                cells.texts.push_back(join_cell_words(cell_words));
                start = boundary;
            }
        }
        LayoutTable table;
        table.columns = xs;
        table.rows = ys;
        for (int row = 0; row < rows; ++row) {
            const RowCells& cells = row_cells[static_cast<std::size_t>(row)];
            if (cells.texts.empty()) {
                continue;
            }
            for (std::size_t cell_index = 0;
                 cell_index < cells.texts.size();
                 ++cell_index) {
                if (cells.texts[cell_index].empty()) {
                    continue;
                }
                // Вертикальное объединение: та же колонка в следующих строках
                // без разделительной горизонтальной линии и без своего текста.
                int row_span = 1;
                while (row + row_span < rows) {
                    const RowCells& below =
                        row_cells[static_cast<std::size_t>(row + row_span)];
                    std::size_t match = below.starts.size();
                    for (std::size_t index = 0; index < below.starts.size();
                         ++index) {
                        if (below.starts[index] ==
                                cells.starts[cell_index] &&
                            below.ends[index] == cells.ends[cell_index]) {
                            match = index;
                            break;
                        }
                    }
                    if (match >= below.texts.size() ||
                        !below.texts[match].empty() ||
                        has_horizontal_border(
                            frame.horizontal,
                            table.rows[static_cast<std::size_t>(row + row_span)],
                            table.columns[static_cast<std::size_t>(
                                cells.starts[cell_index])],
                            table.columns[static_cast<std::size_t>(
                                cells.ends[cell_index] + 1)])) {
                        break;
                    }
                    ++row_span;
                }
                LayoutTableText text;
                text.text = cells.texts[cell_index];
                text.row = row;
                text.column = cells.starts[cell_index];
                text.column_span = cells.spans[cell_index];
                text.row_span = row_span;
                std::vector<int> heights;
                int left = 1 << 30;
                int top = 1 << 30;
                int right = -1;
                int bottom = -1;
                for (const auto* word : by_row[static_cast<std::size_t>(row)]) {
                    const int cx = (word->left + word->right) / 2;
                    const int column = static_cast<int>(
                        std::upper_bound(
                            frame.xs.begin(), frame.xs.end(), cx) -
                        frame.xs.begin()) - 1;
                    if (column < cells.starts[cell_index] ||
                        column > cells.ends[cell_index]) {
                        continue;
                    }
                    heights.push_back(word->height);
                    left = std::min(left, word->left);
                    top = std::min(top, word->top);
                    right = std::max(right, word->right);
                    bottom = std::max(bottom, word->bottom);
                }
                text.font_height = std::max(median(heights), 1);
                text.left = left;
                text.top = top;
                text.right = right;
                text.bottom = bottom;
                table.texts.push_back(std::move(text));
            }
        }
        if (table.texts.size() >= 4) {
            tables.push_back(std::move(table));
        }
    }
    return tables;
}

bool line_inside_table(const LayoutPage& page, const LayoutLine& line) {
    const int cx = (line.left + line.right) / 2;
    const int cy = (line.top + line.bottom) / 2;
    for (const auto& table : page.tables) {
        if (table.columns.empty() || table.rows.empty()) {
            continue;
        }
        if (cx >= table.columns.front() && cx <= table.columns.back() &&
            cy >= table.rows.front() && cy <= table.rows.back()) {
            return true;
        }
    }
    return false;
}

LayoutPage build_layout_page(
    const OcrLayout& layout,
    const std::vector<int>& forced_columns) {
    LayoutPage page;
    page.width = layout.width;
    page.height = layout.height;
    if (layout.words.empty()) {
        return page;
    }

    std::vector<WordInfo> words;
    words.reserve(layout.words.size());
    for (const auto& box : layout.words) {
        WordInfo info;
        info.text = wide_text(box.text);
        info.left = box.left;
        info.top = box.top;
        info.right = box.left + box.width;
        info.bottom = box.top + box.height;
        info.height = box.height;
        info.block = box.block;
        info.paragraph = box.paragraph;
        info.line = box.line;
        words.push_back(std::move(info));
    }

    // Группируем слова по блокам Tesseract. В сложной таблице каждая
    // строка может быть своим «абзацем», а колонки общие для всего блока,
    // поэтому таблицу ищем в пределах блока.
    std::map<int, std::vector<std::size_t>> blocks;
    for (std::size_t index = 0; index < words.size(); ++index) {
        blocks[words[index].block].push_back(index);
    }

    for (auto& [block_id, indices] : blocks) {
        (void)block_id;
        std::vector<int> heights;
        for (const auto index : indices) {
            heights.push_back(words[index].height);
        }
        const int font_height = std::max(median(heights), 1);
        int words_min_left = words[indices.front()].left;
        int words_max_right = words[indices.front()].right;
        for (const auto index : indices) {
            words_min_left = std::min(words_min_left, words[index].left);
            words_max_right = std::max(words_max_right, words[index].right);
        }

        // Строки блока: ключ (абзац, строка), порядок — сверху вниз.
        std::map<std::pair<int, int>, std::vector<const WordInfo*>> lines;
        for (const auto index : indices) {
            lines[{words[index].paragraph, words[index].line}].push_back(
                &words[index]);
        }
        std::vector<std::vector<const WordInfo*>> ordered_lines;
        for (auto& [line_key, line_words] : lines) {
            (void)line_key;
            std::sort(
                line_words.begin(),
                line_words.end(),
                [](const WordInfo* left, const WordInfo* right) {
                    return left->left < right->left;
                });
            ordered_lines.push_back(std::move(line_words));
        }
        std::sort(
            ordered_lines.begin(),
            ordered_lines.end(),
            [](const std::vector<const WordInfo*>& left,
               const std::vector<const WordInfo*>& right) {
                return left.front()->top < right.front()->top;
            });
        if (ordered_lines.empty()) {
            continue;
        }

        // Вертикальные зазоры, повторяющиеся от строки к строке, — границы
        // колонок. Считаем «покрытие» по x: сколько строк имеют зазор в
        // этой точке. Устойчивые полосы покрытия — разделители колонок, при
        // этом соседние колонки не сливаются.
        const int min_gap = std::max(8, font_height);
        struct GapInterval {
            int start{0};
            int end{0};
        };
        std::vector<GapInterval> intervals;
        int lines_with_gap = 0;
        for (const auto& line_words : ordered_lines) {
            bool has_gap = false;
            for (std::size_t index = 1; index < line_words.size(); ++index) {
                const int start = line_words[index - 1]->right;
                const int end = line_words[index]->left;
                if (end - start >= min_gap) {
                    intervals.push_back({start, end});
                    has_gap = true;
                }
            }
            if (has_gap) {
                ++lines_with_gap;
            }
        }
        std::vector<int> columns;
        // Зазоры, повторяющиеся от строки к строке, — границы колонок.
        // Таблица не всегда распознаётся как единый блок, поэтому колонки
        // вычисляем всегда, а применяем к строке только там, где зазор есть.
        if (!intervals.empty()) {
            int min_x = intervals.front().start;
            int max_x = intervals.front().end;
            for (const auto& interval : intervals) {
                min_x = std::min(min_x, interval.start);
                max_x = std::max(max_x, interval.end);
            }
            if (max_x > min_x) {
                std::vector<int> coverage(
                    static_cast<std::size_t>(max_x - min_x + 1),
                    0);
                for (const auto& interval : intervals) {
                    const int from = std::max(interval.start, min_x);
                    const int to = std::min(interval.end, max_x);
                    for (int x = from; x < to; ++x) {
                        ++coverage[static_cast<std::size_t>(x - min_x)];
                    }
                }
                const int threshold = std::max(
                    2,
                    lines_with_gap / 2);
                std::size_t index = 0;
                while (index < coverage.size()) {
                    if (coverage[index] >= threshold) {
                        const std::size_t start = index;
                        while (index < coverage.size() &&
                               coverage[index] >= threshold) {
                            ++index;
                        }
                        const std::size_t end = index;
                        columns.push_back(
                            min_x + static_cast<int>((start + end) / 2));
                    } else {
                        ++index;
                    }
                }
            }
        }
        // Если по зазорам колонки не нашлись, берём линии сетки с картинки.
        if (columns.empty() && !forced_columns.empty()) {
            for (const int column : forced_columns) {
                if (column > words_min_left && column < words_max_right) {
                    columns.push_back(column);
                }
            }
        }

        LayoutBlock block;
        block.table = !columns.empty();
        for (const auto& line_words : ordered_lines) {
            LayoutLine line;
            line.top = line_words.front()->top;
            line.bottom = line_words.front()->bottom;
            line.left = line_words.front()->left;
            line.right = line_words.front()->right;
            std::vector<int> line_heights;
            for (const WordInfo* word : line_words) {
                line.top = std::min(line.top, word->top);
                line.bottom = std::max(line.bottom, word->bottom);
                line.left = std::min(line.left, word->left);
                line.right = std::max(line.right, word->right);
                line_heights.push_back(word->height);
            }
            line.font_height = std::max(median(line_heights), 1);

            // Колонки применяем к строке только при реальном зазоре между
            // словами — тогда значение остаётся в своей колонке, как в
            // оригинале, а предложение абзаца не разваливается.
            std::vector<int> active_columns =
                line_columns_with_gaps(line_words, columns, min_gap);
            if (active_columns.empty()) {
                active_columns = line_columns_with_gaps(
                    line_words, forced_columns, min_gap);
            }
            if (!active_columns.empty()) {
                line.cells =
                    split_line_by_columns(line_words, active_columns, font_height);
            } else {
                line.cells.push_back(make_line_cell(line_words, font_height));
            }
            block.lines.push_back(std::move(line));
        }

        // Склеиваем строки абзаца в один блок: перевод тогда цельный (как в
        // оригинале), а не разбитый по строкам. Строки таблицы (несколько
        // ячеек) и новые пункты списка не склеиваем.
        std::vector<LayoutLine> merged;
        for (auto& line : block.lines) {
            const bool body = line.cells.size() == 1 &&
                !line.cells[0].text.empty();
            if (body && !merged.empty()) {
                LayoutLine& previous = merged.back();
                const bool previous_body = previous.cells.size() == 1 &&
                    !previous.cells[0].text.empty();
                const std::wstring& text = line.cells[0].text;
                const wchar_t first = text.empty() ? L'\0' : text.front();
                const bool starts_item = first == L'•' || first == L'-' ||
                    first == L'·' || first == L'*';
                // Склеиваем только если предыдущая строка оборвана на полуслове
                // (нет завершающей точки) — иначе это новый пункт/предложение.
                const std::wstring& prior = previous.cells[0].text;
                std::size_t last = prior.size();
                while (last > 0 && (prior[last - 1] == L' ' ||
                                    prior[last - 1] == L'\t')) {
                    --last;
                }
                const wchar_t prior_last = last > 0 ? prior[last - 1] : L'\0';
                const bool sentence_end = prior_last == L'.' ||
                    prior_last == L'!' || prior_last == L'?' ||
                    prior_last == L':' || prior_last == L';';
                const int font = std::max(previous.font_height, 1);
                const int gap = line.top - previous.bottom;
                // Продолжение абзаца/пункта может быть сдвинуто вправо
                // («висячий» отступ после маркера — до нескольких кеглей).
                const bool same_indent = line.left <= previous.left + font * 6;
                const bool same_size =
                    std::abs(previous.font_height - line.font_height) <=
                    std::max(6, font / 2);
                if (previous_body && !starts_item && !sentence_end &&
                    same_indent && same_size && gap <= font) {
                    previous.cells[0].text += L' ';
                    previous.cells[0].text += text;
                    previous.cells[0].left = std::min(
                        previous.cells[0].left, line.cells[0].left);
                    previous.cells[0].right = std::max(
                        previous.cells[0].right, line.cells[0].right);
                    previous.left = std::min(previous.left, line.left);
                    previous.right = std::max(previous.right, line.right);
                    previous.top = std::min(previous.top, line.top);
                    previous.bottom = std::max(previous.bottom, line.bottom);
                    previous.font_height = std::max(
                        previous.font_height, line.font_height);
                    continue;
                }
            }
            merged.push_back(std::move(line));
        }
        block.lines.swap(merged);

        // Заголовки: строка обособлена пустой областью (зазор после) и при
        // этом широкая или стоит по центру страницы. Размер не используем —
        // у буллетов OCR часто даёт завышенный кегль.
        {
            std::vector<int> heights;
            for (const auto& line : block.lines) {
                heights.push_back(line.font_height);
            }
            const int median_height = median(heights);
            const int span = words_max_right - words_min_left;
            const int block_center = (words_min_left + words_max_right) / 2;
            for (std::size_t line_index = 0;
                 line_index < block.lines.size();
                 ++line_index) {
                const auto& line = block.lines[line_index];
                const int gap = line_index + 1 < block.lines.size()
                    ? block.lines[line_index + 1].top - line.bottom
                    : 0;
                const int line_center = (line.left + line.right) / 2;
                const bool centered =
                    span > 0 &&
                    std::abs(line_center - block_center) < span / 10;
                if (line.cells.size() == 1 &&
                    // Одна физическая строка (не склеенный абзац).
                    line.bottom - line.top <= line.font_height * 3 / 2 &&
                    gap >= line.font_height * 3 / 2 &&
                    ((line.right - line.left) >= span / 3 || centered)) {
                    for (auto& cell : block.lines[line_index].cells) {
                        cell.bold = true;
                    }
                    // У стилизованных надписей OCR даёт тонкий бокс — не
                    // рисуем заголовок мельче основного текста.
                    block.lines[line_index].font_height = std::max(
                        line.font_height,
                        median_height * 3 / 2);
                }
            }
        }

        block.left = block.lines.front().left;
        block.top = block.lines.front().top;
        block.right = block.lines.front().right;
        block.bottom = block.lines.front().bottom;
        for (const auto& line : block.lines) {
            block.left = std::min(block.left, line.left);
            block.top = std::min(block.top, line.top);
            block.right = std::max(block.right, line.right);
            block.bottom = std::max(block.bottom, line.bottom);
        }
        page.blocks.push_back(std::move(block));
    }
    return page;
}

std::vector<std::wstring> layout_texts(const LayoutPage& page) {
    std::vector<std::wstring> texts;
    for (const auto& block : page.blocks) {
        for (const auto& line : block.lines) {
            if (line_inside_table(page, line)) {
                continue;
            }
            for (const auto& cell : line.cells) {
                if (excel_cell_translatable(cell.text)) {
                    texts.push_back(cell.text);
                }
            }
        }
    }
    for (const auto& table : page.tables) {
        for (const auto& cell : table.texts) {
            // Многострочные ячейки переводим построчно, чтобы переносы
            // сохранились в экспорте.
            std::size_t start = 0;
            while (start <= cell.text.size()) {
                const std::size_t next = cell.text.find(L'\n', start);
                const std::wstring line = cell.text.substr(
                    start,
                    next == std::wstring::npos ? std::wstring::npos
                                               : next - start);
                if (excel_cell_translatable(line)) {
                    texts.push_back(line);
                }
                if (next == std::wstring::npos) {
                    break;
                }
                start = next + 1;
            }
        }
    }
    return texts;
}

void apply_layout_translations(
    LayoutPage* page,
    const std::vector<std::wstring>& translations) {
    if (page == nullptr) {
        return;
    }
    std::size_t index = 0;
    for (auto& block : page->blocks) {
        for (auto& line : block.lines) {
            if (line_inside_table(*page, line)) {
                continue;
            }
            for (auto& cell : line.cells) {
                if (!excel_cell_translatable(cell.text)) {
                    continue;
                }
                if (index >= translations.size()) {
                    return;
                }
                cell.text = translations[index++];
            }
        }
    }
    for (auto& table : page->tables) {
        for (auto& cell : table.texts) {
            std::wstring rebuilt;
            std::size_t start = 0;
            while (start <= cell.text.size()) {
                const std::size_t next = cell.text.find(L'\n', start);
                const std::wstring line = cell.text.substr(
                    start,
                    next == std::wstring::npos ? std::wstring::npos
                                               : next - start);
                if (!rebuilt.empty() || start > 0) {
                    rebuilt += L'\n';
                }
                if (!excel_cell_translatable(line)) {
                    rebuilt += line;
                } else if (index < translations.size()) {
                    rebuilt += translations[index++];
                } else {
                    rebuilt += line;
                }
                if (next == std::wstring::npos) {
                    break;
                }
                start = next + 1;
            }
            cell.text = std::move(rebuilt);
        }
    }
}

LayoutPage build_layout_page(const OcrLayout& layout, const GridSegments& grid) {
    std::vector<int> columns;
    columns.reserve(grid.vertical.size());
    for (const auto& line : grid.vertical) {
        columns.push_back(line.pos);
    }
    LayoutPage page = build_layout_page(layout, columns);
    if (layout.words.empty()) {
        return page;
    }
    std::vector<WordInfo> words;
    words.reserve(layout.words.size());
    for (const auto& box : layout.words) {
        WordInfo info;
        info.text = wide_text(box.text);
        info.left = box.left;
        info.top = box.top;
        info.right = box.left + box.width;
        info.bottom = box.top + box.height;
        info.height = box.height;
        info.block = box.block;
        info.paragraph = box.paragraph;
        info.line = box.line;
        words.push_back(std::move(info));
    }
    page.tables = build_tables_from_grid(grid, words);
    return page;
}

GridSegments detect_grid_segments(
    const std::uint8_t* bgra,
    int width,
    int height,
    int bytes_per_line) {
    GridSegments grid;
    if (bgra == nullptr || width <= 2 || height <= 2 ||
        bytes_per_line <= 0) {
        return grid;
    }
    grid.vertical =
        extract_grid_runs(bgra, width, height, bytes_per_line, true);
    grid.horizontal =
        extract_grid_runs(bgra, width, height, bytes_per_line, false);
    return grid;
}

std::vector<int> detect_vertical_grid_lines(
    const std::uint8_t* bgra,
    int width,
    int height,
    int bytes_per_line) {
    std::vector<int> lines;
    if (bgra == nullptr || width <= 2 || height <= 2 ||
        bytes_per_line <= 0) {
        return lines;
    }
    std::vector<int> dark(static_cast<std::size_t>(width), 0);
    for (int y = 0; y < height; ++y) {
        const std::uint8_t* row =
            bgra + static_cast<std::size_t>(y) * bytes_per_line;
        for (int x = 0; x < width; ++x) {
            const std::uint8_t* pixel =
                row + static_cast<std::size_t>(x) * 4;
            const int sum = pixel[0] + pixel[1] + pixel[2];
            if (sum < 600) {
                ++dark[static_cast<std::size_t>(x)];
            }
        }
    }
    // Линия сетки тянется почти на всю высоту таблицы.
    const int threshold = std::max(4, height / 4);
    int index = 0;
    while (index < width) {
        if (dark[static_cast<std::size_t>(index)] >= threshold) {
            const int start = index;
            while (index < width &&
                   dark[static_cast<std::size_t>(index)] >= threshold) {
                ++index;
            }
            lines.push_back((start + index - 1) / 2);
        } else {
            ++index;
        }
    }
    return lines;
}

}  // пространство имён offline_translator
