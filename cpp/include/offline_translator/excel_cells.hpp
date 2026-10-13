#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace offline_translator {

// Выделение Excel, подготовленное к переводу: строки и ячейки.
struct ExcelGrid {
    std::vector<std::vector<std::wstring>> rows;

    bool empty() const;
    std::size_t cell_count() const;
    std::size_t translatable_count() const;
};

// Разбор TSV-выделения Excel: кавычки, экранирование "" и переводы строк
// внутри ячеек. Одиночный завершающий перевод строки игнорируется.
ExcelGrid parse_excel_tsv(std::wstring_view text);

// Обратно в TSV: ячейки с табами/кавычками/переводами строк экранируются.
std::wstring serialize_excel_tsv(const ExcelGrid& grid);

// Ячейка содержит буквы: числа, даты и пустое не переводим.
bool excel_cell_translatable(std::wstring_view cell);

// Оригинал + разделитель + перевод; хвостовые пробелы оригинала срезаются.
std::wstring excel_combined_cell(
    std::wstring_view original,
    std::wstring_view translation,
    std::wstring_view separator);

// Защита от «выделить весь столбец»: не переводим гигантские диапазоны.
inline constexpr std::size_t kExcelMaxCells = 4000;
inline constexpr std::size_t kExcelMaxTranslatableCells = 400;
bool excel_selection_supported(std::size_t cells, std::size_t translatable);

}  // пространство имён offline_translator
