#include "offline_translator/marian_model_manager.hpp"

#include "file_transfer.hpp"
#include "fs_utils.hpp"
#include "offline_translator/app_log.hpp"
#include "offline_translator/app_settings.hpp"
#include "offline_translator/language_store.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <regex>
#include <stdexcept>
#include <utility>

namespace offline_translator {
namespace {

constexpr std::string_view kBuiltinPairs[][2] = {
    {"en", "ru"}, {"ru", "en"}, {"en", "de"}, {"de", "en"},
    {"en", "fr"}, {"fr", "en"}, {"en", "es"}, {"es", "en"},
    {"en", "it"}, {"it", "en"}, {"en", "pt"}, {"pt", "en"},
    {"en", "zh"}, {"zh", "en"}, {"en", "uk"}, {"uk", "en"},
    {"en", "pl"}, {"pl", "en"}, {"en", "tr"}, {"tr", "en"},
    {"en", "ja"}, {"ja", "en"}, {"en", "ko"}, {"ko", "en"},
    {"en", "ar"}, {"ar", "en"}, {"en", "nl"}, {"nl", "en"},
    {"en", "cs"}, {"cs", "en"}, {"en", "sv"}, {"sv", "en"},
    {"en", "fi"}, {"fi", "en"}, {"en", "el"}, {"el", "en"},
    {"en", "he"}, {"he", "en"}, {"en", "hu"}, {"hu", "en"},
    {"en", "ro"}, {"ro", "en"}, {"en", "bg"}, {"bg", "en"},
    {"en", "da"}, {"da", "en"}, {"en", "id"}, {"id", "en"},
    {"en", "vi"}, {"vi", "en"}, {"en", "th"}, {"th", "en"},
    {"en", "hi"}, {"hi", "en"}, {"uk", "ru"}, {"ru", "uk"},
    {"de", "ru"}, {"ru", "de"}, {"fr", "ru"}, {"ru", "fr"},
    {"pl", "ru"}, {"ru", "pl"},
};

std::filesystem::path& cache_path_override() {
    static std::filesystem::path path;
    return path;
}

bool is_iso_code(std::string_view code) {
    if (code.size() < 2 || code.size() > 3) {
        return false;
    }
    return std::all_of(code.begin(), code.end(), [](unsigned char ch) {
        return std::islower(ch);
    });
}

PackageInfo make_package_info(
    std::string_view from_code,
    std::string_view to_code,
    std::string repo = {}) {
    PackageInfo info;
    info.from_code = std::string(from_code);
    info.to_code = std::string(to_code);
    info.from_name = language_store_name(from_code, from_code);
    info.to_name = language_store_name(to_code, to_code);
    info.dirname = info.from_code + "-" + info.to_code;
    info.architecture = "marian";
    info.download_url = std::move(repo);
    return info;
}

std::vector<std::string> hf_file_urls(
    const std::string& repo,
    const std::string& file_name) {
    return {
        "https://huggingface.co/" + repo + "/resolve/main/" + file_name +
            "?download=true",
        "https://hf-mirror.com/" + repo + "/resolve/main/" + file_name +
            "?download=true",
    };
}

std::vector<PackageInfo> read_catalog_cache(const std::filesystem::path& path) {
    std::vector<PackageInfo> result;
    try {
        if (!std::filesystem::is_regular_file(path)) {
            return result;
        }
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            return result;
        }
        const auto parsed = nlohmann::json::parse(in, nullptr, false);
        if (parsed.is_discarded() || !parsed.is_array()) {
            return result;
        }
        for (const auto& item : parsed) {
            if (!item.is_object()) {
                continue;
            }
            const auto from = item.value("from", std::string{});
            const auto to = item.value("to", std::string{});
            if (!is_iso_code(from) || !is_iso_code(to) || from == to) {
                continue;
            }
            result.push_back(
                make_package_info(from, to, item.value("repo", std::string{})));
        }
    } catch (...) {
        return {};
    }
    return result;
}

void merge_package(
    std::vector<PackageInfo>& packages,
    PackageInfo incoming) {
    for (auto& item : packages) {
        if (item.from_code == incoming.from_code &&
            item.to_code == incoming.to_code) {
            if (item.download_url.empty()) {
                item.download_url = std::move(incoming.download_url);
            }
            return;
        }
    }
    packages.push_back(std::move(incoming));
}

}  // анонимное пространство имён

