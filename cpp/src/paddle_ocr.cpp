#include "offline_translator/paddle_ocr.hpp"

#include "file_transfer.hpp"

#include "onnxruntime_c_api.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <utility>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace offline_translator {
namespace {

const OrtApi* ort() {
    static const OrtApi* api = OrtGetApiBase()->GetApi(ORT_API_VERSION);
    if (api == nullptr) {
        throw std::runtime_error("onnxruntime: не удалось получить API");
    }
    return api;
}

void ort_check(OrtStatus* status, std::string_view what) {
    if (status == nullptr) {
        return;
    }
    const char* message = ort()->GetErrorMessage(status);
    std::string text = message != nullptr ? message : "unknown";
    ort()->ReleaseStatus(status);
    throw std::runtime_error(
        std::string("onnxruntime: ") + std::string(what) + ": " + text);
}

// --- модели и скрипты ------------------------------------------------------

std::filesystem::path env_home_ppocr() {
    const char* home = std::getenv("OFFLINE_TRANSLATOR_HOME");
    if (home == nullptr || *home == '\0') {
        return {};
    }
    return std::filesystem::path(home) / "ppocr";
}

std::filesystem::path exe_data_ppocr() {
#if defined(_WIN32)
    std::array<wchar_t, 32768> buffer{};
    const DWORD length = GetModuleFileNameW(
        nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) {
        return {};
    }
    std::filesystem::path exe(buffer.data(), buffer.data() + length);
    return exe.parent_path() / "data" / "ppocr";
#else
    return {};
#endif
}

std::filesystem::path user_home_ppocr() {
    const char* profile = std::getenv("USERPROFILE");
    if (profile == nullptr || *profile == '\0') {
        return {};
    }
    return std::filesystem::path(profile) / ".local" / "share" /
        "offline-translator" / "ppocr";
}

}  // namespace

std::string ppocr_script_for_language(std::string_view code) {
    static const std::unordered_map<std::string, std::string> kMap = {
        {"en", "latin"}, {"de", "latin"}, {"fr", "latin"}, {"es", "latin"},
        {"it", "latin"}, {"pt", "latin"}, {"nl", "latin"}, {"pl", "latin"},
        {"cs", "latin"}, {"sk", "latin"}, {"sl", "latin"}, {"hr", "latin"},
        {"sr", "latin"}, {"bs", "latin"}, {"hu", "latin"}, {"ro", "latin"},
        {"tr", "latin"}, {"az", "latin"}, {"uz", "latin"}, {"id", "latin"},
        {"ms", "latin"}, {"vi", "latin"}, {"sv", "latin"}, {"da", "latin"},
        {"no", "latin"}, {"nb", "latin"}, {"fi", "latin"}, {"et", "latin"},
        {"lt", "latin"}, {"lv", "latin"}, {"sq", "latin"}, {"af", "latin"},
        {"sw", "latin"}, {"tl", "latin"}, {"ca", "latin"}, {"gl", "latin"},
        {"eu", "latin"}, {"cy", "latin"}, {"ga", "latin"}, {"is", "latin"},
        {"ru", "eslav"}, {"uk", "eslav"}, {"be", "eslav"}, {"bg", "eslav"},
        {"zh", "chinese"}, {"zt", "chinese"}, {"ja", "chinese"},
    };
    const auto it = kMap.find(std::string(code));
    if (it == kMap.end()) {
        return {};
    }
    return it->second;
}

std::filesystem::path default_ppocr_root() {
    for (std::filesystem::path candidate :
         {env_home_ppocr(), exe_data_ppocr(), user_home_ppocr()}) {
        if (candidate.empty()) {
            continue;
        }
        std::error_code ec;
        if (std::filesystem::exists(candidate / "det.onnx", ec) && !ec) {
            return candidate;
        }
    }
    const auto exe = exe_data_ppocr();
    if (!exe.empty()) {
        return exe;
    }
    const auto home = user_home_ppocr();
    if (!home.empty()) {
        return home;
    }
    return {};
}

bool ppocr_installed(const std::filesystem::path& root) {
    const auto dir = root.empty() ? default_ppocr_root() : root;
    if (dir.empty()) {
        return false;
    }
    std::error_code ec;
    const bool has_det = std::filesystem::exists(dir / "det.onnx", ec);
    const bool has_rec =
        std::filesystem::exists(dir / "rec_latin.onnx", ec) ||
        std::filesystem::exists(dir / "rec_eslav.onnx", ec);
    return has_det && has_rec;
}

bool ppocr_script_installed(
    std::string_view script,
    const std::filesystem::path& root) {
    const auto dir = root.empty() ? default_ppocr_root() : root;
    if (dir.empty() || script.empty()) {
        return false;
    }
    std::error_code ec;
    const auto base = dir / ("rec_" + std::string(script) + ".onnx");
    const auto dict = dir / ("dict_" + std::string(script) + ".txt");
    return std::filesystem::exists(base, ec) &&
        std::filesystem::exists(dict, ec);
}

void download_ppocr_script(
    std::string_view script,
    const ProgressCallback& progress,
    const std::filesystem::path& root) {
    if (script.empty()) {
        throw std::runtime_error("ppocr: не задан скрипт распознавателя");
    }
    const auto dir = root.empty() ? default_ppocr_root() : root;
    if (dir.empty()) {
        throw std::runtime_error("ppocr: не найден каталог моделей");
    }
    std::filesystem::create_directories(dir);
    const std::string base =
        "https://huggingface.co/monkt/paddleocr-onnx/resolve/main/languages/" +
        std::string(script);
    download_resumable(
        {base + "/rec.onnx"},
        dir / ("rec_" + std::string(script) + ".onnx"),
        progress,
        "Модель OCR «" + std::string(script) + "»");
    download_resumable(
        {base + "/dict.txt"},
        dir / ("dict_" + std::string(script) + ".txt"),
        progress,
        "Словарь OCR «" + std::string(script) + "»");
}

namespace {
bool accurate_det_from_env() {
    const char* env = std::getenv("TLING_OCR_ACCURATE");
    if (env == nullptr) {
        return false;
    }
    const std::string value(env);
    return value == "1" || value == "accurate";
}
bool g_accurate_det = accurate_det_from_env();
}  // пространство имён

void set_ppocr_accurate(bool accurate) {
    g_accurate_det = accurate;
}

bool ppocr_accurate() {
    return g_accurate_det;
}

bool ppocr_server_det_installed(const std::filesystem::path& root) {
    const auto dir = root.empty() ? default_ppocr_root() : root;
    if (dir.empty()) {
        return false;
    }
    std::error_code ec;
    return std::filesystem::exists(dir / "det_server.onnx", ec) && !ec;
}

