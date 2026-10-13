#include "offline_translator/document_layout.hpp"

#include "offline_translator/excel_cells.hpp"

#include <algorithm>
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
        cell.left = words.front()->left;
        cell.right = words.back()->right;
    }
    return cell;
}

}  // анонимное пространство имён

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
        // Таблица: зазоры есть у многих строк и образуют вертикальные полосы.
        const bool table_like = ordered_lines.size() >= 2 &&
            lines_with_gap * 2 >=
                static_cast<int>(ordered_lines.size());
        if (table_like && !intervals.empty()) {
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
                    static_cast<int>(ordered_lines.size()) / 3);
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
        if (table_like && columns.empty() && !forced_columns.empty()) {
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

            if (!block.table) {
                line.cells.push_back(make_line_cell(line_words, font_height));
            } else {
                std::size_t word_index = 0;
                int column = 0;
                while (word_index < line_words.size()) {
                    std::vector<const WordInfo*> cell_words;
                    const int boundary =
                        column < static_cast<int>(columns.size())
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
                    line.cells.push_back(
                        make_line_cell(cell_words, font_height));
                    ++column;
                    if (column > static_cast<int>(columns.size())) {
                        // Последняя колонка: добираем остаток слов.
                        while (word_index < line_words.size()) {
                            line.cells.back().text += L' ' +
                                line_words[word_index]->text;
                            line.cells.back().right =
                                line_words[word_index]->right;
                            ++word_index;
                        }
                    }
                }
            }
            block.lines.push_back(std::move(line));
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
            for (const auto& cell : line.cells) {
                if (excel_cell_translatable(cell.text)) {
                    texts.push_back(cell.text);
                }
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
            if (sum < 420) {
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
