#include "offline_translator/ocr_engine.hpp"

#include "offline_translator/app_log.hpp"
#include "offline_translator/app_settings.hpp"

#include "file_transfer.hpp"
#include "fs_utils.hpp"

#ifdef OT_PADDLE_OCR
#include "offline_translator/paddle_ocr.hpp"
#endif

#include <tesseract/baseapi.h>

#include <cstddef>
#include <cstdint>
#include <array>
#include <cstring>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace offline_translator {
namespace {

OcrBackend g_ocr_backend = OcrBackend::Tesseract;
std::filesystem::path g_ppocr_root;

}  // namespace

void set_ocr_backend(OcrBackend backend, std::filesystem::path ppocr_root) {
    g_ocr_backend = backend;
    g_ppocr_root = std::move(ppocr_root);
}

OcrBackend ocr_backend() {
    return g_ocr_backend;
}

bool ocr_paddle_ready() {
#ifdef OT_PADDLE_OCR
    return g_ocr_backend == OcrBackend::Paddle && ppocr_installed(g_ppocr_root);
#else
    return false;
#endif
}

namespace {

// Поддерживает ли Paddle (PP-OCR) язык: есть распознаватель для его скрипта.
bool paddle_supports_language(std::string_view code) {
#ifdef OT_PADDLE_OCR
    return !ppocr_script_for_language(code).empty();
#else
    (void)code;
    return false;
#endif
}

bool paddle_supports_all(const std::vector<std::string>& codes) {
    for (const auto& code : codes) {
        if (!paddle_supports_language(code)) {
            return false;
        }
    }
    return !codes.empty();
}

}  // namespace

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

// Подготовка данных для Tesseract: BGRA переводим в градации серого,
// серое отдаём как есть.
struct GrayImage {
    std::vector<std::uint8_t> storage;
    const std::uint8_t* data{nullptr};
    int bytes_per_line{0};
};

GrayImage make_gray_image(
    const std::uint8_t* image,
    int width,
    int height,
    int bytes_per_line,
    int bytes_per_pixel) {
    if (bytes_per_pixel == 1) {
        return GrayImage{{}, image, bytes_per_line};
    }
    if (bytes_per_pixel == 4) {
        GrayImage result;
        result.storage = bgra_to_gray(image, width, height, bytes_per_line);
        result.data = result.storage.data();
        result.bytes_per_line = width;
        return result;
    }
    throw std::invalid_argument(
        "OCR: поддерживаются 1 (серый) или 4 (BGRA) байта на пиксель");
}

// TSV Tesseract: level page block par line word left top width height conf text.
OcrLayout parse_tsv_layout(const char* tsv, int width, int height) {
    OcrLayout layout;
    layout.width = width;
    layout.height = height;
    if (tsv == nullptr) {
        return layout;
    }
    const std::string text(tsv);
    std::size_t position = 0;
    bool header = true;
    const auto to_int = [](const std::string& value) {
        try {
            return std::stoi(value);
        } catch (...) {
            return 0;
        }
    };
    const auto to_double = [](const std::string& value) {
        try {
            return std::stod(value);
        } catch (...) {
            return 0.0;
        }
    };
    while (position <= text.size()) {
        std::size_t end = text.find('\n', position);
        if (end == std::string::npos) {
            end = text.size();
        }
        std::string row = text.substr(position, end - position);
        position = end + 1;
        if (!row.empty() && row.back() == '\r') {
            row.pop_back();
        }
        if (row.empty()) {
            if (end == text.size()) {
                break;
            }
            continue;
        }
        if (header) {
            header = false;
            continue;
        }
        std::array<std::string, 12> fields{};
        std::size_t begin = 0;
        std::size_t field = 0;
        for (std::size_t index = 0;
             index < row.size() && field < 11;
             ++index) {
            if (row[index] == '\t') {
                fields[field++] = row.substr(begin, index - begin);
                begin = index + 1;
            }
        }
        fields[field] = row.substr(begin);
        if (field < 11) {
            continue;
        }
        if (to_int(fields[0]) != 5) {
            continue;
        }
        OcrWordBox word;
        word.block = to_int(fields[2]);
        word.paragraph = to_int(fields[3]);
        word.line = to_int(fields[4]);
        word.left = to_int(fields[6]);
        word.top = to_int(fields[7]);
        word.width = to_int(fields[8]);
        word.height = to_int(fields[9]);
        word.confidence = to_double(fields[10]);
        word.text = fields[11];
        if (word.text.empty() || word.left < 0 || word.top < 0 ||
            word.width <= 0 || word.height <= 0) {
            continue;
        }
        layout.words.push_back(std::move(word));
        if (end == text.size()) {
            break;
        }
    }
    return layout;
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
    bool paddle_mode{false};
#ifdef OT_PADDLE_OCR
    std::unique_ptr<PaddleOcrEngine> paddle;
#endif
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
#ifdef OT_PADDLE_OCR
    if (ocr_paddle_ready() && paddle_supports_language(language_code)) {
        if (!impl_->paddle) {
            impl_->paddle = std::make_unique<PaddleOcrEngine>(g_ppocr_root);
        }
        impl_->paddle->initialize(language_code);
        impl_->paddle_mode = true;
        impl_->language_code = std::string(language_code);
        return;
    }
#endif
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
    impl_->api.SetPageSegMode(tesseract::PSM_AUTO);
    impl_->language_code = std::string(language_code);
    impl_->initialized = true;
}