void download_ppocr_server_det(
    const ProgressCallback& progress,
    const std::filesystem::path& root) {
    const auto dir = root.empty() ? default_ppocr_root() : root;
    if (dir.empty()) {
        throw std::runtime_error("ppocr: не найден каталог моделей");
    }
    std::filesystem::create_directories(dir);
    download_resumable(
        {"https://www.modelscope.cn/models/RapidAI/RapidOCR/resolve/"
         "v3.10.0/onnx/PP-OCRv5/det/ch_PP-OCRv5_det_server.onnx"},
        dir / "det_server.onnx",
        progress,
        "Точная модель OCR (детектор PP-OCRv5)");
}

namespace {

// --- изображения -----------------------------------------------------------

struct Rgb {
    int width{0};
    int height{0};
    std::vector<std::uint8_t> pixels;  // RGB, 3 байта на пиксель
};

Rgb to_rgb(
    const std::uint8_t* image,
    int width,
    int height,
    int bytes_per_line,
    int bytes_per_pixel) {
    Rgb out;
    out.width = width;
    out.height = height;
    out.pixels.resize(static_cast<std::size_t>(width) * height * 3);
    for (int y = 0; y < height; ++y) {
        const std::uint8_t* row = image + static_cast<std::size_t>(y) *
            bytes_per_line;
        std::uint8_t* dst =
            out.pixels.data() + static_cast<std::size_t>(y) * width * 3;
        for (int x = 0; x < width; ++x) {
            if (bytes_per_pixel == 1) {
                const std::uint8_t g = row[x];
                dst[x * 3 + 0] = g;
                dst[x * 3 + 1] = g;
                dst[x * 3 + 2] = g;
            } else {
                const std::uint8_t* px = row + x * bytes_per_pixel;
                dst[x * 3 + 0] = px[2];
                dst[x * 3 + 1] = px[1];
                dst[x * 3 + 2] = px[0];
            }
        }
    }
    return out;
}

std::uint8_t clamp_u8(double value) {
    if (value <= 0.0) {
        return 0;
    }
    if (value >= 255.0) {
        return 255;
    }
    return static_cast<std::uint8_t>(value + 0.5);
}

Rgb resize_rgb(const Rgb& src, int dst_width, int dst_height) {
    Rgb out;
    out.width = std::max(dst_width, 1);
    out.height = std::max(dst_height, 1);
    out.pixels.resize(
        static_cast<std::size_t>(out.width) * out.height * 3);
    if (src.width <= 0 || src.height <= 0) {
        return out;
    }
    const double scale_x =
        static_cast<double>(src.width) / out.width;
    const double scale_y =
        static_cast<double>(src.height) / out.height;
    for (int y = 0; y < out.height; ++y) {
        const double sy = (y + 0.5) * scale_y - 0.5;
        int y0 = static_cast<int>(std::floor(sy));
        double fy = sy - y0;
        if (y0 < 0) {
            y0 = 0;
            fy = 0.0;
        }
        int y1 = std::min(y0 + 1, src.height - 1);
        if (y0 > src.height - 1) {
            y0 = src.height - 1;
        }
        for (int x = 0; x < out.width; ++x) {
            const double sx = (x + 0.5) * scale_x - 0.5;
            int x0 = static_cast<int>(std::floor(sx));
            double fx = sx - x0;
            if (x0 < 0) {
                x0 = 0;
                fx = 0.0;
            }
            int x1 = std::min(x0 + 1, src.width - 1);
            if (x0 > src.width - 1) {
                x0 = src.width - 1;
            }
            for (int c = 0; c < 3; ++c) {
                const double p00 =
                    src.pixels[(static_cast<std::size_t>(y0) * src.width + x0) * 3 + c];
                const double p01 =
                    src.pixels[(static_cast<std::size_t>(y0) * src.width + x1) * 3 + c];
                const double p10 =
                    src.pixels[(static_cast<std::size_t>(y1) * src.width + x0) * 3 + c];
                const double p11 =
                    src.pixels[(static_cast<std::size_t>(y1) * src.width + x1) * 3 + c];
                const double top = p00 * (1.0 - fx) + p01 * fx;
                const double bottom = p10 * (1.0 - fx) + p11 * fx;
                const double value = top * (1.0 - fy) + bottom * fy;
                out.pixels[(static_cast<std::size_t>(y) * out.width + x) * 3 + c] =
                    clamp_u8(value);
            }
        }
    }
    return out;
}

Rgb rotate_rgb(const Rgb& src, int angle) {
    angle = ((angle % 360) + 360) % 360;
    if (angle == 0 || src.width <= 0 || src.height <= 0) {
        return src;
    }
    Rgb out;
    if (angle == 180) {
        out.width = src.width;
        out.height = src.height;
        out.pixels.resize(src.pixels.size());
        for (int y = 0; y < src.height; ++y) {
            for (int x = 0; x < src.width; ++x) {
                const std::size_t sidx =
                    (static_cast<std::size_t>(y) * src.width + x) * 3;
                const std::size_t didx =
                    (static_cast<std::size_t>(src.height - 1 - y) * src.width +
                     (src.width - 1 - x)) * 3;
                out.pixels[didx + 0] = src.pixels[sidx + 0];
                out.pixels[didx + 1] = src.pixels[sidx + 1];
                out.pixels[didx + 2] = src.pixels[sidx + 2];
            }
        }
        return out;
    }
    out.width = src.height;
    out.height = src.width;
    out.pixels.resize(src.pixels.size());
    for (int y = 0; y < src.height; ++y) {
        for (int x = 0; x < src.width; ++x) {
            const std::size_t sidx =
                (static_cast<std::size_t>(y) * src.width + x) * 3;
            int dx = 0;
            int dy = 0;
            if (angle == 90) {  // по часовой стрелке
                dx = src.height - 1 - y;
                dy = x;
            } else {  // 270
                dx = y;
                dy = src.width - 1 - x;
            }
            const std::size_t didx =
                (static_cast<std::size_t>(dy) * out.width + dx) * 3;
            out.pixels[didx + 0] = src.pixels[sidx + 0];
            out.pixels[didx + 1] = src.pixels[sidx + 1];
            out.pixels[didx + 2] = src.pixels[sidx + 2];
        }
    }
    return out;
}

// --- сессии ONNX Runtime ---------------------------------------------------

struct Session {
    OrtSession* session{nullptr};
    std::string input_name;
    std::string output_name;
};

struct SharedOrtEnv {
    OrtEnv* env{nullptr};
    ~SharedOrtEnv() {
        if (env != nullptr) {
            ort()->ReleaseEnv(env);
        }
    }
};

SharedOrtEnv& shared_env() {
    static SharedOrtEnv instance;
    static std::once_flag flag;
    std::call_once(flag, []() {
        ort_check(
            ort()->CreateEnv(
                ORT_LOGGING_LEVEL_WARNING, "tling-ppocr", &instance.env),
            "CreateEnv");
    });
    return instance;
}

Session open_session(const std::filesystem::path& model_path) {
    OrtSessionOptions* options = nullptr;
    ort_check(ort()->CreateSessionOptions(&options), "CreateSessionOptions");
    ort()->SetIntraOpNumThreads(
        options, static_cast<int>(std::max(1u, std::thread::hardware_concurrency())));
    ort()->SetSessionGraphOptimizationLevel(options, ORT_ENABLE_ALL);
    ort()->SetSessionExecutionMode(options, ORT_SEQUENTIAL);

    Session result;
    ort_check(
        ort()->CreateSession(
            shared_env().env,
            model_path.wstring().c_str(),
            options,
            &result.session),
        "CreateSession " + model_path.string());
    ort()->ReleaseSessionOptions(options);

    OrtAllocator* allocator = nullptr;
    ort_check(
        ort()->GetAllocatorWithDefaultOptions(&allocator), "GetAllocator");
    char* input_name = nullptr;
    ort_check(
        ort()->SessionGetInputName(result.session, 0, allocator, &input_name),
        "SessionGetInputName");
    if (input_name != nullptr) {
        result.input_name = input_name;
        ort()->AllocatorFree(allocator, input_name);
    }
    char* output_name = nullptr;
    ort_check(
        ort()->SessionGetOutputName(result.session, 0, allocator, &output_name),
        "SessionGetOutputName");
    if (output_name != nullptr) {
        result.output_name = output_name;
        ort()->AllocatorFree(allocator, output_name);
    }
    return result;
}

void close_session(Session& session) {
    if (session.session != nullptr) {
        ort()->ReleaseSession(session.session);
        session.session = nullptr;
    }
}

// Распознаватель одного скрипта (latin/eslav/...) + его словарь.
struct Recognizer {
    std::string script;
    Session session;
    std::vector<std::string> chars;
};

std::vector<float> run_float(
    Session& session,
    const std::vector<float>& input,
    const std::vector<std::int64_t>& shape,
    std::vector<std::int64_t>& out_shape) {
    OrtMemoryInfo* memory = nullptr;
    ort_check(
        ort()->CreateCpuMemoryInfo(
            OrtArenaAllocator, OrtMemTypeDefault, &memory),
        "CreateCpuMemoryInfo");

    OrtValue* input_value = nullptr;
    ort_check(
        ort()->CreateTensorWithDataAsOrtValue(
            memory,
            const_cast<float*>(input.data()),
            input.size() * sizeof(float),
            shape.data(),
            shape.size(),
            ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT,
            &input_value),
        "CreateTensorWithDataAsOrtValue");

    const char* input_names[] = {session.input_name.c_str()};
    const char* output_names[] = {session.output_name.c_str()};
    OrtValue* output_value = nullptr;
    ort_check(
        ort()->Run(
            session.session,
            nullptr,
            input_names,
            &input_value,
            1,
            output_names,
            1,
            &output_value),
        "Run");
    ort()->ReleaseValue(input_value);

    OrtTensorTypeAndShapeInfo* info = nullptr;
    ort_check(
        ort()->GetTensorTypeAndShape(output_value, &info),
        "GetTensorTypeAndShape");
    std::size_t rank = 0;
    ort()->GetDimensionsCount(info, &rank);
    std::array<std::int64_t, 8> dims{};
    ort()->GetDimensions(info, dims.data(), std::min<std::size_t>(rank, 8));
    out_shape.assign(dims.begin(), dims.begin() + std::min<std::size_t>(rank, 8));
    ort()->ReleaseTensorTypeAndShapeInfo(info);

    std::size_t total = 1;
    for (const auto dim : out_shape) {
        total *= static_cast<std::size_t>(std::max<std::int64_t>(dim, 1));
    }
    float* raw = nullptr;
    ort_check(
        ort()->GetTensorMutableData(output_value, reinterpret_cast<void**>(&raw)),
        "GetTensorMutableData");
    std::vector<float> result(raw, raw + total);
    ort()->ReleaseValue(output_value);
    ort()->ReleaseMemoryInfo(memory);
    return result;
}

// --- нормализация ----------------------------------------------------------

std::vector<float> to_nchw(
    const Rgb& image,
    const std::array<float, 3>& mean,
    const std::array<float, 3>& std) {
    const std::size_t plane =
        static_cast<std::size_t>(image.width) * image.height;
    std::vector<float> out(plane * 3);
    for (std::size_t i = 0; i < plane; ++i) {
        for (int c = 0; c < 3; ++c) {
            const float value =
                static_cast<float>(image.pixels[i * 3 + c]) / 255.0f;
            out[static_cast<std::size_t>(c) * plane + i] =
                (value - mean[c]) / std[c];
        }
    }
    return out;
}

// --- детектор --------------------------------------------------------------

struct Box {
    int left{0};
    int top{0};
    int right{0};
    int bottom{0};
    double score{0.0};
};

int find_root(std::vector<int>& parent, int a) {
    while (parent[a] != a) {
        parent[a] = parent[parent[a]];
        a = parent[a];
    }
    return a;
}

std::vector<Box> detect_boxes(
    Session& det_session,
    const Rgb& rgb,
    int limit_side_len,
    double threshold = 0.3,
    double box_threshold = 0.55) {
    const int h = rgb.height;
    const int w = rgb.width;
    if (h <= 0 || w <= 0) {
        return {};
    }
    double ratio = 1.0;
    if (std::max(h, w) > limit_side_len) {
        ratio = static_cast<double>(limit_side_len) / std::max(h, w);
    }
    int resized_h = std::max(32, static_cast<int>(std::lround(
        h * ratio / 32.0)) * 32);
    int resized_w = std::max(32, static_cast<int>(std::lround(
        w * ratio / 32.0)) * 32);

    const Rgb resized = resize_rgb(rgb, resized_w, resized_h);
    const auto input = to_nchw(
        resized,
        {0.485f, 0.456f, 0.406f},
        {0.229f, 0.224f, 0.225f});
    std::vector<std::int64_t> shape = {1, 3, resized_h, resized_w};
    std::vector<std::int64_t> out_shape;
    const auto prob = run_float(det_session, input, shape, out_shape);
    if (out_shape.size() != 4) {
        return {};
    }
    const int map_h = static_cast<int>(out_shape[2]);
    const int map_w = static_cast<int>(out_shape[3]);

    const double kThreshold = threshold;
    const double kBoxThreshold = box_threshold;
    constexpr double kUnclip = 1.5;
    std::vector<std::uint8_t> mask(
        static_cast<std::size_t>(map_h) * map_w, 0);
    for (int i = 0; i < map_h * map_w; ++i) {
        mask[static_cast<std::size_t>(i)] =
            prob[static_cast<std::size_t>(i)] > kThreshold ? 1 : 0;
    }

    std::vector<int> labels(
        static_cast<std::size_t>(map_h) * map_w, -1);
    std::vector<int> parent;
    int next_label = 0;
    std::vector<std::array<int, 3>> prev_runs;  // start, end, label
    std::vector<std::array<int, 3>> cur_runs;
    for (int y = 0; y < map_h; ++y) {
        cur_runs.clear();
        const std::size_t row = static_cast<std::size_t>(y) * map_w;
        int x = 0;
        while (x < map_w) {
            if (mask[row + x] == 0) {
                ++x;
                continue;
            }
            const int start = x;
            while (x < map_w && mask[row + x] != 0) {
                ++x;
            }
            const int end = x;  // половина открыта
            int label = -1;
            for (const auto& run : prev_runs) {
                if (run[0] < end && start < run[1]) {
                    if (label == -1) {
                        label = find_root(parent, run[2]);
                    } else {
                        const int other = find_root(parent, run[2]);
                        if (other != label) {
                            parent[other] = label;
                        }
                    }
                }
            }
            if (label == -1) {
                label = next_label++;
                parent.push_back(label);
            }
            for (int px = start; px < end; ++px) {
                labels[row + px] = label;
            }
            cur_runs.push_back({start, end, label});
        }
        prev_runs.swap(cur_runs);
    }

    struct Accum {
        int min_x{1 << 30};
        int min_y{1 << 30};
        int max_x{-1};
        int max_y{-1};
        int count{0};
        double sum{0.0};
    };
    std::vector<Accum> accums(static_cast<std::size_t>(next_label));
    for (int y = 0; y < map_h; ++y) {
        for (int x = 0; x < map_w; ++x) {
            const int label = labels[static_cast<std::size_t>(y) * map_w + x];
            if (label < 0) {
                continue;
            }
            const int root = find_root(parent, label);
            Accum& acc = accums[static_cast<std::size_t>(root)];
            acc.min_x = std::min(acc.min_x, x);
            acc.min_y = std::min(acc.min_y, y);
            acc.max_x = std::max(acc.max_x, x);
            acc.max_y = std::max(acc.max_y, y);
            acc.count += 1;
            acc.sum += prob[static_cast<std::size_t>(y) * map_w + x];
        }
    }

    const double scale_y = static_cast<double>(map_h) / h;
    const double scale_x = static_cast<double>(map_w) / w;
    std::vector<Box> boxes;
    for (const auto& acc : accums) {
        if (acc.count < 16 || acc.max_x < 0) {
            continue;
        }
        const int bw = acc.max_x - acc.min_x + 1;
        const int bh = acc.max_y - acc.min_y + 1;
        if (std::min(bw, bh) < 3) {
            continue;
        }
        if (acc.sum / acc.count < kBoxThreshold) {
            continue;
        }
        const double dx = bw * (kUnclip - 1.0) / 2.0;
        const double dy = bh * (kUnclip - 1.0) / 2.0;
        Box box;
        box.left = std::clamp(
            static_cast<int>(std::lround((acc.min_x - dx) / scale_x)), 0, w - 1);
        box.top = std::clamp(
            static_cast<int>(std::lround((acc.min_y - dy) / scale_y)), 0, h - 1);
        box.right = std::clamp(
            static_cast<int>(std::lround((acc.max_x + dx) / scale_x)), 0, w - 1);
        box.bottom = std::clamp(
            static_cast<int>(std::lround((acc.max_y + dy) / scale_y)), 0, h - 1);
        box.score = acc.sum / acc.count;
        if (box.right - box.left >= 2 && box.bottom - box.top >= 2) {
            boxes.push_back(box);
        }
    }
    return boxes;
}

// --- распознавание строк ---------------------------------------------------

// Широкий бокс может оказаться склейкой нескольких полей (шапка документа и
// номер справа: детектор сливает области одной высоты). Разбиваем такой бокс
// по широким вертикальным промежуткам без текста.
std::vector<Box> split_wide_boxes(const Rgb& rgb, const std::vector<Box>& boxes) {
    std::vector<Box> out;
    out.reserve(boxes.size());
    for (const Box& box : boxes) {
        const int bw = box.right - box.left + 1;
        const int bh = box.bottom - box.top + 1;
        if (bw < 3 * bh || bw < 40 || bh < 6) {
            out.push_back(box);
            continue;
        }
        std::vector<int> lums;
        lums.reserve(512);
        for (int y = box.top; y <= box.bottom; y += 2) {
            for (int x = box.left; x <= box.right; x += 2) {
                const std::uint8_t* p = rgb.pixels.data() +
                    (static_cast<std::size_t>(y) * rgb.width + x) * 3;
                lums.push_back(p[0] + p[1] + p[2]);
            }
        }
        if (lums.empty()) {
            out.push_back(box);
            continue;
        }
        std::sort(lums.begin(), lums.end());
        const int bg = lums[lums.size() * 9 / 10];
        const auto is_text = [&](int x, int y) {
            const std::uint8_t* p = rgb.pixels.data() +
                (static_cast<std::size_t>(y) * rgb.width + x) * 3;
            const int r = p[0];
            const int g = p[1];
            const int b = p[2];
            const int lum = r + g + b;
            if (lum < bg - 70) {
                return true;
            }
            const int mx = std::max(r, std::max(g, b));
            const int mn = std::min(r, std::min(g, b));
            return mx - mn > 80 && lum < bg - 20;  // насыщенный: печать, форма
        };
        std::vector<int> col(bw, 0);
        for (int x = 0; x < bw; ++x) {
            for (int y = box.top; y <= box.bottom; ++y) {
                if (is_text(box.left + x, y)) {
                    col[x] += 1;
                }
            }
        }
        const int gap_level = std::max(2, bh / 12);
        int first = 0;
        while (first < bw && col[first] <= gap_level) {
            ++first;
        }
        int last = bw - 1;
        while (last > first && col[last] <= gap_level) {
            --last;
        }
        const int gap_min = std::max(10, bh);
        std::vector<int> cuts;
        int run = 0;
        for (int x = first; x <= last; ++x) {
            if (col[x] <= gap_level) {
                ++run;
                continue;
            }
            if (run >= gap_min) {
                cuts.push_back(x - run / 2);
            }
            run = 0;
        }
        if (cuts.empty() || cuts.size() > 7) {
            out.push_back(box);
            continue;
        }
        std::vector<Box> parts;
        int start = first;
        for (const int cut : cuts) {
            parts.push_back(Box{
                box.left + start, box.top, box.left + cut - 1, box.bottom,
                box.score});
            start = cut;
        }
        parts.push_back(Box{
            box.left + start, box.top, box.left + last, box.bottom, box.score});
        // Узкие обрывки (одна-две буквы) сливаем обратно с соседним полем.
        const int min_w = bh * 6 / 5;
        for (std::size_t i = 0; i < parts.size();) {
            const int pw = parts[i].right - parts[i].left + 1;
            if (parts.size() > 1 && pw < min_w) {
                if (i + 1 < parts.size() &&
                    (i == 0 ||
                     parts[i + 1].right - parts[i + 1].left >=
                         parts[i - 1].right - parts[i - 1].left)) {
                    parts[i + 1].left = parts[i].left;
                } else {
                    parts[i - 1].right = parts[i].right;
                }
                parts.erase(parts.begin() + static_cast<std::ptrdiff_t>(i));
                continue;
            }
            ++i;
        }
        if (parts.size() < 2) {
            out.push_back(box);
            continue;
        }
        for (auto& part : parts) {
            part.left = std::clamp(part.left, 0, rgb.width - 1);
            part.right = std::clamp(part.right, 0, rgb.width - 1);
            out.push_back(part);
        }
    }
    return out;
}

// Вертикальные колонки иероглифов (ярлыки форм) детектор видит как цепочку
// квадратных боксов-глифов (часть теряется). Сцепляем квадраты одного столбца
// в высокий бокс — его распознаватель прочтёт лентой глифов.
void merge_vertical_columns(std::vector<Box>& boxes) {
    const std::size_t n = boxes.size();
    std::vector<int> parent(n);
    for (std::size_t i = 0; i < n; ++i) {
        parent[i] = static_cast<int>(i);
    }
    const auto squareish = [&](const Box& b) {
        const int w = b.right - b.left + 1;
        const int h = b.bottom - b.top + 1;
        return w * 10 >= h * 5 && h * 10 >= w * 5;  // 0.5..2.0
    };
    for (std::size_t i = 0; i < n; ++i) {
        if (!squareish(boxes[i])) {
            continue;
        }
        for (std::size_t j = i + 1; j < n; ++j) {
            if (!squareish(boxes[j])) {
                continue;
            }
            const Box& a = boxes[i];
            const Box& b = boxes[j];
            const int aw = a.right - a.left + 1;
            const int bw = b.right - b.left + 1;
            const int ah = a.bottom - a.top + 1;
            const int bh = b.bottom - b.top + 1;
            const int min_w = std::min(aw, bw);
            const int ix = std::max(
                0,
                std::min(a.right, b.right) - std::max(a.left, b.left));
            if (ix * 10 < min_w * 6) {
                continue;
            }
            const int ca = (a.left + a.right) / 2;
            const int cb = (b.left + b.right) / 2;
            if (std::abs(ca - cb) * 10 > min_w * 4) {
                continue;
            }
            const int gap = std::max(a.top, b.top) - std::min(a.bottom, b.bottom);
            if (gap < -2 || gap * 10 > std::min(ah, bh) * 16) {
                continue;
            }
            parent[find_root(parent, static_cast<int>(j))] =
                find_root(parent, static_cast<int>(i));
        }
    }
    std::vector<Box> merged;
    std::vector<int> count(n, 0);
    for (std::size_t i = 0; i < n; ++i) {
        count[static_cast<std::size_t>(find_root(parent, static_cast<int>(i)))] += 1;
    }
    std::vector<bool> done(n, false);
    for (std::size_t i = 0; i < n; ++i) {
        const int root = find_root(parent, static_cast<int>(i));
        if (count[static_cast<std::size_t>(root)] >= 2) {
            if (done[static_cast<std::size_t>(root)]) {
                continue;
            }
            done[static_cast<std::size_t>(root)] = true;
            Box box = boxes[i];
            for (std::size_t j = 0; j < n; ++j) {
                if (find_root(parent, static_cast<int>(j)) != root) {
                    continue;
                }
                box.left = std::min(box.left, boxes[j].left);
                box.top = std::min(box.top, boxes[j].top);
                box.right = std::max(box.right, boxes[j].right);
                box.bottom = std::max(box.bottom, boxes[j].bottom);
                box.score = std::max(box.score, boxes[j].score);
            }
            merged.push_back(box);
        } else {
            merged.push_back(boxes[i]);
        }
    }
    boxes = std::move(merged);
}

std::vector<std::string> load_chars(const std::filesystem::path& dict_path) {
    std::ifstream stream(dict_path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error(
            "ppocr: не найден словарь " + dict_path.string());
    }
    std::string data(
        (std::istreambuf_iterator<char>(stream)),
        std::istreambuf_iterator<char>());
    std::vector<std::string> chars;
    std::string current;
    for (std::size_t i = 0; i < data.size(); ++i) {
        const char c = data[i];
        if (c == '\n') {
            if (!(current.empty() && i + 1 == data.size())) {
                chars.push_back(current);
            }
            current.clear();
        } else if (c != '\r') {
            current.push_back(c);
        }
    }
    // CTC: индекс 0 — пустой символ, далее символы словаря + пробел.
    chars.push_back(" ");
    return chars;
}

struct TextResult {
    std::string text;
    double confidence{0.0};
    bool vertical{false};
};

TextResult recognize_line(
    Session& rec_session,
    const std::vector<std::string>& chars,
    const Rgb& rgb,
    const Box& box,
    Session* textline_ori_session) {
    TextResult result;
    const int pad = 2;
    const int x0 = std::max(0, box.left - pad);
    const int y0 = std::max(0, box.top - pad);
    const int x1 = std::min(rgb.width, box.right + pad);
    const int y1 = std::min(rgb.height, box.bottom + pad);
    const int crop_w = x1 - x0;
    const int crop_h = y1 - y0;
    if (crop_w < 2 || crop_h < 2) {
        return result;
    }

    Rgb crop;
    crop.width = crop_w;
    crop.height = crop_h;
    crop.pixels.resize(static_cast<std::size_t>(crop_w) * crop_h * 3);
    for (int y = 0; y < crop_h; ++y) {
        const std::uint8_t* src =
            rgb.pixels.data() + (static_cast<std::size_t>(y0 + y) *
                rgb.width + x0) * 3;
        std::memcpy(
            crop.pixels.data() + static_cast<std::size_t>(y) * crop_w * 3,
            src,
            static_cast<std::size_t>(crop_w) * 3);
    }

    // Вертикальная колонка (ярлыки формы: 购买方, 销售方, 密码区):
    // распознаватель читает только горизонтальные строки, поэтому
    // раскладываем глифы сверху-вниз в горизонтальную ленту слева-направо.
    int strip_w = crop_w;
    int strip_h = crop_h;
    bool vertical = false;
    if (crop_h >= crop_w * 2 + 4 && crop_w >= 6) {
        const int glyphs = std::clamp(
            static_cast<int>(
                std::lround(static_cast<double>(crop_h) / crop_w)),
            2, 24);
        Rgb strip;
        strip.width = glyphs * 48;
        strip.height = 48;
        strip.pixels.assign(
            static_cast<std::size_t>(strip.width) * 48 * 3, 255);
        for (int i = 0; i < glyphs; ++i) {
            Rgb cell;
            const int top = crop_h * i / glyphs;
            const int bottom = crop_h * (i + 1) / glyphs;
            cell.width = crop_w;
            cell.height = std::max(1, bottom - top);
            cell.pixels.resize(
                static_cast<std::size_t>(cell.width) * cell.height * 3);
            for (int y = 0; y < cell.height; ++y) {
                std::memcpy(
                    cell.pixels.data() +
                        static_cast<std::size_t>(y) * crop_w * 3,
                    crop.pixels.data() +
                        static_cast<std::size_t>(top + y) * crop_w * 3,
                    static_cast<std::size_t>(crop_w) * 3);
            }
            const Rgb sized = resize_rgb(cell, 48, 48);
            std::memcpy(
                strip.pixels.data() +
                    static_cast<std::size_t>(i) * 48 * 48 * 3,
                sized.pixels.data(),
                static_cast<std::size_t>(48) * 48 * 3);
        }
        crop = std::move(strip);
        strip_w = crop.width;
        strip_h = crop.height;
        vertical = true;
        result.vertical = true;
    }

    if (!vertical && textline_ori_session != nullptr) {
        const Rgb probe = resize_rgb(crop, 160, 80);
        const auto input = to_nchw(
            probe,
            {0.485f, 0.456f, 0.406f},
            {0.229f, 0.224f, 0.225f});
        std::vector<std::int64_t> probe_shape = {1, 3, 80, 160};
        std::vector<std::int64_t> probe_out_shape;
        const auto classes = run_float(
            *textline_ori_session, input, probe_shape, probe_out_shape);
        if (classes.size() >= 2 && classes[1] > classes[0]) {
            crop = rotate_rgb(crop, 180);
        }
    }

    constexpr int kTargetHeight = 48;
    int target_w = static_cast<int>(std::lround(
        static_cast<double>(strip_w) * kTargetHeight / strip_h));
    target_w = std::clamp(target_w, 1, 1600);
    const Rgb resized = resize_rgb(crop, target_w, kTargetHeight);
    const auto input = to_nchw(resized, {0.5f, 0.5f, 0.5f}, {0.5f, 0.5f, 0.5f});
    std::vector<std::int64_t> shape = {1, 3, kTargetHeight, target_w};
    std::vector<std::int64_t> out_shape;
    const auto output = run_float(rec_session, input, shape, out_shape);
    if (out_shape.size() != 3) {
        return result;
    }
    const int steps = static_cast<int>(out_shape[1]);
    const int classes = static_cast<int>(out_shape[2]);
    if (steps <= 0 || classes <= 0 ||
        output.size() < static_cast<std::size_t>(steps) * classes) {
        return result;
    }

    std::string text;
    double confidence_sum = 0.0;
    int emitted = 0;
    int previous = -1;
    for (int t = 0; t < steps; ++t) {
        const float* row =
            output.data() + static_cast<std::size_t>(t) * classes;
        int best = 0;
        float best_value = row[0];
        for (int c = 1; c < classes; ++c) {
            if (row[c] > best_value) {
                best_value = row[c];
                best = c;
            }
        }
        if (best != 0 && best != previous) {
            const std::size_t char_index = static_cast<std::size_t>(best - 1);
            if (char_index < chars.size()) {
                text += chars[char_index];
                confidence_sum += best_value;
                emitted += 1;
            }
        }
        previous = best;
    }
    result.text = text;
    result.confidence = emitted > 0 ? confidence_sum / emitted * 100.0 : 0.0;
    return result;
}

// Мусор распознавания: детектор срабатывает на водяные знаки/штампы и
// модель выдаёт короткий низкоуверенный текст ("cw", "cMw"). Отбрасываем
// такое, чтобы не засорять разметку и перевод.
bool is_noise_text(const std::string& text, double confidence) {
    if (confidence >= 80.0) {
        return false;
    }
    std::size_t codepoints = 0;
    std::size_t tokens = 0;
    std::size_t single_letter = 0;
    std::size_t current = 0;
    for (const unsigned char symbol : text) {
        if ((symbol & 0xC0) == 0x80) {
            continue;  // продолжение UTF-8, не отдельный символ
        }
        ++codepoints;
        if (symbol == ' ' || symbol == '\t') {
            if (current > 0) {
                ++tokens;
                if (current == 1) {
                    ++single_letter;
                }
                current = 0;
            }
        } else {
            ++current;
        }
    }
    if (current > 0) {
        ++tokens;
        if (current == 1) {
            ++single_letter;
        }
    }
    if (confidence < 45.0) {
        return true;
    }
    if (codepoints <= 5 && confidence < 65.0) {
        return true;
    }
    // Набор одиночных «букв» через пробел — шум от водяных знаков/марок.
    if (tokens >= 3 && confidence < 75.0 &&
        single_letter * 2 >= tokens) {
        return true;
    }
    return false;
}

int detect_doc_angle(Session& doc_ori_session, const Rgb& rgb) {
    const Rgb resized = resize_rgb(rgb, 224, 224);
    const auto input = to_nchw(
        resized,
        {0.485f, 0.456f, 0.406f},
        {0.229f, 0.224f, 0.225f});
    std::vector<std::int64_t> shape = {1, 3, 224, 224};
    std::vector<std::int64_t> out_shape;
    const auto output = run_float(doc_ori_session, input, shape, out_shape);
    if (output.empty()) {
        return 0;
    }
    int best = 0;
    for (std::size_t i = 1; i < output.size(); ++i) {
        if (output[i] > output[best]) {
            best = static_cast<int>(i);
        }
    }
    return best * 90;
}

void sort_into_lines(std::vector<Box>& boxes) {
    std::sort(
        boxes.begin(),
        boxes.end(),
        [](const Box& a, const Box& b) {
            if (a.top != b.top) {
                return a.top < b.top;
            }
            return a.left < b.left;
        });
}

OcrLayout recognize_rgb(
    Session& det_session,
    std::vector<Recognizer>& recognizers,
    Session* textline_ori_session,
    const Rgb& rgb) {
    OcrLayout layout;
    layout.width = rgb.width;
    layout.height = rgb.height;
    // Мелкие/тонкие строки на сканах детектор пропускает: ищем на
    // увеличенном изображении, а боксы возвращаем в исходный масштаб.
    double detect_scale = 1.0;
    Rgb detect_image = rgb;
    const int longest = std::max(rgb.width, rgb.height);
    if (longest > 0 && longest < 3000) {
        detect_scale = std::min(3.0, 3000.0 / longest);
        detect_image = resize_rgb(
            rgb,
            static_cast<int>(std::lround(rgb.width * detect_scale)),
            static_cast<int>(std::lround(rgb.height * detect_scale)));
    }
    auto boxes = detect_boxes(det_session, detect_image, 3000);
    // Вертикальные колонки (ярлыки форм вроде 购买方): вторым проходом
    // детектора по изображению, повёрнутому на 90°, — там они становятся
    // горизонтальными строками. Оставляем только ставшие вертикальными
    // в исходной ориентации и не покрытые обычными боксами.
    {
        const int dh = detect_image.height;
        const Rgb rotated = rotate_rgb(detect_image, 90);
        const auto vboxes = detect_boxes(det_session, rotated, 3000, 0.2, 0.4);
        for (const auto& vb : vboxes) {
            Box box;
            box.left = vb.top;
            box.right = vb.bottom;
            box.top = dh - 1 - vb.right;
            box.bottom = dh - 1 - vb.left;
            box.score = vb.score;
            const int bw = box.right - box.left + 1;
            const int bh = box.bottom - box.top + 1;
            if (bh * 5 < bw * 4) {
                continue;
            }
            bool covered = false;
            for (const auto& eb : boxes) {
                const int ix = std::max(
                    0,
                    std::min(box.right, eb.right) -
                        std::max(box.left, eb.left));
                const int iy = std::max(
                    0,
                    std::min(box.bottom, eb.bottom) -
                        std::max(box.top, eb.top));
                if (ix * iy * 10 >= bw * bh * 6) {
                    covered = true;
                    break;
                }
            }
            if (!covered) {
                boxes.push_back(box);
            }
        }
    }
    if (detect_scale > 1.0) {
        for (auto& box : boxes) {
            box.left = static_cast<int>(std::lround(box.left / detect_scale));
            box.right = static_cast<int>(std::lround(box.right / detect_scale));
            box.top = static_cast<int>(std::lround(box.top / detect_scale));
            box.bottom =
                static_cast<int>(std::lround(box.bottom / detect_scale));
        }
    }
    boxes = split_wide_boxes(rgb, boxes);
    // Грубый бокс-обрубок, почти целиком накрывающий точную строку,
    // убираем: иначе текст строки дублируется в разметке.
    for (std::size_t i = 0; i < boxes.size();) {
        const Box& a = boxes[i];
        const int aa = (a.right - a.left + 1) * (a.bottom - a.top + 1);
        const int ah = a.bottom - a.top + 1;
        bool coarse = false;
        for (std::size_t j = 0; j < boxes.size() && !coarse; ++j) {
            if (j == i) {
                continue;
            }
            const Box& b = boxes[j];
            const int bb = (b.right - b.left + 1) * (b.bottom - b.top + 1);
            const int bh = b.bottom - b.top + 1;
            if (aa * 10 < bb * 14 || ah * 5 < bh * 8) {
                continue;
            }
            const int cx = (b.left + b.right) / 2;
            const int cy = (b.top + b.bottom) / 2;
            if (cx < a.left || cx > a.right || cy < a.top || cy > a.bottom) {
                continue;
            }
            const int ix = std::max(
                0,
                std::min(a.right, b.right) - std::max(a.left, b.left));
            const int iy = std::max(
                0,
                std::min(a.bottom, b.bottom) - std::max(a.top, b.top));
            if (ix * iy * 2 >= bb) {
                coarse = true;
            }
        }
        if (coarse) {
            boxes.erase(boxes.begin() + static_cast<std::ptrdiff_t>(i));
        } else {
            ++i;
        }
    }
    merge_vertical_columns(boxes);
    sort_into_lines(boxes);

    int median_height = 20;
    if (!boxes.empty()) {
        std::vector<int> all;
        all.reserve(boxes.size());
        for (const auto& box : boxes) {
            all.push_back(box.bottom - box.top);
        }
        std::sort(all.begin(), all.end());
        median_height = std::max(1, all[all.size() / 2]);
    }

    // Смешанные скрипты (eng+rus): основной скрипт — доминирующий на
    // странице (по средней уверенности). Отдельный бокс может «переключиться»
    // на другой скрипт, если тот уверен заметно выше (например, английская
    // строка на кириллической странице) или если основной прочитал пусто.
    std::vector<std::vector<TextResult>> candidates;
    std::vector<int> script_index(boxes.size(), 0);
    if (recognizers.size() > 1 && !boxes.empty()) {
        candidates.resize(boxes.size());
        std::vector<double> score_sum(recognizers.size(), 0.0);
        std::vector<int> score_count(recognizers.size(), 0);
        for (std::size_t i = 0; i < boxes.size(); ++i) {
            candidates[i].resize(recognizers.size());
            for (std::size_t j = 0; j < recognizers.size(); ++j) {
                candidates[i][j] = recognize_line(
                    recognizers[j].session,
                    recognizers[j].chars,
                    rgb,
                    boxes[i],
                    textline_ori_session);
                if (!candidates[i][j].text.empty()) {
                    score_sum[j] += candidates[i][j].confidence;
                    score_count[j] += 1;
                }
            }
        }
        int primary = 0;
        double best_average = -1.0;
        for (std::size_t j = 0; j < recognizers.size(); ++j) {
            const double average = score_count[j] > 0
                ? score_sum[j] / score_count[j]
                : 0.0;
            if (average > best_average) {
                best_average = average;
                primary = static_cast<int>(j);
            }
        }
        constexpr double kOverrideMargin = 20.0;
        for (std::size_t i = 0; i < boxes.size(); ++i) {
            int chosen = primary;
            double chosen_confidence =
                candidates[i][static_cast<std::size_t>(primary)].confidence;
            bool chosen_empty =
                candidates[i][static_cast<std::size_t>(primary)].text.empty();
            for (std::size_t j = 0; j < recognizers.size(); ++j) {
                if (static_cast<int>(j) == primary) {
                    continue;
                }
                const TextResult& other = candidates[i][j];
                if (other.text.empty()) {
                    continue;
                }
                if (chosen_empty ||
                    other.confidence > chosen_confidence + kOverrideMargin) {
                    chosen = static_cast<int>(j);
                    chosen_confidence = other.confidence;
                    chosen_empty = false;
                }
            }
            script_index[i] = chosen;
        }
    }

    int line_index = -1;
    int current_top = 0;
    int current_bottom = 0;
    bool has_line = false;
    for (std::size_t box_index = 0; box_index < boxes.size(); ++box_index) {
        const Box& box = boxes[box_index];
        TextResult text;
        if (recognizers.size() > 1) {
            text = candidates[box_index]
                             [static_cast<std::size_t>(script_index[box_index])];
        } else {
            for (auto& recognizer : recognizers) {
                const TextResult candidate = recognize_line(
                    recognizer.session,
                    recognizer.chars,
                    rgb,
                    box,
                    textline_ori_session);
                if (candidate.confidence > text.confidence) {
                    text = candidate;
                }
            }
        }
        if (text.text.empty() ||
            (text.vertical
                 ? text.confidence < 45.0
                 : is_noise_text(text.text, text.confidence))) {
            continue;
        }
        const int center = (box.top + box.bottom) / 2;
        if (!has_line ||
            std::abs(center - (current_top + current_bottom) / 2) >
                median_height * 3 / 4) {
            ++line_index;
            current_top = box.top;
            current_bottom = box.bottom;
            has_line = true;
        } else {
            current_top = std::min(current_top, box.top);
            current_bottom = std::max(current_bottom, box.bottom);
        }
        OcrWordBox word;
        word.text = text.text;
        word.left = box.left;
        word.top = box.top;
        word.width = box.right - box.left;
        word.height = box.bottom - box.top;
        word.block = 0;
        word.paragraph = 0;
        word.line = line_index;
        word.confidence = text.confidence;
        layout.words.push_back(std::move(word));
    }
    return layout;
}

}  // namespace

