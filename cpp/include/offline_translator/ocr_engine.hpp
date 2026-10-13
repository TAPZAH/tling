#pragma once

#include "offline_translator/package_types.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace offline_translator {

// Результат распознавания: текст и средняя уверенность по словам (0..100).
struct OcrResult {
    std::string text;
    double confidence{0.0};
};

// Слово с координатами в пикселях изображения, отданного в OCR.
struct OcrWordBox {
    std::string text;
    int left{0};
    int top{0};
    int width{0};
    int height{0};
    int block{0};
    int paragraph{0};
    int line{0};
    double confidence{0.0};
};

// Разметка страницы: слова в порядке обхода.
struct OcrLayout {
    int width{0};
    int height{0};
    std::vector<OcrWordBox> words;
};

// ISO-код приложения (en, zh, zt, ...) → код языка Tesseract (eng, chi_sim,
// chi_tra, ...). Бросает std::runtime_error для неподдержанного языка.
std::string tesseract_language_code(std::string_view code);
// Поддерживает ли Tesseract этот язык (для тагальского tl данных нет).
bool tesseract_language_supported(std::string_view code);
// Имя файла языковых данных: eng.traineddata. Бросает для неподдержанного.
std::string tessdata_file_name(std::string_view code);

// Каталог языковых данных Tesseract:
//   %OFFLINE_TRANSLATOR_HOME%/tessdata,
//   иначе <каталог exe>/data/tessdata (portable),
//   иначе %USERPROFILE%/.local/share/offline-translator/tessdata.
std::filesystem::path default_tessdata_root();
// Путь к файлу данных языка в каталоге (по умолчанию — default_tessdata_root).
std::filesystem::path tessdata_path(
    std::string_view code,
    const std::filesystem::path& tessdata_root = {});
// Есть ли на диске непустой файл данных языка.
bool tessdata_installed(
    std::string_view code,
    const std::filesystem::path& tessdata_root = {});

// Скачивает языковые данные tessdata_fast (raw GitHub + зеркало jsDelivr).
// Качает существующим download_resumable — файл докачивается после обрыва.
void download_tessdata(
    std::string_view code,
    const ProgressCallback& progress = {},
    const std::filesystem::path& tessdata_root = {});

// Обёртка TessBaseAPI: Init по tessdata-каталогу, SetImage, Recognize.
// Один экземпляр на поток — объект не потокобезопасен.
class OcrEngine {
public:
    explicit OcrEngine(std::filesystem::path tessdata_root = {});
    ~OcrEngine();
    OcrEngine(const OcrEngine&) = delete;
    OcrEngine& operator=(const OcrEngine&) = delete;

    // Загружает язык в Tesseract. Повторный вызов с тем же языком — no-op.
    // Бросает std::runtime_error, если нет языковых данных или Init не удался.
    void initialize(std::string_view language_code);
    // Загрузка сразу нескольких языков (Tesseract «eng+rus»): нужно для
    // автоопределения при распознавании изображения. Требует данные всех.
    void initialize_multi(const std::vector<std::string>& language_codes);
    // Выгружает текущий язык, освобождая данные Tesseract.
    void reset();
    bool is_initialized() const;
    // ISO-код загруженного языка (пусто, если движок не инициализирован).
    std::string language_code() const;

    // image: BGRA (4 байта на пиксель) или grayscale (1 байт). Для BGRA
    // выполняется перевод в градации серого (устойчиво к порядку каналов).
    // bytes_per_line — шаг строки в байтах. Бросает, если движок не готов.
    OcrResult recognize(
        const std::uint8_t* image,
        int width,
        int height,
        int bytes_per_line,
        int bytes_per_pixel);

    // То же распознавание, но с координатами слов (TSV Tesseract): нужно
    // для восстановления разметки страницы (абзацы, колонки, таблицы).
    OcrLayout recognize_layout(
        const std::uint8_t* image,
        int width,
        int height,
        int bytes_per_line,
        int bytes_per_pixel);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Распознаёт текст, подбирая поворот изображения (0/90/180/270): сканы
// часто перевёрнуты. rotation_degrees (если задан) получает лучший угол.
OcrLayout recognize_best_orientation(
    OcrEngine& engine,
    const std::uint8_t* image,
    int width,
    int height,
    int bytes_per_line,
    int bytes_per_pixel,
    int* rotation_degrees = nullptr);

// Поворот BGRA-изображения (0/90/180/270). Для других форматов — копия
// исходных данных без поворота.
std::vector<std::uint8_t> rotate_bgra_image(
    const std::uint8_t* image,
    int width,
    int height,
    int bytes_per_line,
    int rotation,
    int* out_width,
    int* out_height);

}  // namespace offline_translator
