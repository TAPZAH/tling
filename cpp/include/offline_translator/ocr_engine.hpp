#pragma once

#include "offline_translator/package_types.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace offline_translator {

// Результат распознавания: текст и средняя уверенность по словам (0..100).
struct OcrResult {
    std::string text;
    double confidence{0.0};
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

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace offline_translator