struct PaddleOcrEngine::Impl {
    std::filesystem::path root;
    Session det;
    Session doc_ori;
    Session textline_ori;
    std::vector<Recognizer> recognizers;
    std::string language;
    bool loaded{false};
};

PaddleOcrEngine::PaddleOcrEngine(std::filesystem::path models_root)
    : impl_(std::make_unique<Impl>()) {
    impl_->root = models_root.empty() ? default_ppocr_root() : std::move(models_root);
}

PaddleOcrEngine::~PaddleOcrEngine() {
    reset();
}

void PaddleOcrEngine::reset() {
    if (!impl_) {
        return;
    }
    for (auto& recognizer : impl_->recognizers) {
        close_session(recognizer.session);
    }
    impl_->recognizers.clear();
    close_session(impl_->det);
    close_session(impl_->doc_ori);
    close_session(impl_->textline_ori);
    impl_->language.clear();
    impl_->loaded = false;
}

bool PaddleOcrEngine::is_initialized() const {
    return impl_ != nullptr && impl_->loaded && !impl_->recognizers.empty();
}

std::string PaddleOcrEngine::language_code() const {
    return impl_ != nullptr ? impl_->language : std::string();
}

void PaddleOcrEngine::initialize(std::string_view language_code) {
    const std::string script = ppocr_script_for_language(language_code);
    if (script.empty()) {
        throw std::runtime_error(
            "ppocr: нет распознавателя для языка " +
            std::string(language_code));
    }
    const auto dir = impl_->root.empty() ? default_ppocr_root() : impl_->root;
    if (dir.empty()) {
        throw std::runtime_error("ppocr: не найден каталог моделей");
    }
    if (impl_->det.session == nullptr) {
        const auto server_det = dir / "det_server.onnx";
        std::error_code ec;
        if (g_accurate_det && std::filesystem::exists(server_det, ec) && !ec) {
            impl_->det = open_session(server_det);
        } else {
            impl_->det = open_session(dir / "det.onnx");
        }
    }
    if (impl_->doc_ori.session == nullptr) {
        const auto ori_path = dir / "doc_ori.onnx";
        std::error_code ec;
        if (std::filesystem::exists(ori_path, ec)) {
            impl_->doc_ori = open_session(ori_path);
        }
    }
    if (impl_->textline_ori.session == nullptr) {
        const auto line_path = dir / "textline_ori.onnx";
        std::error_code ec;
        if (std::filesystem::exists(line_path, ec)) {
            impl_->textline_ori = open_session(line_path);
        }
    }
    const auto existing = std::find_if(
        impl_->recognizers.begin(),
        impl_->recognizers.end(),
        [&script](const Recognizer& recognizer) {
            return recognizer.script == script;
        });
    if (existing == impl_->recognizers.end()) {
        const auto rec_path = dir / ("rec_" + script + ".onnx");
        std::error_code ec;
        if (!std::filesystem::exists(rec_path, ec)) {
            throw std::runtime_error(
                "ppocr: не найдена модель " + rec_path.string());
        }
        Recognizer recognizer;
        recognizer.script = script;
        recognizer.session = open_session(rec_path);
        recognizer.chars = load_chars(dir / ("dict_" + script + ".txt"));
        impl_->recognizers.push_back(std::move(recognizer));
    }
    impl_->language = std::string(language_code);
    impl_->loaded = true;
}

