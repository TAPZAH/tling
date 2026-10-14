#pragma once

#include "offline_translator/ocr_engine.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace offline_translator {

// Скрипт (группа письменности) PP-OCR для ISO-кода приложения: "latin"
// (en/de/fr/es/tr/pl/...), "eslav" (ru/uk/be/bg). Пусто — нет распознавателя.
std::string ppocr_script_for_language(std::string_view code);

// Каталог моделей PP-OCR:
//   %OFFLINE_TRANSLATOR_HOME%/ppocr,
//   иначе <каталог exe>/data/ppocr (portable),
//   иначе %USERPROFILE%/.local/share/offline-translator/ppocr.
std::filesystem::path default_ppocr_root();

// Есть ли базовый набор (детектор + хотя бы один распознаватель) в каталоге.
bool ppocr_installed(const std::filesystem::path& root = {});
// Установлен ли распознаватель скрипта ("latin"/"eslav").
bool ppocr_script_installed(
    std::string_view script,
    const std::filesystem::path& root = {});

// OCR на моделях PaddleOCR (PP-OCR) через ONNX Runtime. Детектор текстовых
// регионов (DBNet) + CTC-распознаватель по скрипту. Не потокобезопасен.
class PaddleOcrEngine {
public:
    explicit PaddleOcrEngine(std::filesystem::path models_root = {});
    ~PaddleOcrEngine();
    PaddleOcrEngine(const PaddleOcrEngine&) = delete;
    PaddleOcrEngine& operator=(const PaddleOcrEngine&) = delete;

    // Загружает распознаватель для языка (по скрипту). Повторный вызов с
    // языком того же скрипта — no-op. Бросает std::runtime_error при сбое.
    void initialize(std::string_view language_code);
    void initialize_multi(const std::vector<std::string>& language_codes);
    void reset();
    bool is_initialized() const;
    std::string language_code() const;

    // BGRA (4 байта) или grayscale (1 байт). Распознаёт строки текста и
    // возвращает разметку с координатами в пикселях исходного изображения.
    OcrLayout recognize_layout(
        const std::uint8_t* image,
        int width,
        int height,
        int bytes_per_line,
        int bytes_per_pixel);

    // recognize_layout + подбор ориентации страницы моделью doc_ori.
    // rotation_degrees (если задан) получает применённый угол (0/90/180/270).
    OcrLayout recognize_layout_oriented(
        const std::uint8_t* image,
        int width,
        int height,
        int bytes_per_line,
        int bytes_per_pixel,
        int* rotation_degrees = nullptr);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace offline_translator