void OcrEngine::initialize_multi(
    const std::vector<std::string>& language_codes) {
    if (language_codes.empty()) {
        throw std::runtime_error("Не задан язык OCR");
    }
#ifdef OT_PADDLE_OCR
    if (ocr_paddle_ready() && paddle_supports_all(language_codes)) {
        if (!impl_->paddle) {
            impl_->paddle = std::make_unique<PaddleOcrEngine>(g_ppocr_root);
        }
        impl_->paddle->initialize_multi(language_codes);
        impl_->paddle_mode = true;
        impl_->language_code = impl_->paddle->language_code();
        app_log_info(
            "paddle OCR: languages=" + impl_->language_code);
        return;
    }
    if (ocr_paddle_ready()) {
        app_log_info(
            "paddle OCR: языки без распознавателя PP-OCR — откат на Tesseract");
    }
#endif
    std::string spec;
    std::string cache_key;
    for (const auto& code : language_codes) {
        const std::string tesseract_code = tesseract_language_code(code);
        if (!spec.empty()) {
            spec += "+";
        }
        spec += tesseract_code;
        if (!cache_key.empty()) {
            cache_key += "+";
        }
        cache_key += code;
    }
    if (impl_->initialized && impl_->language_code == cache_key) {
        return;
    }
    reset();
    for (const auto& code : language_codes) {
        const auto data_file =
            impl_->tessdata_root / tessdata_file_name(code);
        if (impl_->tessdata_root.empty() ||
            !fs_utils::is_regular_file(data_file) ||
            fs_utils::file_size_or_zero(data_file) == 0) {
            throw std::runtime_error(
                "Нет языковых данных OCR для «" + code + "»: " +
                data_file.string() + ". Скачайте их (файл " +
                tessdata_file_name(code) + ").");
        }
    }
    const std::string datapath = impl_->tessdata_root.string();
    if (impl_->api.Init(datapath.c_str(), spec.c_str()) != 0) {
        throw std::runtime_error(
            "Не удалось загрузить языковые данные OCR («" + spec + "»).");
    }
    impl_->api.SetPageSegMode(tesseract::PSM_AUTO);
    impl_->language_code = cache_key;
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
#ifdef OT_PADDLE_OCR
    if (impl_->paddle) {
        impl_->paddle->reset();
    }
#endif
    impl_->paddle_mode = false;
}

bool OcrEngine::is_initialized() const {
    if (impl_ && impl_->paddle_mode) {
#ifdef OT_PADDLE_OCR
        return impl_->paddle && impl_->paddle->is_initialized();
#else
        return false;
#endif
    }
    return impl_ && impl_->initialized;
}