MarianModelManager::MarianModelManager(
    std::filesystem::path models_root,
    std::string source_code,
    std::string target_code,
    std::uintmax_t minimum_model_size)
    : models_root_(std::move(models_root)),
      source_code_(std::move(source_code)),
      target_code_(std::move(target_code)),
      minimum_model_size_(minimum_model_size) {
    if (models_root_.empty()) {
        throw std::invalid_argument("Не задан корень моделей MarianMT");
    }
    if (source_code_.empty() || target_code_.empty()) {
        throw std::invalid_argument("Не заданы коды языков MarianMT");
    }
}

std::filesystem::path MarianModelManager::default_models_root() {
    const auto env = fs_utils::getenv_string("OFFLINE_TRANSLATOR_MARIAN");
    if (!env.empty()) {
        return std::filesystem::path(env);
    }
    return default_data_root() / "marian-models";
}

std::filesystem::path MarianModelManager::index_cache_path() {
    if (!cache_path_override().empty()) {
        return cache_path_override();
    }
    return default_data_root() / "marian-catalog.json";
}

void MarianModelManager::set_index_cache_path(std::filesystem::path path) {
    cache_path_override() = std::move(path);
}

std::vector<PackageInfo> MarianModelManager::available_packages() {
    std::vector<PackageInfo> packages;
    for (const auto& pair : kBuiltinPairs) {
        merge_package(packages, make_package_info(pair[0], pair[1]));
    }
    for (auto& item : read_catalog_cache(index_cache_path())) {
        merge_package(packages, std::move(item));
    }
    return packages;
}

std::vector<PackageInfo> MarianModelManager::installed_packages(
    const std::filesystem::path& models_root) {
    std::vector<PackageInfo> packages;
    try {
        if (!std::filesystem::is_directory(models_root)) {
            return packages;
        }
        for (const auto& entry :
             std::filesystem::directory_iterator(models_root)) {
            if (!entry.is_directory()) {
                continue;
            }
            const auto name = entry.path().filename().string();
            const auto dash = name.find('-');
            if (dash == std::string::npos || dash == 0 ||
                dash + 1 >= name.size()) {
                continue;
            }
            const auto from = name.substr(0, dash);
            const auto to = name.substr(dash + 1);
            if (!is_iso_code(from) || !is_iso_code(to)) {
                continue;
            }
            MarianModelManager manager(models_root, from, to);
            if (manager.is_installed() || manager.has_incomplete_package()) {
                packages.push_back(make_package_info(from, to));
            }
        }
    } catch (const std::filesystem::filesystem_error&) {
        return packages;
    }
    return packages;
}

void MarianModelManager::update_remote_index() {
    try {
        const auto cache = index_cache_path();
        std::filesystem::create_directories(cache.parent_path());
        const auto raw = cache.parent_path() / "marian-catalog.hf.json";
        std::filesystem::remove(raw);
        download_resumable(
            {
                "https://huggingface.co/api/models?search=opus-mt-ctranslate2&limit=300",
                "https://hf-mirror.com/api/models?search=opus-mt-ctranslate2&limit=300",
            },
            raw,
            {},
            "Каталог MarianMT");
        nlohmann::json written = nlohmann::json::array();
        const std::regex pattern(
            R"(opus-mt-([a-z]{2,3})-([a-z]{2,3})(?:-ctranslate2.*)?$)");
        try {
            std::ifstream in(raw, std::ios::binary);
            const auto parsed = nlohmann::json::parse(in, nullptr, false);
            if (parsed.is_array()) {
                for (const auto& item : parsed) {
                    if (!item.is_object() || !item.contains("id")) {
                        continue;
                    }
                    const auto id = item.value("id", std::string{});
                    const auto slash = id.rfind('/');
                    const auto name =
                        slash == std::string::npos ? id : id.substr(slash + 1);
                    std::smatch match;
                    if (!std::regex_match(name, match, pattern)) {
                        continue;
                    }
                    written.push_back({
                        {"from", match[1].str()},
                        {"to", match[2].str()},
                        {"repo", id},
                    });
                }
            }
        } catch (...) {
        }
        if (!written.empty()) {
            fs_utils::write_text_file(cache, written.dump(2) + "\n");
        }
        try {
            std::filesystem::remove(raw);
        } catch (...) {
        }
        app_log_info(
            "каталог MarianMT обновлён, пар=" + std::to_string(written.size()));
    } catch (const std::exception& error) {
        app_log_info(std::string("каталог MarianMT не обновлён: ") + error.what());
    }
}

