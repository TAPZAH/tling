#pragma once

#include "offline_translator/package_types.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace offline_translator {

// Менеджер моделей Firefox Translations. Раскладка на диске совпадает с
// Python (language_packages.py):
//   <root>/<tiny|base>/<from>-<to>/{model.bin, vocab.spm | srcvocab.spm +
//   trgvocab.spm, lex.bin, metadata.json}; для tiny поддержан старый
//   плоский каталог <root>/<from>-<to>; незавершённые загрузки —
//   <root>/_downloads/<arch>-<from>-<to>.
class FirefoxModelManager {
public:
    FirefoxModelManager(
        std::filesystem::path models_root,
        std::string architecture,
        std::string source_code,
        std::string target_code);

    static std::vector<std::string> architectures();
    static bool is_architecture(std::string_view architecture);
    static std::filesystem::path default_models_root();

    // Каталог доступных пар: кэш catalog.json, иначе встроенные списки.
    static std::vector<PackageInfo> available_packages(
        const std::filesystem::path& models_root,
        std::string_view architecture);
    // Обновляет каталог из GitHub contents API; при ошибке сети каталог
    // не трогается (как update_remote_index в Python).
    static void update_remote_index(const std::filesystem::path& models_root);

    static std::vector<PackageInfo> installed_packages(
        const std::filesystem::path& models_root);

    bool is_installed() const;
    bool has_incomplete_package() const;

    // Путь установленной модели: сначала выбранный размер, затем другой
    // (base→tiny и наоборот), затем legacy-плоский каталог tiny.
    std::optional<std::filesystem::path> resolve_model_path() const;

    void download_and_install(const ProgressCallback& progress = {});
    void uninstall();

private:
    std::filesystem::path downloads_dir_for_pair() const;
    std::optional<std::filesystem::path> ready_model_in(
        std::string_view architecture) const;

    std::filesystem::path models_root_;
    std::string architecture_;
    std::string source_code_;
    std::string target_code_;
};

}  // namespace offline_translator
