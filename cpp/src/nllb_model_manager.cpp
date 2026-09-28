#include "offline_translator/nllb_model_manager.hpp"

#include "file_transfer.hpp"
#include "fs_utils.hpp"

#include <nlohmann/json.hpp>

#include <array>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace offline_translator {

namespace {

constexpr std::string_view kModelName = "nllb-200-distilled-600M";
constexpr std::string_view kHfRepo =
    "mijuanlo/nllb-200-distilled-600M-ct2-int8";
constexpr std::array<std::string_view, 3> kDownloadFiles{
    "model.bin",
    "shared_vocabulary.json",
    "sentencepiece.bpe.model",
};
constexpr std::array<std::string_view, 4> kRequiredFiles{
    "model.bin",
    "shared_vocabulary.json",
    "sentencepiece.bpe.model",
    "config.json",
};

std::vector<std::string> default_file_urls(std::string_view file_name) {
    const std::string name(file_name);
    return {
        "https://huggingface.co/" + std::string(kHfRepo) + "/resolve/main/" +
            name + "?download=true",
        "https://hf-mirror.com/" + std::string(kHfRepo) + "/resolve/main/" +
            name + "?download=true",
    };
}

}  // анонимное пространство имён

NllbModelManager::NllbModelManager(
    std::filesystem::path models_root,
    std::uintmax_t minimum_model_size)
    : models_root_(std::move(models_root)),
      minimum_model_size_(minimum_model_size) {
    if (models_root_.empty()) {
        throw std::invalid_argument("Не задан корень моделей NLLB");
    }
}

std::filesystem::path NllbModelManager::default_models_root() {
    const auto env = fs_utils::getenv_string("OFFLINE_TRANSLATOR_NLLB");
    if (!env.empty()) {
        return std::filesystem::path(env);
    }
    return fs_utils::user_home() / ".local" / "share" / "offline-translator" /
           "nllb-200";
}

std::filesystem::path NllbModelManager::models_root() const {
    return models_root_;
}

std::filesystem::path NllbModelManager::model_path() const {
    return models_root_ / kModelName;
}

std::filesystem::path NllbModelManager::downloads_path() const {
    return models_root_ / "_downloads";
}

std::filesystem::path NllbModelManager::staging_path() const {
    return downloads_path() / kModelName;
}

bool NllbModelManager::is_installed() const {
    try {
        const auto root = model_path();
        const auto model_bin = root / "model.bin";
        if (!std::filesystem::is_regular_file(model_bin) ||
            std::filesystem::file_size(model_bin) < minimum_model_size_) {
            return false;
        }
        for (const auto file_name : kRequiredFiles) {
            if (!std::filesystem::is_regular_file(root / file_name)) {
                return false;
            }
        }
        return true;
    } catch (const std::filesystem::filesystem_error&) {
        return false;
    }
}

bool NllbModelManager::has_incomplete_package() const {
    if (is_installed()) {
        return false;
    }
    try {
        if (std::filesystem::exists(model_path())) {
            return true;
        }
        return fs_utils::directory_has_entries(downloads_path());
    } catch (const std::filesystem::filesystem_error&) {
        return false;
    }
}

void NllbModelManager::validate() const {
    if (!is_installed()) {
        throw std::runtime_error(
            "Модель NLLB не установлена или повреждена: " +
            model_path().string());
    }
}

std::vector<PackageInfo> NllbModelManager::available_packages() const {
    return {PackageInfo{
        "nllb",
        "all",
        "NLLB-200 Distilled 600M",
        "200 языков",
        std::string(kModelName),
        "nllb",
        "https://huggingface.co/" + std::string(kHfRepo),
    }};
}

void NllbModelManager::update_remote_index() const {
    // Каталог NLLB фиксированный, индекс не качаем.
}

void NllbModelManager::set_source_url(std::string file_name, std::string url) {
    source_urls_[std::move(file_name)] = std::move(url);
}

bool NllbModelManager::staging_is_valid() const {
    try {
        const auto root = staging_path();
        const auto model_bin = root / "model.bin";
        if (!std::filesystem::is_regular_file(model_bin) ||
            std::filesystem::file_size(model_bin) < minimum_model_size_) {
            return false;
        }
        for (const auto file_name : kDownloadFiles) {
            if (!std::filesystem::is_regular_file(root / file_name)) {
                return false;
            }
        }
        return std::filesystem::is_regular_file(root / "config.json");
    } catch (const std::filesystem::filesystem_error&) {
        return false;
    }
}

void NllbModelManager::write_config_json(
    const std::filesystem::path& directory) const {
    nlohmann::json config = {
        {"add_source_bos", false},
        {"add_source_eos", false},
        {"bos_token", "<s>"},
        {"decoder_start_token", "</s>"},
        {"eos_token", "</s>"},
        {"layer_norm_epsilon", nullptr},
        {"unk_token", "<unk>"},
    };
    fs_utils::write_text_file(
        directory / "config.json", config.dump(2));
}

void NllbModelManager::download_and_install(const ProgressCallback& progress) {
    if (is_installed()) {
        if (progress) {
            progress(1, 1, "Модель NLLB уже установлена");
        }
        return;
    }

    std::filesystem::create_directories(staging_path());
    for (const auto file_name : kDownloadFiles) {
        std::vector<std::string> urls;
        const auto override = source_urls_.find(std::string(file_name));
        if (override != source_urls_.end()) {
            urls.push_back(override->second);
        } else {
            urls = default_file_urls(file_name);
        }
        const std::optional<std::uintmax_t> skip_size =
            file_name == "model.bin"
                ? std::optional<std::uintmax_t>(minimum_model_size_)
                : std::optional<std::uintmax_t>{};
        download_resumable(
            urls,
            staging_path() / file_name,
            progress,
            std::string("Скачиваю ") + std::string(file_name),
            skip_size);
    }
    write_config_json(staging_path());
    install_from_staging(progress);
}

void NllbModelManager::install_from_staging(const ProgressCallback& progress) {
    if (is_installed()) {
        if (progress) {
            progress(1, 1, "Модель NLLB уже установлена");
        }
        return;
    }
    write_config_json(staging_path());
    if (!staging_is_valid()) {
        throw std::runtime_error(
            "Промежуточные файлы NLLB неполные или повреждены: " +
            staging_path().string());
    }
    if (progress) {
        progress(1, 1, "Устанавливаю модель NLLB...");
    }
    fs_utils::atomic_replace_directory(staging_path(), model_path());
    if (!fs_utils::directory_has_entries(downloads_path())) {
        fs_utils::remove_tree(downloads_path());
    }
    if (!is_installed()) {
        throw std::runtime_error("Модель NLLB скачана, но файлы не найдены");
    }
}

void NllbModelManager::uninstall(const std::function<void()>& unload_models) {
    if (unload_models) {
        unload_models();
    }
    fs_utils::remove_tree(model_path());
    fs_utils::remove_tree(downloads_path());
    if (is_installed()) {
        throw std::runtime_error("Модель NLLB не удалось удалить");
    }
}

}  // пространство имён offline_translator