bool OcrEngine::uses_paddle() const {
    return impl_ && impl_->paddle_mode;
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
#ifdef OT_PADDLE_OCR
    if (impl_->paddle_mode && impl_->paddle) {
        const OcrLayout layout = impl_->paddle->recognize_layout(
            image, width, height, bytes_per_line, bytes_per_pixel);
        OcrResult result;
        double confidence_sum = 0.0;
        int confident = 0;
        int previous_line = -1;
        for (const auto& word : layout.words) {
            if (!result.text.empty()) {
                result.text += word.line == previous_line ? ' ' : '\n';
            }
            result.text += word.text;
            previous_line = word.line;
            if (word.confidence > 0.0) {
                confidence_sum += word.confidence;
                ++confident;
            }
        }
        result.confidence = confident > 0 ? confidence_sum / confident : 0.0;
        return result;
    }
#endif
    if (!impl_->initialized) {
        throw std::runtime_error(
            "OCR не инициализирован: сначала вызовите initialize()");
    }
    if (image == nullptr || width <= 0 || height <= 0 || bytes_per_line <= 0) {
        throw std::invalid_argument("OCR: пустое изображение");
    }

    const GrayImage gray = make_gray_image(
        image,
        width,
        height,
        bytes_per_line,
        bytes_per_pixel);

    impl_->api.SetImage(gray.data, width, height, 1, gray.bytes_per_line);
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

OcrLayout OcrEngine::recognize_layout(
    const std::uint8_t* image,
    int width,
    int height,
    int bytes_per_line,
    int bytes_per_pixel) {
#ifdef OT_PADDLE_OCR
    if (impl_->paddle_mode && impl_->paddle) {
        return impl_->paddle->recognize_layout(
            image, width, height, bytes_per_line, bytes_per_pixel);
    }
#endif
    if (!impl_->initialized) {
        throw std::runtime_error(
            "OCR не инициализирован: сначала вызовите initialize()");
    }
    if (image == nullptr || width <= 0 || height <= 0 || bytes_per_line <= 0) {
        throw std::invalid_argument("OCR: пустое изображение");
    }

    const GrayImage gray = make_gray_image(
        image,
        width,
        height,
        bytes_per_line,
        bytes_per_pixel);

    impl_->api.SetImage(gray.data, width, height, 1, gray.bytes_per_line);
    if (impl_->api.Recognize(nullptr) != 0) {
        impl_->api.Clear();
        throw std::runtime_error("OCR: распознавание не удалось");
    }

    char* tsv = impl_->api.GetTSVText(0);
    OcrLayout layout = parse_tsv_layout(tsv, width, height);
    delete[] tsv;
    impl_->api.Clear();
    return layout;
}

OcrLayout OcrEngine::recognize_layout_oriented(
    const std::uint8_t* image,
    int width,
    int height,
    int bytes_per_line,
    int bytes_per_pixel,
    int* rotation_degrees) {
#ifdef OT_PADDLE_OCR
    if (impl_->paddle_mode && impl_->paddle) {
        return impl_->paddle->recognize_layout_oriented(
            image,
            width,
            height,
            bytes_per_line,
            bytes_per_pixel,
            rotation_degrees);
    }
#endif
    if (rotation_degrees != nullptr) {
        *rotation_degrees = 0;
    }
    return recognize_layout(image, width, height, bytes_per_line, bytes_per_pixel);
}

namespace {

// Поворот BGRA на 90/180/270. Для других форматов поворот не поддержан.
std::vector<std::uint8_t> rotate_bgra(
    const std::uint8_t* image,
    int width,
    int height,
    int bytes_per_line,
    int rotation,
    int* out_width,
    int* out_height) {
    std::vector<std::uint8_t> result;
    const auto source = [&](int x, int y) {
        return image + static_cast<std::size_t>(y) * bytes_per_line +
            static_cast<std::size_t>(x) * 4;
    };
    if (rotation == 180) {
        *out_width = width;
        *out_height = height;
        result.resize(
            static_cast<std::size_t>(width) *
            static_cast<std::size_t>(height) * 4);
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                std::uint8_t* target = result.data() +
                    (static_cast<std::size_t>(height - 1 - y) * width +
                     (width - 1 - x)) * 4;
                std::memcpy(target, source(x, y), 4);
            }
        }
        return result;
    }
    *out_width = height;
    *out_height = width;
    result.resize(
        static_cast<std::size_t>(width) *
        static_cast<std::size_t>(height) * 4);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            std::size_t target_index = 0;
            if (rotation == 90) {
                target_index = static_cast<std::size_t>(x) * height +
                    static_cast<std::size_t>(height - 1 - y);
            } else {
                target_index = static_cast<std::size_t>(width - 1 - x) *
                    height + static_cast<std::size_t>(y);
            }
            std::memcpy(result.data() + target_index * 4, source(x, y), 4);
        }
    }
    return result;
}

}  // анонимное пространство имён

