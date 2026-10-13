#pragma once

#include <string>
#include <vector>

namespace tling_excel {

// Значения выделенного диапазона Excel.
struct SelectionData {
    long rows{0};
    long cols{0};
    // Построчно (row-major). Числа, даты и ошибки — пустые строки.
    std::vector<std::wstring> texts;
    // true — в ячейке формула: такие ячейки не трогаем.
    std::vector<bool> formulas;
};

// Подключение к УЖЕ запущенному Excel через COM (новый экземпляр не
// создаётся). Все вызовы — с одного STA-потока: создавайте объект и
// работайте с ним в одном потоке (CoInitializeEx внутри attach).
class ExcelSession {
public:
    ExcelSession();
    ~ExcelSession();
    ExcelSession(const ExcelSession&) = delete;
    ExcelSession& operator=(const ExcelSession&) = delete;

    bool attach(std::wstring* error);
    // Несколько несвязанных областей выделения не поддерживаются.
    bool read_selection(SelectionData* data, std::wstring* error);
    // Запись текста в ячейку выделения (координаты 1-based).
    bool write_cell(
        long row,
        long col,
        const std::wstring& text,
        std::wstring* error);

private:
    struct Impl;
    Impl* impl_{nullptr};
};

enum class ExcelSelfTestResult { passed, failed, skipped };

// Проверка COM-слоя на живом Excel: создаёт отдельный невидимый экземпляр
// только если Excel ещё не запущен (чужую сессию не трогаем), пишет и
// читает диапазон. Без установленного Excel — skipped.
ExcelSelfTestResult excel_com_self_test(std::wstring* message);

}  // namespace tling_excel
