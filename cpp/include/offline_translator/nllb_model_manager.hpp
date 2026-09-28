#pragma once

#include "offline_translator/package_types.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace offline_translator {

class NllbModelManager {
public:
    static constexpr std::uintmax_t kDefaultMinimumModelSize =
        500ULL * 1024ULL * 1024ULL;

    explicit NllbModelManager(
        std::filesystem::path models_root,
        std::uintmax_t minimum_model_size = kDefaultMinimumModelSize);

    static std::filesystem::path default_models_root();

    std::filesystem::path models_root() const;
    std::filesystem::path model_path() const;
    std::filesystem::path downloads_path() const;
    std::filesystem::path staging_path() const;
    bool is_installed() const;
    bool has_incomplete_package() const;
    void validate() const;

    std::vector<PackageInfo> available_packages() const;
    // Каталог NLLB фиксированный, как в Python: удалённый индекс не качаем.
    void update_remote_index() const;

    void set_source_url(std::string file_name, std::string url);

    void download_and_install(const ProgressCallback& progress = {});
    void install_from_staging(const ProgressCallback& progress = {});
    // unload_models выгружает движок, чтобы файлы модели не были заняты.
    void uninstall(const std::function<void()>& unload_models = {});

private:
    bool staging_is_valid() const;
    void write_config_json(const std::filesystem::path& directory) const;

    std::filesystem::path models_root_;
    std::uintmax_t minimum_model_size_;
    std::map<std::string, std::string> source_urls_;
};

}  // пространство имён offline_translator