void PaddleOcrEngine::initialize_multi(
    const std::vector<std::string>& language_codes) {
    // Каждый распознаватель покрывает скрипт; грузим по одному на скрипт,
    // чтобы распознавать смешанный текст (например, eng+rus на экране).
    bool any = false;
    for (const auto& code : language_codes) {
        if (!ppocr_script_for_language(code).empty()) {
            initialize(code);
            any = true;
        }
    }
    if (!any && !language_codes.empty()) {
        initialize(language_codes.front());
    }
}

OcrLayout PaddleOcrEngine::recognize_layout(
    const std::uint8_t* image,
    int width,
    int height,
    int bytes_per_line,
    int bytes_per_pixel) {
    if (!is_initialized()) {
        throw std::runtime_error("ppocr: движок не инициализирован");
    }
    const Rgb rgb =
        to_rgb(image, width, height, bytes_per_line, bytes_per_pixel);
    return recognize_rgb(
        impl_->det,
        impl_->recognizers,
        impl_->textline_ori.session != nullptr ? &impl_->textline_ori
                                               : nullptr,
        rgb);
}

OcrLayout PaddleOcrEngine::recognize_layout_oriented(
    const std::uint8_t* image,
    int width,
    int height,
    int bytes_per_line,
    int bytes_per_pixel,
    int* rotation_degrees) {
    if (rotation_degrees != nullptr) {
        *rotation_degrees = 0;
    }
    if (!is_initialized()) {
        throw std::runtime_error("ppocr: движок не инициализирован");
    }
    Rgb original =
        to_rgb(image, width, height, bytes_per_line, bytes_per_pixel);
    if (impl_->doc_ori.session == nullptr) {
        return recognize_rgb(
            impl_->det,
            impl_->recognizers,
            impl_->textline_ori.session != nullptr ? &impl_->textline_ori
                                                   : nullptr,
            original);
    }
    const int angle = detect_doc_angle(impl_->doc_ori, original);
    if (angle == 0) {
        return recognize_rgb(
            impl_->det,
            impl_->recognizers,
            impl_->textline_ori.session != nullptr ? &impl_->textline_ori
                                                   : nullptr,
            original);
    }
    const Rgb rotated = rotate_rgb(original, -angle);
    if (rotation_degrees != nullptr) {
        // Угол по часовой стрелке, на которое надо повернуть ИСХОДНОЕ
        // изображение, чтобы получить координаты раскладки: та же конвенция,
        // что у Tesseract-пути и rotate_bgra_image (-angle == +(360-angle)).
        *rotation_degrees = (360 - angle) % 360;
    }
    return recognize_rgb(
        impl_->det,
        impl_->recognizers,
        impl_->textline_ori.session != nullptr ? &impl_->textline_ori
                                               : nullptr,
        rotated);
}

}  // namespace offline_translator