std::vector<std::uint8_t> rotate_bgra_image(
    const std::uint8_t* image,
    int width,
    int height,
    int bytes_per_line,
    int rotation,
    int* out_width,
    int* out_height) {
    if (rotation == 0) {
        if (out_width != nullptr) {
            *out_width = width;
        }
        if (out_height != nullptr) {
            *out_height = height;
        }
        std::vector<std::uint8_t> result(
            static_cast<std::size_t>(width) *
            static_cast<std::size_t>(height) * 4);
        for (int y = 0; y < height; ++y) {
            std::memcpy(
                result.data() + static_cast<std::size_t>(y) * width * 4,
                image + static_cast<std::size_t>(y) * bytes_per_line,
                static_cast<std::size_t>(width) * 4);
        }
        return result;
    }
    return rotate_bgra(
        image,
        width,
        height,
        bytes_per_line,
        rotation,
        out_width,
        out_height);
}

OcrLayout recognize_best_orientation(
    OcrEngine& engine,
    const std::uint8_t* image,
    int width,
    int height,
    int bytes_per_line,
    int bytes_per_pixel,
    int* rotation_degrees) {
    if (rotation_degrees != nullptr) {
        *rotation_degrees = 0;
    }
    if (image == nullptr || width <= 0 || height <= 0 ||
        bytes_per_line <= 0) {
        throw std::invalid_argument("OCR: пустое изображение");
    }
    if (engine.uses_paddle()) {
        return engine.recognize_layout_oriented(
            image,
            width,
            height,
            bytes_per_line,
            bytes_per_pixel,
            rotation_degrees);
    }
    OcrLayout best;
    int best_score = -1;
    const int rotations[4] = {0, 90, 270, 180};
    for (const int rotation : rotations) {
        if (bytes_per_pixel != 4 && rotation != 0) {
            break;
        }
        int rotated_width = width;
        int rotated_height = height;
        int stride = bytes_per_line;
        const std::uint8_t* data = image;
        std::vector<std::uint8_t> rotated;
        if (rotation != 0) {
            rotated = rotate_bgra(
                image,
                width,
                height,
                bytes_per_line,
                rotation,
                &rotated_width,
                &rotated_height);
            data = rotated.data();
            stride = rotated_width * 4;
        }
        OcrLayout layout = engine.recognize_layout(
            data,
            rotated_width,
            rotated_height,
            stride,
            bytes_per_pixel);
        int confident_words = 0;
        double confidence_sum = 0.0;
        for (const auto& word : layout.words) {
            if (word.confidence > 0.0) {
                ++confident_words;
                confidence_sum += word.confidence;
            }
        }
        // Средняя уверенность важнее количества: на повёрнутом мусоре
        // Tesseract выдаёт много слов, но с низкой уверенностью.
        const double average = confident_words > 0
            ? confidence_sum / confident_words
            : 0.0;
        const int score = static_cast<int>(average * 1000.0) +
            std::min(confident_words, 999);
        if (score > best_score) {
            best_score = score;
            best = std::move(layout);
            if (rotation_degrees != nullptr) {
                *rotation_degrees = rotation;
            }
        }
        // Уверенно распознали без поворота — остальные углы не нужны.
        if (rotation == 0 && average >= 75.0 && confident_words >= 2) {
            break;
        }
    }
    return best;
}

}  // namespace offline_translator