std::filesystem::path MarianModelManager::pair_path() const {
    return models_root_ / (source_code_ + "-" + target_code_);
}

std::filesystem::path MarianModelManager::model_path() const {
    return pair_path();
}

std::filesystem::path MarianModelManager::source_tokenizer_path() const {
    const auto dedicated = pair_path() / "source.spm";
    if (std::filesystem::is_regular_file(dedicated)) {
        return dedicated;
    }
    return pair_path() / "sentencepiece.model";
}

std::filesystem::path MarianModelManager::target_tokenizer_path() const {
    const auto dedicated = pair_path() / "target.spm";
    if (std::filesystem::is_regular_file(dedicated)) {
        return dedicated;
    }
    return source_tokenizer_path();
}

std::filesystem::path MarianModelManager::downloads_path() const {
    return models_root_ / "_downloads";
}

std::filesystem::path MarianModelManager::staging_path() const {
    return downloads_path() / (source_code_ + "-" + target_code_);
}

bool MarianModelManager::package_files_ready(
    const std::filesystem::path& directory) const {
    try {
        const auto model_bin = directory / "model.bin";
        if (!std::filesystem::is_regular_file(model_bin) ||
            std::filesystem::file_size(model_bin) < minimum_model_size_) {
            return false;
        }
        const bool has_shared =
            std::filesystem::is_regular_file(directory / "shared_vocabulary.json");
        const bool has_split =
            std::filesystem::is_regular_file(
                directory / "source_vocabulary.json") &&
            std::filesystem::is_regular_file(
                directory / "target_vocabulary.json");
        if (!has_shared && !has_split) {
            return false;
        }
        const bool has_spm =
            std::filesystem::is_regular_file(directory / "source.spm") ||
            std::filesystem::is_regular_file(directory / "sentencepiece.model");
        return has_spm &&
               std::filesystem::is_regular_file(directory / "config.json");
    } catch (const std::filesystem::filesystem_error&) {
        return false;
    }
}

bool MarianModelManager::is_installed() const {
    return package_files_ready(pair_path());
}

bool MarianModelManager::has_incomplete_package() const {
    if (is_installed()) {
        return false;
    }
    try {
        if (std::filesystem::exists(pair_path())) {
            return true;
        }
        return fs_utils::directory_has_entries(staging_path());
    } catch (const std::filesystem::filesystem_error&) {
        return false;
    }
}

void MarianModelManager::validate() const {
    if (!is_installed()) {
        throw std::runtime_error(
            "Модель MarianMT не установлена: " + source_code_ + " → " +
            target_code_ + " (" + pair_path().string() + ")");
    }
}

void MarianModelManager::set_repo_override(std::string repo) {
    repo_override_ = std::move(repo);
}

std::vector<std::string> MarianModelManager::candidate_repos() const {
    std::vector<std::string> repos;
    if (!repo_override_.empty()) {
        repos.push_back(repo_override_);
    }
    for (const auto& item : available_packages()) {
        if (item.from_code == source_code_ && item.to_code == target_code_ &&
            !item.download_url.empty()) {
            repos.push_back(item.download_url);
            break;
        }
    }
    repos.push_back(
        "Sams200/opus-mt-" + source_code_ + "-" + target_code_);
    repos.push_back(
        "gaudi/opus-mt-" + source_code_ + "-" + target_code_ + "-ctranslate2");
    std::vector<std::string> unique;
    for (const auto& repo : repos) {
        if (std::find(unique.begin(), unique.end(), repo) == unique.end()) {
            unique.push_back(repo);
        }
    }
    return unique;
}

