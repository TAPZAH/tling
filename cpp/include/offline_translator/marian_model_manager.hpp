#pragma once

#include "offline_translator/package_types.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace offline_translator {

// Пакеты Helsinki-NLP OPUS-MT / MarianMT в формате CTranslate2.
// Каталог: <root>/<from>-<to>/{model.bin, source.spm, target.spm, ...}
class MarianModelManager {
public:
    static constexpr std::uintmax_t kDefaultMinimumModelSize =
        10ULL * 1024ULL * 1024ULL;

    MarianModelManager(
        std::filesystem::path models_root,
        std::string source_code,
        std::string target_code,
        std::uintmax_t minimum_model_size = kDefaultMinimumModelSize);

    static std::filesystem::path default_models_root();
    static std::vector<PackageInfo> available_packages();
    static std::vector<PackageInfo> installed_packages(
        const std::filesystem::path& models_root);
    // Качает список с Hugging Face. Ошибка сети не бросается.
    static void update_remote_index();
    static std::filesystem::path index_cache_path();
    static void set_index_cache_path(std::filesystem::path path);

    std::filesystem::path pair_path() const;
    std::filesystem::path model_path() const;
    std::filesystem::path source_tokenizer_path() const;
    std::filesystem::path target_tokenizer_path() const;
    std::filesystem::path downloads_path() const;
    std::filesystem::path staging_path() const;
    bool is_installed() const;
    bool has_incomplete_package() const;
    void validate() const;

    void set_repo_override(std::string repo);

    void download_and_install(const ProgressCallback& progress = {});
    void install_from_staging(const ProgressCallback& progress = {});
    void uninstall(const std::function<void()>& unload_models = {});

private:
    bool package_files_ready(const std::filesystem::path& directory) const;
    std::vector<std::string> candidate_repos() const;
    void download_from_repo(
        const std::string& repo,
        const ProgressCallback& progress);
    void ensure_config_json(const std::filesystem::path& directory) const;

    std::filesystem::path models_root_;
    std::string source_code_;
    std::string target_code_;
    std::uintmax_t minimum_model_size_;
    std::string repo_override_;
};

}  // пространство имён offline_translator
