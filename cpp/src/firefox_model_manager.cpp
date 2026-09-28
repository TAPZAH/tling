#include "offline_translator/firefox_model_manager.hpp"

#include "offline_translator/app_log.hpp"
#include "compression.hpp"
#include "file_transfer.hpp"
#include "fs_utils.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <stdexcept>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace offline_translator {
namespace {

using nlohmann::json;

constexpr const char* kGithubContents =
    "https://api.github.com/repos/mozilla/firefox-translations-models/"
    "contents/models/";
constexpr const char* kGithubRaw =
    "https://raw.githubusercontent.com/mozilla/firefox-translations-models/"
    "main/models/";
constexpr const char* kGithubLfs =
    "https://media.githubusercontent.com/media/mozilla/"
    "firefox-translations-models/main/models/";
constexpr const char* kGcsModels =
    "https://storage.googleapis.com/"
    "moz-fx-translations-data--303e-prod-translations-data/"
    "firefox-ci/models/";
constexpr const char* kRemoteSettings =
    "https://firefox.settings.services.mozilla.com/v1/buckets/main/"
    "collections/translations-models/records";
constexpr const char* kFirefoxCdn =
    "https://firefox-settings-attachments.cdn.mozilla.net/";

const std::vector<std::string>& fallback_dirs(std::string_view architecture) {
    static const std::vector<std::string> tiny{
        "azen", "been", "bgen", "bnen", "bsen", "caen", "csen", "daen",
        "deen", "elen", "enaz", "enbg", "enbn", "enca", "encs", "enda",
        "ende", "enel", "enes", "enet", "enfa", "enfi", "enfr", "engu",
        "enhe", "enhi", "enhr", "enhu", "enid", "enit", "enkn", "enlt",
        "enlv", "enml", "enms", "ennl", "enpl", "enpt", "enro", "enru",
        "ensk", "ensl", "ensq", "ensv", "enta", "ente", "entr", "enuk",
        "esen", "eten", "faen", "fien", "fren", "guen", "heen", "hien",
        "hren", "huen", "iden", "isen", "iten", "knen", "lten", "lven",
        "mlen", "msen", "mten", "nben", "nlen", "nnen", "plen", "pten",
        "roen", "ruen", "sken", "slen", "sqen", "sren", "sven", "taen",
        "teen", "tren", "uken", "vien",
    };
    static const std::vector<std::string> base{
        "aren", "deen", "enar", "encs", "enja", "enko", "enru", "enzh",
        "jaen", "koen", "zhen",
    };
    return architecture == "base" ? base : tiny;
}

std::string lower_ascii(std::string text) {
    std::transform(
        text.begin(),
        text.end(),
        text.begin(),
        [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return text;
}

// pair_from_dirname: каталог GitHub "enru" -> ("en","ru").
std::optional<std::pair<std::string, std::string>> pair_from_dirname(
    const std::string& name) {
    if (name.size() == 4 &&
        std::all_of(name.begin(), name.end(), [](unsigned char ch) {
            return std::isalpha(ch);
        })) {
        return std::make_pair(
            lower_ascii(name.substr(0, 2)),
            lower_ascii(name.substr(2, 2)));
    }
    return std::nullopt;
}

// normalize_language: "zh-Hans" -> "zh".
std::string normalize_language(std::string code) {
    code = lower_ascii(std::move(code));
    std::replace(code.begin(), code.end(), '_', '-');
    if (code.rfind("zh-", 0) == 0) {
        return "zh";
    }
    const auto dash = code.find('-');
    if (dash != std::string::npos) {
        code.resize(dash);
    }
    return code;
}

std::filesystem::path catalog_path(const std::filesystem::path& root) {
    return root / "catalog.json";
}

PackageInfo make_package(
    std::string from_code,
    std::string to_code,
    std::string dirname,
    std::string architecture) {
    PackageInfo info;
    info.from_code = std::move(from_code);
    info.to_code = std::move(to_code);
    info.dirname = std::move(dirname);
    info.architecture = std::move(architecture);
    return info;
}

std::vector<PackageInfo> fallback_packages(std::string_view architecture) {
    std::vector<PackageInfo> packages;
    for (const auto& dirname : fallback_dirs(architecture)) {
        auto parsed = pair_from_dirname(dirname);
        if (!parsed) {
            continue;
        }
        packages.push_back(make_package(
            parsed->first, parsed->second, dirname, std::string(architecture)));
    }
    return packages;
}

std::vector<PackageInfo> load_catalog(const std::filesystem::path& root) {
    std::ifstream in(catalog_path(root), std::ios::binary);
    if (!in) {
        return {};
    }
    std::string raw((std::istreambuf_iterator<char>(in)),
                    std::istreambuf_iterator<char>());
    try {
        const auto parsed = json::parse(raw);
        std::vector<PackageInfo> packages;
        if (!parsed.is_array()) {
            return {};
        }
        for (const auto& item : parsed) {
            if (!item.is_object() || !item.contains("from_code") ||
                !item.contains("to_code")) {
                continue;
            }
            auto from = item.at("from_code").get<std::string>();
            auto to = item.at("to_code").get<std::string>();
            auto dirname = item.value("dirname", from + to);
            auto architecture = item.value("architecture", std::string{"tiny"});
            if (!FirefoxModelManager::is_architecture(architecture)) {
                architecture = "tiny";
            }
            packages.push_back(make_package(
                std::move(from), std::move(to), std::move(dirname),
                std::move(architecture)));
        }
        return packages;
    } catch (const std::exception&) {
        return {};
    }
}

void save_catalog(
    const std::filesystem::path& root,
    const std::vector<PackageInfo>& packages) {
    json array = json::array();
    for (const auto& package : packages) {
        array.push_back({
            {"from_code", package.from_code},
            {"to_code", package.to_code},
            {"dirname", package.dirname},
            {"architecture", package.architecture},
        });
    }
    fs_utils::write_text_file(
        catalog_path(root),
        array.dump(2));
}

struct FirefoxCdnFile {
    std::string from;
    std::string to;
    std::string architecture;
    std::string name;
    std::string url;
};

std::string record_language(const json& record, const char* primary, const char* fallback) {
    if (record.contains(primary) && record[primary].is_string()) {
        return normalize_language(record[primary].get<std::string>());
    }
    if (record.contains(fallback) && record[fallback].is_string()) {
        return normalize_language(record[fallback].get<std::string>());
    }
    return {};
}

std::vector<FirefoxCdnFile> load_remote_settings_files() {
    static std::mutex mutex;
    static std::vector<FirefoxCdnFile> cache;
    static bool loaded = false;
    std::lock_guard lock(mutex);
    if (loaded) {
        return cache;
    }
    loaded = true;
    const auto temp = std::filesystem::temp_directory_path() /
        ("fx-remote-settings-" + std::to_string(::GetTickCount64()) + ".json");
    try {
        download_resumable(
            {kRemoteSettings},
            temp,
            {},
            "Каталог Firefox Remote Settings");
    } catch (const std::exception& error) {
        app_log_warn(std::string("Remote Settings недоступен: ") + error.what());
        return cache;
    }
    std::ifstream in(temp, std::ios::binary);
    std::string raw((std::istreambuf_iterator<char>(in)),
                    std::istreambuf_iterator<char>());
    std::error_code ignored;
    std::filesystem::remove(temp, ignored);
    try {
        const auto parsed = json::parse(raw);
        const json* items = nullptr;
        if (parsed.is_object() && parsed.contains("data") &&
            parsed["data"].is_array()) {
            items = &parsed["data"];
        } else if (parsed.is_array()) {
            items = &parsed;
        }
        if (!items) {
            return cache;
        }
        for (const auto& record : *items) {
            if (!record.is_object()) {
                continue;
            }
            const auto from = record_language(record, "fromLang", "sourceLanguage");
            const auto to = record_language(record, "toLang", "targetLanguage");
            auto architecture = record.value("architecture", std::string{"tiny"});
            if (!FirefoxModelManager::is_architecture(architecture) &&
                architecture != "base-memory") {
                continue;
            }
            std::string name = record.value("name", std::string{});
            std::string location;
            if (record.contains("attachment") && record["attachment"].is_object()) {
                location = record["attachment"].value("location", std::string{});
                if (name.empty()) {
                    name = record["attachment"].value("filename", std::string{});
                }
            }
            if (from.empty() || to.empty() || location.empty()) {
                continue;
            }
            FirefoxCdnFile file;
            file.from = from;
            file.to = to;
            file.architecture = architecture;
            file.name = name;
            file.url = std::string(kFirefoxCdn) + location;
            while (!file.url.empty() && file.url.back() == '/') {
                file.url.pop_back();
            }
            cache.push_back(std::move(file));
        }
        app_log_info(
            "Remote Settings: файлов=" + std::to_string(cache.size()));
    } catch (const std::exception& error) {
        app_log_warn(std::string("Remote Settings не разобран: ") + error.what());
    }
    return cache;
}

std::vector<std::pair<std::string, bool>> cdn_urls_for_file(
    std::string_view from_code,
    std::string_view to_code,
    std::string_view architecture,
    const std::string& remote_name) {
    const std::string stem = remote_name.size() > 3 &&
            remote_name.substr(remote_name.size() - 3) == ".gz"
        ? remote_name.substr(0, remote_name.size() - 3)
        : remote_name;
    const auto from = normalize_language(std::string(from_code));
    const auto to = normalize_language(std::string(to_code));
    std::vector<std::string> architectures{std::string(architecture)};
    if (architecture == "base") {
        architectures.emplace_back("base-memory");
    }
    std::vector<std::pair<std::string, bool>> urls;
    for (const auto& file : load_remote_settings_files()) {
        if (file.from != from || file.to != to) {
            continue;
        }
        bool architecture_ok = false;
        for (const auto& item : architectures) {
            if (file.architecture == item) {
                architecture_ok = true;
                break;
            }
        }
        if (!architecture_ok) {
            continue;
        }
        if (file.name != stem && file.name != remote_name &&
            file.name.find(stem) == std::string::npos) {
            continue;
        }
        urls.emplace_back(file.url, true);
    }
    return urls;
}

// Список файлов пары через GitHub contents API; пустой результат —
// если API недоступен.
std::vector<std::string> fetch_remote_file_names(
    std::string_view architecture,
    const std::string& dirname) {
    const std::filesystem::path temp =
        std::filesystem::temp_directory_path() /
        ("fx-listing-" + dirname + "-" +
         std::to_string(::GetTickCount64()) + ".json");
    const std::string url =
        std::string(kGithubContents) + std::string(architecture) +
        "/" + dirname;
    try {
        download_resumable(
            {url},
            temp,
            {},
            "Получаю список файлов модели");
    } catch (const std::exception&) {
        std::error_code ignored;
        std::filesystem::remove(temp, ignored);
        return {};
    }
    std::ifstream in(temp, std::ios::binary);
    std::string raw((std::istreambuf_iterator<char>(in)),
                    std::istreambuf_iterator<char>());
    std::error_code ignored;
    std::filesystem::remove(temp, ignored);

    try {
        const auto parsed = json::parse(raw);
        std::vector<std::string> names;
        if (!parsed.is_array()) {
            return {};
        }
        for (const auto& entry : parsed) {
            if (!entry.is_object() ||
                entry.value("type", std::string{}) != "file") {
                continue;
            }
            names.push_back(entry.value("name", std::string{}));
        }
        return names;
    } catch (const std::exception&) {
        return {};
    }
}

// _normalized_name: имя на GitHub -> model.bin / vocab.spm / ...
std::optional<std::string> normalized_name(const std::string& remote_name) {
    std::string name = lower_ascii(remote_name);
    if (name.size() > 3 && name.rfind(".gz") == name.size() - 3) {
        name.resize(name.size() - 3);
    }
    if (name == "metadata.json") {
        return name;
    }
    if (name.rfind("model.", 0) == 0 &&
        name.size() > 4 && name.rfind(".bin") == name.size() - 4) {
        return "model.bin";
    }
    if (name.rfind("vocab.", 0) == 0 &&
        name.size() > 5 && name.rfind(".spm") == name.size() - 4) {
        return "vocab.spm";
    }
    if (name.rfind("srcvocab.", 0) == 0 &&
        name.size() > 9 && name.rfind(".spm") == name.size() - 4) {
        return "srcvocab.spm";
    }
    if (name.rfind("trgvocab.", 0) == 0 &&
        name.size() > 9 && name.rfind(".spm") == name.size() - 4) {
        return "trgvocab.spm";
    }
    if (name.rfind("lex.", 0) == 0 &&
        name.size() > 4 && name.rfind(".bin") == name.size() - 4) {
        return "lex.bin";
    }
    return std::nullopt;
}

}  // namespace

std::vector<std::string> FirefoxModelManager::architectures() {
    return {"tiny", "base"};
}

bool FirefoxModelManager::is_architecture(std::string_view architecture) {
    return architecture == "tiny" || architecture == "base";
}

std::filesystem::path FirefoxModelManager::default_models_root() {
    // Совпадает с models_dir() в Python: env, portable data, профиль.
    const auto custom = fs_utils::getenv_string("OFFLINE_TRANSLATOR_MODELS");
    if (!custom.empty()) {
        return {custom};
    }
    wchar_t exe_path[MAX_PATH]{};
    if (GetModuleFileNameW(nullptr, exe_path, MAX_PATH) > 0) {
        const std::filesystem::path exe_dir =
            std::filesystem::path(exe_path).parent_path();
        const auto portable = exe_dir / L"data" / L"firefox-models";
        if (std::filesystem::exists(portable)) {
            return portable;
        }
    }
    return fs_utils::user_home() / ".local" / "share" / "offline-translator" /
           "firefox-models";
}

FirefoxModelManager::FirefoxModelManager(
    std::filesystem::path models_root,
    std::string architecture,
    std::string source_code,
    std::string target_code)
    : models_root_(std::move(models_root)),
      architecture_(std::move(architecture)),
      source_code_(std::move(source_code)),
      target_code_(std::move(target_code)) {
    if (!is_architecture(architecture_)) {
        throw std::invalid_argument("Неизвестный размер модели Firefox");
    }
}

bool firefox_model_ready(const std::filesystem::path& directory) {
    std::error_code ignored;
    const bool has_model =
        std::filesystem::is_regular_file(directory / "model.bin", ignored);
    const bool shared_vocab =
        std::filesystem::is_regular_file(directory / "vocab.spm", ignored);
    const bool split_vocab =
        std::filesystem::is_regular_file(directory / "srcvocab.spm", ignored) &&
        std::filesystem::is_regular_file(directory / "trgvocab.spm", ignored);
    return has_model && (shared_vocab || split_vocab);
}

std::optional<std::filesystem::path> FirefoxModelManager::ready_model_in(
    std::string_view architecture) const {
    const auto candidate =
        models_root_ / std::string(architecture) /
        (source_code_ + "-" + target_code_);
    if (firefox_model_ready(candidate)) {
        return candidate;
    }
    return std::nullopt;
}

std::optional<std::filesystem::path> FirefoxModelManager::resolve_model_path()
    const {
    if (auto preferred = ready_model_in(architecture_)) {
        return preferred;
    }
    for (const auto& other : architectures()) {
        if (other == architecture_) {
            continue;
        }
        if (auto alternate = ready_model_in(other)) {
            return alternate;
        }
    }
    if (architecture_ != "tiny") {
        return std::nullopt;
    }
    const auto legacy = models_root_ / (source_code_ + "-" + target_code_);
    if (firefox_model_ready(legacy)) {
        return legacy;
    }
    return std::nullopt;
}

bool FirefoxModelManager::is_installed() const {
    return resolve_model_path().has_value();
}

bool FirefoxModelManager::has_incomplete_package() const {
    std::error_code ignored;
    const auto installed = models_root_ / architecture_ /
                           (source_code_ + "-" + target_code_);
    if (std::filesystem::is_directory(installed, ignored) &&
        !firefox_model_ready(installed)) {
        return true;
    }
    const auto staging = downloads_dir_for_pair();
    return std::filesystem::is_directory(staging, ignored);
}

std::filesystem::path FirefoxModelManager::downloads_dir_for_pair() const {
    return models_root_ / "_downloads" /
           (architecture_ + "-" + source_code_ + "-" + target_code_);
}

std::vector<PackageInfo> FirefoxModelManager::installed_packages(
    const std::filesystem::path& models_root) {
    std::vector<PackageInfo> packages;
    auto add_pair_directory =
        [&packages](const std::filesystem::path& directory,
                    const std::string& architecture) {
            if (!firefox_model_ready(directory)) {
                return;
            }
            std::string from;
            std::string to;
            const auto metadata = directory / "metadata.json";
            std::error_code ignored;
            if (std::filesystem::is_regular_file(metadata, ignored)) {
                std::ifstream in(metadata, std::ios::binary);
                std::string raw((std::istreambuf_iterator<char>(in)),
                                std::istreambuf_iterator<char>());
                try {
                    const auto parsed = json::parse(raw);
                    from = normalize_language(
                        parsed.value("sourceLanguage", std::string{}));
                    to = normalize_language(
                        parsed.value("targetLanguage", std::string{}));
                } catch (const std::exception&) {
                }
            }
            if (from.empty() || to.empty()) {
                const std::string name = directory.filename().string();
                const auto dash = name.find('-');
                if (dash != std::string::npos && dash > 0 &&
                    dash + 1 < name.size()) {
                    from = lower_ascii(name.substr(0, dash));
                    to = lower_ascii(name.substr(dash + 1));
                }
            }
            if (!from.empty() && !to.empty()) {
                packages.push_back(make_package(
                    from, to, directory.filename().string(), architecture));
            }
        };
    for (const auto& architecture : architectures()) {
        const auto arch_root = models_root / architecture;
        std::error_code ignored;
        if (!std::filesystem::is_directory(arch_root, ignored)) {
            continue;
        }
        for (const auto& entry : std::filesystem::directory_iterator(arch_root)) {
            if (entry.is_directory()) {
                add_pair_directory(entry.path(), architecture);
            }
        }
    }
    // Старый плоский каталог tiny: <root>/<from>-<to>.
    for (const auto& entry : std::filesystem::directory_iterator(models_root)) {
        if (!entry.is_directory()) {
            continue;
        }
        const std::string name = entry.path().filename().string();
        if (!name.empty() && name[0] == '_') {
            continue;
        }
        if (is_architecture(name)) {
            continue;
        }
        add_pair_directory(entry.path(), "tiny");
    }
    return packages;
}

std::vector<PackageInfo> FirefoxModelManager::available_packages(
    const std::filesystem::path& models_root,
    std::string_view architecture) {
    const auto catalog = load_catalog(models_root);
    std::vector<PackageInfo> matched;
    for (auto& package : catalog) {
        if (package.architecture == architecture) {
            matched.push_back(std::move(package));
        }
    }
    if (!matched.empty()) {
        return matched;
    }
    return fallback_packages(architecture);
}

void FirefoxModelManager::update_remote_index(
    const std::filesystem::path& models_root) {
    std::vector<PackageInfo> combined;
    for (const auto& architecture : architectures()) {
        const std::filesystem::path temp =
            std::filesystem::temp_directory_path() /
            ("fx-catalog-" + architecture + "-" +
             std::to_string(::GetTickCount64()) + ".json");
        try {
            download_resumable(
                {std::string(kGithubContents) + architecture},
                temp,
                {},
                "Обновляю каталог Firefox");
        } catch (const std::exception&) {
            for (auto& package : fallback_packages(architecture)) {
                combined.push_back(std::move(package));
            }
            continue;
        }
        std::ifstream in(temp, std::ios::binary);
        std::string raw((std::istreambuf_iterator<char>(in)),
                        std::istreambuf_iterator<char>());
        std::error_code ignored;
        std::filesystem::remove(temp, ignored);
        bool added_any = false;
        try {
            const auto parsed = json::parse(raw);
            if (!parsed.is_array()) {
                throw std::runtime_error("unexpected github answer");
            }
            for (const auto& entry : parsed) {
                if (!entry.is_object() ||
                    entry.value("type", std::string{}) != "dir") {
                    continue;
                }
                const auto dirname = entry.value("name", std::string{});
                const auto codes = pair_from_dirname(dirname);
                if (!codes) {
                    continue;
                }
                combined.push_back(make_package(
                    codes->first, codes->second, dirname, architecture));
                added_any = true;
            }
        } catch (const std::exception&) {
        }
        if (!added_any) {
            for (auto& package : fallback_packages(architecture)) {
                bool duplicate = false;
                for (const auto& existing : combined) {
                    if (existing.from_code == package.from_code &&
                        existing.to_code == package.to_code &&
                        existing.architecture == package.architecture) {
                        duplicate = true;
                        break;
                    }
                }
                if (!duplicate) {
                    combined.push_back(std::move(package));
                }
            }
        }
    }
    if (combined.empty()) {
        throw std::runtime_error("Каталог Firefox недоступен");
    }
    save_catalog(models_root, combined);
}

namespace {

// Скачивает сжатый файл и распаковывает в destination.
// Возвращает размер распакованных данных.
std::uintmax_t download_and_decompress(
    const std::string& url,
    bool zst_encoded,
    const std::filesystem::path& destination,
    const ProgressCallback& progress) {
    auto compressed = destination;
    compressed.replace_extension(".download");
    download_resumable(
        {url},
        compressed,
        progress,
        "Скачиваю модель Firefox");
    std::ifstream in(compressed, std::ios::binary);
    if (!in) {
        throw std::runtime_error("Скачанный файл не читается");
    }
    std::vector<std::uint8_t> payload(
        (std::istreambuf_iterator<char>(in)),
        std::istreambuf_iterator<char>());
    std::error_code ignored;
    std::filesystem::remove(compressed, ignored);
    if (payload.size() < 500) {
        throw std::runtime_error("Скачан слишком маленький файл");
    }
    std::vector<std::uint8_t> data;
    bool ok = zst_encoded
        ? compression::zunstd(payload.data(), payload.size(), data)
        : compression::gunzip(payload.data(), payload.size(), data);
    if (!ok || data.size() < 500) {
        data.clear();
        ok = zst_encoded
            ? compression::gunzip(payload.data(), payload.size(), data)
            : compression::zunstd(payload.data(), payload.size(), data);
    }
    if ((!ok || data.size() < 500) && payload.size() >= 10000) {
        // Remote Settings иногда отдаёт уже распакованный файл.
        data = std::move(payload);
        ok = true;
    }
    if (!ok || data.size() < 500) {
        throw std::runtime_error(
            "Не удалось распаковать файл модели: " + url);
    }
    fs_utils::write_bytes(destination, data.data(), data.size());
    return data.size();
}

}  // namespace

void FirefoxModelManager::download_and_install(const ProgressCallback& progress) {
    if (is_installed()) {
        if (progress) {
            progress(1, 1, "Пакет уже установлен");
        }
        return;
    }
    // dirname пары: из каталога, иначе конкатенация кодов.
    std::string dirname = source_code_ + target_code_;
    for (const auto& package :
         available_packages(models_root_, architecture_)) {
        if (package.from_code == source_code_ &&
            package.to_code == target_code_) {
            dirname = package.dirname;
            break;
        }
    }

    std::vector<std::string> remote_names =
        fetch_remote_file_names(architecture_, dirname);
    if (remote_names.empty()) {
        const std::string pair = source_code_ + target_code_;
        remote_names = {
            "model." + pair + ".intgemm.alphas.bin.gz",
            "vocab." + pair + ".spm.gz",
            "srcvocab." + pair + ".spm.gz",
            "trgvocab." + pair + ".spm.gz",
            "lex.50.50." + pair + ".s2t.bin.gz",
            "metadata.json",
        };
    }
    bool has_metadata = false;
    for (const auto& name : remote_names) {
        if (name == "metadata.json") {
            has_metadata = true;
        }
    }
    if (!has_metadata) {
        remote_names.push_back("metadata.json");
    }

    const auto staging = downloads_dir_for_pair();
    fs_utils::remove_tree(staging);
    std::filesystem::create_directories(staging);

    bool planned_split_vocab = false;
    for (const auto& name : remote_names) {
        const auto normalized = normalized_name(name);
        if (normalized == "srcvocab.spm" || normalized == "trgvocab.spm") {
            planned_split_vocab = true;
        }
    }

    for (const auto& remote_name : remote_names) {
        const auto target = normalized_name(remote_name);
        if (!target) {
            continue;
        }
        if (target == "metadata.json") {
            try {
                download_resumable(
                    {std::string(kGithubRaw) + architecture_ + "/" + dirname +
                     "/metadata.json"},
                    staging / "metadata.json",
                    progress,
                    "Скачиваю metadata.json");
            } catch (const std::exception&) {
                // metadata не обязателен для перевода.
            }
            continue;
        }
        const std::string stem =
            remote_name.substr(0, remote_name.size() - 3);
        std::vector<std::pair<std::string, bool>> urls =
            cdn_urls_for_file(source_code_, target_code_, architecture_, remote_name);
        const auto add_architecture_variants =
            [&](const std::string& architecture) {
                urls.emplace_back(
                    std::string(kGcsModels) + architecture + "/" +
                        source_code_ + "-" + target_code_ + "/" + stem +
                        ".zst",
                    true);
                urls.emplace_back(
                    std::string(kGithubLfs) + architecture + "/" + dirname +
                        "/" + remote_name,
                    false);
                urls.emplace_back(
                    std::string(kGithubRaw) + architecture + "/" + dirname +
                        "/" + remote_name,
                    false);
            };
        add_architecture_variants(architecture_);
        if (architecture_ == "base") {
            add_architecture_variants("base-memory");
        }
        bool downloaded = false;
        std::string last_error;
        app_log_info(
            "Firefox файл " + remote_name + " источников=" +
            std::to_string(urls.size()));
        for (const auto& [url, zst_encoded] : urls) {
            try {
                download_and_decompress(url, zst_encoded, staging / *target, progress);
                downloaded = true;
                app_log_info("Firefox скачан " + remote_name);
                break;
            } catch (const std::exception& error) {
                last_error = error.what();
                app_log_warn(
                    "Firefox источник не подошёл " + url + ": " + last_error);
            }
        }
        if (downloaded) {
            continue;
        }
        // lex ускоряет перевод; общий vocab не нужен при раздельных словарях.
        if (target == "lex.bin" ||
            (target == "vocab.spm" && planned_split_vocab)) {
            if (progress) {
                progress(0, 1, "Пропускаю " + *target + ": " + last_error);
            }
            continue;
        }
        throw std::runtime_error(
            "Загрузка " + remote_name + " не удалась: " + last_error);
    }

    if (!firefox_model_ready(staging)) {
        throw std::runtime_error("Не удалось скачать модель или словарь");
    }
    // Дописываем архитектуру в metadata.json, как Python.
    try {
        const auto metadata_file = staging / "metadata.json";
        json metadata = json::object();
        std::error_code ignored;
        if (std::filesystem::is_regular_file(metadata_file, ignored)) {
            std::ifstream in(metadata_file, std::ios::binary);
            std::string raw((std::istreambuf_iterator<char>(in)),
                            std::istreambuf_iterator<char>());
            auto parsed = json::parse(raw);
            if (parsed.is_object()) {
                metadata = std::move(parsed);
            }
        }
        metadata["sourceLanguage"] = source_code_;
        metadata["targetLanguage"] = target_code_;
        metadata["architecture"] = architecture_;
        fs_utils::write_text_file(
            metadata_file,
            metadata.dump(2));
    } catch (const std::exception&) {
    }

    if (progress) {
        progress(1, 1, "Устанавливаю пакет...");
    }
    const auto installed_dir = models_root_ / architecture_ /
                               (source_code_ + "-" + target_code_);
    fs_utils::atomic_replace_directory(staging, installed_dir);
}

void FirefoxModelManager::uninstall() {
    std::vector<std::filesystem::path> targets{
        models_root_ / architecture_ / (source_code_ + "-" + target_code_),
        downloads_dir_for_pair(),
    };
    if (architecture_ == "tiny") {
        targets.push_back(models_root_ / (source_code_ + "-" + target_code_));
    }
    for (const auto& path : targets) {
        std::error_code ignored;
        if (std::filesystem::exists(path, ignored)) {
            fs_utils::remove_tree(path);
        }
    }
}

}  // namespace offline_translator
