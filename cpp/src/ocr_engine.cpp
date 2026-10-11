#include "offline_translator/ocr_engine.hpp"

#include "offline_translator/app_log.hpp"
#include "offline_translator/app_settings.hpp"

#include "file_transfer.hpp"
#include "fs_utils.hpp"

#include <tesseract/baseapi.h>

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace offline_translator {
namespace {

// ISO-код приложения → код языковых данных Tesseract. Совпадает с
// tessdata_fast: нет данных только для тагальского (tl).
const std::unordered_map<std::string, std::string>& language_table() {
    static const std::unordered_map<std::string, std::string> table{
        {"en", "eng"},
        {"ru", "rus"},
        {"de", "deu"},
        {"fr", "fra"},
        {"es", "spa"},
        {"it", "ita"},
        {"pt", "por"},
        {"zh", "chi_sim"},
        {"zt", "chi_tra"},
        {"ja", "jpn"},
        {"ko", "kor"},
        {"ar", "ara"},
        {"uk", "ukr"},
        {"pl", "pol"},
        {"tr", "tur"},
        {"nl", "nld"},
        {"cs", "ces"},
        {"sv", "swe"},
        {"fi", "fin"},
        {"el", "ell"},
        {"he", "heb"},
        {"hi", "hin"},
        {"id", "ind"},
        {"az", "aze"},
        {"be", "bel"},
        {"bg", "bul"},
        {"bn", "ben"},
        {"bs", "bos"},
        {"ca", "cat"},
        {"da", "dan"},
        {"et", "est"},
        {"fa", "fas"},
        {"gu", "guj"},
        {"hr", "hrv"},
        {"hu", "hun"},
        {"is", "isl"},
        {"kn", "kan"},
        {"lt", "lit"},
        {"lv", "lav"},
        {"ml", "mal"},
        {"ms", "msa"},
        {"mt", "mlt"},
        {"nb", "nor"},
        {"nn", "nor"},
        {"ro", "ron"},
        {"sk", "slk"},
        {"sl", "slv"},
        {"sq", "sqi"},
        {"sr", "srp"},
        {"ta", "tam"},
        {"te", "tel"},
        {"th", "tha"},
        {"vi", "vie"},
        {"mk", "mkd"},
        {"gl", "glg"},
        {"ur", "urd"},
        {"ka", "kat"},
        {"eo", "epo"},
        {"eu", "eus"},
        {"ga", "gle"},
        {"ky", "kir"},
        {"pb", "por"},
        {"sw", "swa"},
    };
    return table;
}

std::filesystem::path resolve_tessdata_root(
    const std::filesystem::path& given) {
    if (!given.empty()) {
        return given;
    }
    return default_tessdata_root();
}

std::uint8_t to_gray(std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    // ITU-R BT.601: яркость = 0.299R + 0.587G + 0.114B.
    const auto value =
        (static_cast<unsigned>(r) * 299u + static_cast<unsigned>(g) * 587u +
         static_cast<unsigned>(b) * 114u) /
        1000u;
    return static_cast<std::uint8_t>(value > 255u ? 255u : value);
}

std::vector<std::uint8_t> bgra_to_gray(
    const std::uint8_t* image,
    int width,
    int height,
    int bytes_per_line) {
    std::vector<std::uint8_t> gray(
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
    for (int y = 0; y < height; ++y) {
        const std::uint8_t* row =
            image + static_cast<std::size_t>(y) * bytes_per_line;
        std::uint8_t* out =
            gray.data() + static_cast<std::size_t>(y) * width;
        for (int x = 0; x < width; ++x) {
            const std::uint8_t* pixel = row + static_cast<std::size_t>(x) * 4;
            out[x] = to_gray(pixel[2], pixel[1], pixel[0]);
        }
    }
    return gray;
}

void trim_trailing_whitespace(std::string& text) {
    while (!text.empty()) {
        const char last = text.back();
        if (last != '\n' && last != '\r' && last != ' ' && last != '\t') {
            break;
        }
        text.pop_back();
    }
}

}  // namespace

std::string tesseract_language_code(std::string_view code) {
    if (code.empty()) {
        throw std::runtime_error("Не задан язык OCR");
    }
    const auto found = language_table().find(std::string(code));
    if (found == language_table().end()) {
        throw std::runtime_error(
            "Язык «" + std::string(code) + "» не поддерживается OCR Tesseract");
    }
    return found->second;
}

bool tesseract_language_supported(std::string_view code) {
    return language_table().find(std::string(code)) != language_table().end();
}

std::string tessdata_file_name(std::string_view code) {
    return tesseract_language_code(code) + ".traineddata";
}

std::filesystem::path default_tessdata_root() {
    return default_data_root() / "tessdata";
}

std::filesystem::path tessdata_path(
    std::string_view code,
    const std::filesystem::path& tessdata_root) {
    return resolve_tessdata_root(tessdata_root) / tessdata_file_name(code);
}

bool tessdata_installed(
    std::string_view code,
    const std::filesystem::path& tessdata_root) {
    const auto path = tessdata_path(code, tessdata_root);
    return fs_utils::is_regular_file(path) && fs_utils::file_size_or_zero(path) > 0;
}

void download_tessdata(
    std::string_view code,
    const ProgressCallback& progress,
    const std::filesystem::path& tessdata_root) {
    const std::string name = tessdata_file_name(code);
    const auto root = resolve_tessdata_root(tessdata_root);
    const auto destination = root / name;
    const std::vector<std::string> urls{
        "https://raw.githubusercontent.com/tesseract-ocr/tessdata_fast/main/" +
            name,
        "https://cdn.jsdelivr.net/gh/tesseract-ocr/tessdata_fast@main/" + name,
    };
    download_resumable(
        urls,
        destination,
        progress,
        "Языковые данные OCR «" + std::string(code) + "»");
}

struct OcrEngine::Impl {
    std::filesystem::path tessdata_root;
    tesseract::TessBaseAPI api;
    std::string language_code;
    bool initialized{false};
};

OcrEngine::OcrEngine(std::filesystem::path tessdata_root)
    : impl_(std::make_unique<Impl>()) {
    impl_->tessdata_root = resolve_tessdata_root(tessdata_root);
}

OcrEngine::~OcrEngine() {
    if (impl_ && impl_->initialized) {
        impl_->api.End();
    }
}

void OcrEngine::initialize(std::string_view language_code) {
    const std::string tesseract_code = tesseract_language_code(language_code);
    if (impl_->initialized && impl_->language_code == std::string(language_code)) {
        return;
    }
    reset();

    const auto data_file = impl_->tessdata_root / tessdata_file_name(language_code);
    if (impl_->tessdata_root.empty() || !fs_utils::is_regular_file(data_file) ||
        fs_utils::file_size_or_zero(data_file) == 0) {
        throw std::runtime_error(
            "Нет языковых данных OCR для «" + std::string(language_code) +
            "»: " + data_file.string() +
            ". Скачайте их (файл " + tessdata_file_name(language_code) + ").");
    }

    const std::string datapath = impl_->tessdata_root.string();
    if (impl_->api.Init(datapath.c_str(), tesseract_code.c_str()) != 0) {
        throw std::runtime_error(
            "Не удалось загрузить языковые данные OCR («" + tesseract_code +
            "»).");
    }
    impl_->language_code = std::string(language_code);
    impl_->initialized = true;
}

void OcrEngine::reset() {
    if (!impl_) {
        return;
    }
    if (impl_->initialized) {
        impl_->api.End();
    }
    impl_->initialized = false;
    impl_->language_code.clear();
}

bool OcrEngine::is_initialized() const {
    return impl_ && impl_->initialized;
}

std::string OcrEngine::language_code() const {
    return impl_ ? impl_->language_code : std::string{};
}

OcrResult OcrEngine::recognize(
    const std::uint8_t* image,
    int width,
    int height,
    int bytes_per_line,
    int bytes_per_pixel) {
    if (!impl_->initialized) {
        throw std::runtime_error(
            "OCR не инициализирован: сначала вызовите initialize()");
    }
    if (image == nullptr || width <= 0 || height <= 0 || bytes_per_line <= 0) {
        throw std::invalid_argument("OCR: пустое изображение");
    }

    std::vector<std::uint8_t> gray;
    const std::uint8_t* data = image;
    int line = bytes_per_line;
    if (bytes_per_pixel == 1) {
        // Уже градации серого — передаём как есть.
    } else if (bytes_per_pixel == 4) {
        gray = bgra_to_gray(image, width, height, bytes_per_line);
        data = gray.data();
        line = width;
    } else {
        throw std::invalid_argument(
            "OCR: поддерживаются 1 (серый) или 4 (BGRA) байта на пиксель");
    }

    impl_->api.SetImage(data, width, height, 1, line);
    if (impl_->api.Recognize(nullptr) != 0) {
        impl_->api.Clear();
        throw std::runtime_error("OCR: распознавание не удалось");
    }

    char* raw = impl_->api.GetUTF8Text();
    std::string text = raw != nullptr ? std::string(raw) : std::string{};
    delete[] raw;

    const int confidence = impl_->api.MeanTextConf();
    impl_->api.Clear();

    trim_trailing_whitespace(text);
    return OcrResult{
        std::move(text),
        confidence < 0 ? 0.0 : static_cast<double>(confidence)};
}

}  // namespace offline_translator