void MarianModelManager::ensure_config_json(
    const std::filesystem::path& directory) const {
    const auto config_path = directory / "config.json";
    if (std::filesystem::is_regular_file(config_path)) {
        return;
    }
    const nlohmann::json config = {
        {"add_source_bos", false},
        {"add_source_eos", false},
        {"bos_token", "<s>"},
        {"decoder_start_token", "</s>"},
        {"eos_token", "</s>"},
        {"unk_token", "<unk>"},
    };
    fs_utils::write_text_file(config_path, config.dump(2));
}

void MarianModelManager::download_from_repo(
    const std::string& repo,
    const ProgressCallback& progress) {
    std::filesystem::create_directories(staging_path());
    download_resumable(
        hf_file_urls(repo, "model.bin"),
        staging_path() / "model.bin",
        progress,
        "Скачиваю model.bin",
        minimum_model_size_);
    const std::array<std::string_view, 5> optional_files{
        "shared_vocabulary.json",
        "source_vocabulary.json",
        "target_vocabulary.json",
        "source.spm",
        "target.spm",
    };
    for (const auto file_name : optional_files) {
        try {
            download_resumable(
                hf_file_urls(repo, std::string(file_name)),
                staging_path() / file_name,
                progress,
                std::string("Скачиваю ") + std::string(file_name));
        } catch (const std::exception&) {
            if (file_name == "source.spm") {
                download_resumable(
                    hf_file_urls(repo, "sentencepiece.model"),
                    staging_path() / "sentencepiece.model",
                    progress,
                    "Скачиваю sentencepiece.model");
            }
        }
    }
    try {
        download_resumable(
            hf_file_urls(repo, "config.json"),
            staging_path() / "config.json",
            progress,
            "Скачиваю config.json");
    } catch (const std::exception&) {
    }
    ensure_config_json(staging_path());
}

void MarianModelManager::download_and_install(const ProgressCallback& progress) {
    if (is_installed()) {
        if (progress) {
            progress(1, 1, "Модель MarianMT уже установлена");
        }
        return;
    }
    std::string last_error;
    for (const auto& repo : candidate_repos()) {
        try {
            app_log_info(
                "MarianMT " + source_code_ + "→" + target_code_ +
                " репозиторий " + repo);
            download_from_repo(repo, progress);
            install_from_staging(progress);
            return;
        } catch (const std::exception& error) {
            last_error = error.what();
            app_log_info(
                std::string("MarianMT репозиторий не подошёл: ") + last_error);
            try {
                fs_utils::remove_tree(staging_path());
            } catch (...) {
            }
        }
    }
    throw std::runtime_error(
        "Не удалось скачать MarianMT " + source_code_ + " → " + target_code_ +
        (last_error.empty() ? "" : (": " + last_error)));
}

void MarianModelManager::install_from_staging(const ProgressCallback& progress) {
    if (is_installed()) {
        if (progress) {
            progress(1, 1, "Модель MarianMT уже установлена");
        }
        return;
    }
    ensure_config_json(staging_path());
    if (!package_files_ready(staging_path())) {
        throw std::runtime_error(
            "Промежуточные файлы MarianMT неполные: " +
            staging_path().string());
    }
    if (progress) {
        progress(1, 1, "Устанавливаю модель MarianMT...");
    }
    fs_utils::atomic_replace_directory(staging_path(), pair_path());
    if (!fs_utils::directory_has_entries(downloads_path())) {
        fs_utils::remove_tree(downloads_path());
    }
    if (!is_installed()) {
        throw std::runtime_error("Модель MarianMT скачана, но файлы не найдены");
    }
}

void MarianModelManager::uninstall(const std::function<void()>& unload_models) {
    if (unload_models) {
        unload_models();
    }
    fs_utils::remove_tree(pair_path());
    fs_utils::remove_tree(staging_path());
    if (!fs_utils::directory_has_entries(downloads_path())) {
        fs_utils::remove_tree(downloads_path());
    }
    if (is_installed()) {
        throw std::runtime_error("Модель MarianMT не удалось удалить");
    }
}

}  // пространство имён offline_translator
