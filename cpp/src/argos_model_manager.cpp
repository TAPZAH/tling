#include "offline_translator/argos_model_manager.hpp"

#include "file_transfer.hpp"
#include "fs_utils.hpp"
#include "zip_archive.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <vector>
#include <fstream>
#include <iterator>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <utility>

namespace offline_translator {

namespace {

constexpr std::string_view kDefaultPackageVersion = "1_9";
constexpr std::string_view kDefaultIndexUrl =
    "https://raw.githubusercontent.com/argosopentech/argospm-index/main/"
    "index.json";
// Обязательные файлы пакета Argos. Новый формат (начиная с 1.5) не
// содержит model/config.json, а словарь кладёт в shared_vocabulary.txt,
// поэтому состав проверяется отдельно в package_files_ready().
constexpr std::array<std::string_view, 2> kRequiredFiles{
    "model/model.bin",
    "sentencepiece.model",
};
constexpr std::array<std::string_view, 2> kSharedVocabularies{
    "model/shared_vocabulary.json",
    "model/shared_vocabulary.txt",
};

const std::array<PackageInfo, 2> kEmbeddedCatalog{{
    {"en",
     "ru",
     "English",
     "Russian",
     "translate-en_ru-1_9",
     "argos",
     "https://data.argosopentech.com/argospm/v1/translate-en_ru-1_9.argosmodel",
     {"https://data.argosopentech.com/argospm/v1/translate-en_ru-1_9.argosmodel"}},
    {"ru",
     "en",
     "Russian",
     "English",
     "translate-ru_en-1_9",
     "argos",
     "https://data.argosopentech.com/argospm/v1/translate-ru_en-1_9.argosmodel",
     {"https://data.argosopentech.com/argospm/v1/translate-ru_en-1_9.argosmodel"}},
}};

std::mutex& catalog_mutex() {
    static std::mutex mutex;
    return mutex;
}

std::filesystem::path& cache_path_override() {
    static std::filesystem::path path;
    return path;
}

std::string& url_override() {
    static std::string url;
    return url;
}

bool is_http_url(std::string_view url) {
    return url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0;
}

std::string default_remote_index_url() {
    auto env = fs_utils::getenv_string("ARGOS_PACKAGE_INDEX");
    if (env.empty()) {
        return std::string(kDefaultIndexUrl);
    }
    constexpr std::string_view suffix = "index.json";
    if (env.size() >= suffix.size() &&
        env.compare(env.size() - suffix.size(), suffix.size(), suffix) == 0) {
        return env;
    }
    if (env.back() != '/') {
        env.push_back('/');
    }
    env.append(suffix);
    return env;
}

std::filesystem::path default_index_cache_path() {
    // Как Python settings.local_package_index: рядом с packages/, не внутри.
    return ArgosModelManager::default_packages_root().parent_path() /
           "index.json";
}

std::string version_dirname_suffix(std::string version) {
    if (version.empty()) {
        return std::string(kDefaultPackageVersion);
    }
    std::replace(version.begin(), version.end(), '.', '_');
    return version;
}

std::vector<std::string> http_links(const nlohmann::json& item) {
    std::vector<std::string> urls;
    if (!item.contains("links") || !item["links"].is_array()) {
        return urls;
    }
    for (const auto& link : item["links"]) {
        if (!link.is_string()) {
            continue;
        }
        auto url = link.get<std::string>();
        if (is_http_url(url)) {
            urls.push_back(std::move(url));
        }
    }
    return urls;
}

std::string first_http_link(const nlohmann::json& item) {
    const auto urls = http_links(item);
    return urls.empty() ? std::string{} : urls.front();
}

// argos-net.com из индекса часто недоступен; зеркало совпадает с Python.
std::string data_argos_mirror(const std::string& dirname) {
    if (dirname.empty()) {
        return {};
    }
    return "https://data.argosopentech.com/argospm/v1/" + dirname +
           ".argosmodel";
}

void append_unique_url(std::vector<std::string>& urls, std::string url) {
    if (url.empty()) {
        return;
    }
    if (std::find(urls.begin(), urls.end(), url) == urls.end()) {
        urls.push_back(std::move(url));
    }
}

PackageInfo package_from_index_item(const nlohmann::json& item) {
    const auto from_code = item.value("from_code", std::string());
    const auto to_code = item.value("to_code", std::string());
    auto code = item.value("code", std::string());
    if (code.empty()) {
        code = "translate-" + from_code + "_" + to_code;
    }
    const auto dirname =
        code + "-" +
        version_dirname_suffix(item.value("package_version", std::string()));
    auto urls = http_links(item);
    append_unique_url(urls, data_argos_mirror(dirname));
    const std::string first = urls.empty() ? std::string{} : urls.front();
    return PackageInfo{
        from_code,
        to_code,
        item.value("from_name", from_code),
        item.value("to_name", to_code),
        dirname,
        "argos",
        first,
        std::move(urls),
    };
}

std::vector<PackageInfo> parse_argos_index(const std::string& text) {
    const auto parsed = nlohmann::json::parse(text, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_array()) {
        return {};
    }
    std::vector<PackageInfo> result;
    for (const auto& item : parsed) {
        if (!item.is_object()) {
            continue;
        }
        const auto type = item.value("type", std::string("translate"));
        if (type != "translate") {
            continue;
        }
        const auto from_code = item.value("from_code", std::string());
        const auto to_code = item.value("to_code", std::string());
        if (from_code.empty() || to_code.empty()) {
            continue;
        }
        if (first_http_link(item).empty()) {
            continue;
        }
        result.push_back(package_from_index_item(item));
    }
    return result;
}

std::string read_text_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }
    return std::string(
        std::istreambuf_iterator<char>(in),
        std::istreambuf_iterator<char>());
}

std::vector<PackageInfo> load_catalog_from_cache(
    const std::filesystem::path& cache_path) {
    try {
        if (!fs_utils::is_regular_file(cache_path)) {
            return {};
        }
        return parse_argos_index(read_text_file(cache_path));
    } catch (const std::exception&) {
        return {};
    }
}

std::vector<PackageInfo> embedded_catalog() {
    return {kEmbeddedCatalog.begin(), kEmbeddedCatalog.end()};
}

bool parse_package_name(
    std::string_view name,
    std::string& from_code,
    std::string& to_code) {
    constexpr std::string_view prefix = "translate-";
    if (name.rfind(prefix, 0) != 0) {
        return false;
    }
    const auto rest = name.substr(prefix.size());
    const auto dash = rest.rfind('-');
    if (dash == std::string_view::npos || dash == 0) {
        return false;
    }
    const auto pair = rest.substr(0, dash);
    const auto underscore = pair.find('_');
    if (underscore == std::string_view::npos || underscore == 0 ||
        underscore + 1 >= pair.size()) {
        return false;
    }
    from_code = std::string(pair.substr(0, underscore));
    to_code = std::string(pair.substr(underscore + 1));
    return true;
}

}  // анонимное пространство имён

ArgosModelManager::ArgosModelManager(
    std::filesystem::path packages_root,
    std::string source_code,
    std::string target_code)
    : packages_root_(std::move(packages_root)),
      source_code_(std::move(source_code)),
      target_code_(std::move(target_code)) {
    if (packages_root_.empty() || source_code_.empty() || target_code_.empty()) {
        throw std::invalid_argument("Неполные параметры пакета Argos");
    }
}

std::filesystem::path ArgosModelManager::default_packages_root() {
    return fs_utils::user_home() / ".local" / "share" / "argos-translate" /
           "packages";
}

void ArgosModelManager::set_index_cache_path(std::filesystem::path path) {
    std::lock_guard lock(catalog_mutex());
    cache_path_override() = std::move(path);
}

void ArgosModelManager::set_index_url(std::string url) {
    std::lock_guard lock(catalog_mutex());
    url_override() = std::move(url);
}

std::filesystem::path ArgosModelManager::index_cache_path() {
    std::lock_guard lock(catalog_mutex());
    if (!cache_path_override().empty()) {
        return cache_path_override();
    }
    return default_index_cache_path();
}

std::string ArgosModelManager::remote_index_url() {
    std::lock_guard lock(catalog_mutex());
    if (!url_override().empty()) {
        return url_override();
    }
    return default_remote_index_url();
}

std::vector<PackageInfo> ArgosModelManager::available_packages() {
    std::vector<PackageInfo> catalog;
    {
        std::lock_guard lock(catalog_mutex());
        const auto cache = cache_path_override().empty()
            ? default_index_cache_path()
            : cache_path_override();
        catalog = load_catalog_from_cache(cache);
    }
    if (catalog.empty()) {
        return embedded_catalog();
    }
    return catalog;
}

void ArgosModelManager::update_remote_index() {
    std::filesystem::path dest;
    std::string url;
    {
        std::lock_guard lock(catalog_mutex());
        dest = cache_path_override().empty() ? default_index_cache_path()
                                             : cache_path_override();
        url = url_override().empty() ? default_remote_index_url()
                                     : url_override();
    }
    if (dest.empty() || url.empty()) {
        return;
    }
    const auto staging = std::filesystem::path(
        dest.native() + std::filesystem::path(".new").native());
    try {
        fs_utils::remove_tree(staging);
        download_resumable({url}, staging, {}, "Обновляю индекс Argos");
        const auto parsed = parse_argos_index(read_text_file(staging));
        if (parsed.empty()) {
            fs_utils::remove_tree(staging);
            return;
        }
        std::lock_guard lock(catalog_mutex());
        std::filesystem::create_directories(dest.parent_path());
        if (std::filesystem::exists(dest)) {
            std::filesystem::remove(dest);
        }
        std::filesystem::rename(staging, dest);
    } catch (const std::exception&) {
        try {
            fs_utils::remove_tree(staging);
        } catch (const std::exception&) {
        }
    }
}

std::string ArgosModelManager::package_prefix() const {
    return "translate-" + source_code_ + "_" + target_code_ + "-";
}

std::filesystem::path ArgosModelManager::find_package_dir(
    const std::filesystem::path& root) const {
    const std::string prefix = package_prefix();
    std::filesystem::path latest_package;
    try {
        if (std::filesystem::is_directory(root)) {
            for (const auto& entry : std::filesystem::directory_iterator(root)) {
                if (!entry.is_directory()) {
                    continue;
                }
                const std::string name = entry.path().filename().string();
                if (name == "_downloads") {
                    continue;
                }
                if (name.rfind(prefix, 0) == 0 && name.size() > prefix.size() &&
                    (latest_package.empty() ||
                     name > latest_package.filename().string())) {
                    latest_package = entry.path();
                }
            }
        }
    } catch (const std::filesystem::filesystem_error&) {
        return root / (prefix + std::string(kDefaultPackageVersion));
    }
    if (!latest_package.empty()) {
        return latest_package;
    }
    return root / (prefix + std::string(kDefaultPackageVersion));
}

std::filesystem::path ArgosModelManager::package_path() const {
    return find_package_dir(packages_root_);
}

std::filesystem::path ArgosModelManager::model_path() const {
    return package_path() / "model";
}

std::filesystem::path ArgosModelManager::tokenizer_path() const {
    return package_path() / "sentencepiece.model";
}

std::filesystem::path ArgosModelManager::downloads_path() const {
    return packages_root_ / "_downloads";
}

std::filesystem::path ArgosModelManager::staging_package_dir() const {
    return find_package_dir(downloads_path());
}

bool ArgosModelManager::package_files_ready(
    const std::filesystem::path& package) const {
    try {
        for (const auto file_name : kRequiredFiles) {
            if (!std::filesystem::is_regular_file(package / file_name)) {
                return false;
            }
        }
        // Подходит любой из форматов словаря: .json (старый) или .txt (новый).
        for (const auto file_name : kSharedVocabularies) {
            if (std::filesystem::is_regular_file(package / file_name)) {
                return true;
            }
        }
        return false;
    } catch (const std::filesystem::filesystem_error&) {
        return false;
    }
}

bool ArgosModelManager::is_installed() const {
    return package_files_ready(package_path());
}

bool ArgosModelManager::has_incomplete_package() const {
    if (is_installed()) {
        return false;
    }
    try {
        const auto downloads = downloads_path();
        if (!std::filesystem::is_directory(downloads)) {
            return false;
        }
        const auto staging = staging_package_dir();
        if (std::filesystem::exists(staging)) {
            return true;
        }
        const auto catalog = catalog_entry();
        const std::string zip_stem =
            catalog ? catalog->dirname
                    : (package_prefix() + std::string(kDefaultPackageVersion));
        const auto zip_path = downloads / (zip_stem + ".argosmodel");
        const auto part_path = std::filesystem::path(
            zip_path.native() + std::filesystem::path(".part").native());
        return fs_utils::is_regular_file(zip_path) ||
               fs_utils::is_regular_file(part_path);
    } catch (const std::filesystem::filesystem_error&) {
        return false;
    }
}

void ArgosModelManager::validate() const {
    if (!is_installed()) {
        throw std::runtime_error(
            "Пакет Argos не установлен или повреждён: " +
            package_path().string());
    }
}

std::optional<PackageInfo> ArgosModelManager::catalog_entry() const {
    for (const auto& item : available_packages()) {
        if (item.from_code == source_code_ && item.to_code == target_code_) {
            return item;
        }
    }
    return std::nullopt;
}

void ArgosModelManager::set_package_url(std::string url) {
    package_url_override_ = std::move(url);
}

std::vector<PackageInfo> ArgosModelManager::installed_packages(
    const std::filesystem::path& packages_root) {
    std::vector<PackageInfo> result;
    try {
        if (!std::filesystem::is_directory(packages_root)) {
            return result;
        }
        for (const auto& entry :
             std::filesystem::directory_iterator(packages_root)) {
            if (!entry.is_directory()) {
                continue;
            }
            const auto name = entry.path().filename().string();
            if (name == "_downloads") {
                continue;
            }
            std::string from_code;
            std::string to_code;
            if (!parse_package_name(name, from_code, to_code)) {
                continue;
            }
            ArgosModelManager manager(packages_root, from_code, to_code);
            if (!manager.is_installed()) {
                continue;
            }
            result.push_back(PackageInfo{
                from_code,
                to_code,
                from_code,
                to_code,
                name,
                "argos",
                {},
            });
        }
    } catch (const std::filesystem::filesystem_error&) {
        return {};
    }
    return result;
}

void ArgosModelManager::download_and_install(const ProgressCallback& progress) {
    if (is_installed()) {
        if (progress) {
            progress(1, 1, "Пакет уже установлен");
        }
        return;
    }

    const auto catalog = catalog_entry();
    std::string dirname =
        package_prefix() + std::string(kDefaultPackageVersion);
    std::vector<std::string> urls;
    if (!package_url_override_.empty()) {
        append_unique_url(urls, package_url_override_);
    }
    if (catalog.has_value()) {
        dirname = catalog->dirname;
        for (const auto& catalog_url : catalog->download_urls) {
            append_unique_url(urls, catalog_url);
        }
        append_unique_url(urls, catalog->download_url);
    }
    append_unique_url(urls, data_argos_mirror(dirname));
    if (urls.empty()) {
        throw std::runtime_error(
            "Пакет Argos " + source_code_ + " → " + target_code_ +
            " не найден в каталоге");
    }

    std::filesystem::create_directories(downloads_path());
    const auto zip_path = downloads_path() / (dirname + ".argosmodel");
    if (progress) {
        progress(0, 1, "Скачиваю пакет Argos...");
    }
    download_resumable(urls, zip_path, progress, "Скачиваю пакет Argos");

    const auto extract_root = downloads_path() / ("extract-" + dirname);
    fs_utils::remove_tree(extract_root);
    std::filesystem::create_directories(extract_root);
    extract_zip(zip_path, extract_root);

    std::filesystem::path extracted_package;
    for (const auto& entry : std::filesystem::directory_iterator(extract_root)) {
        if (entry.is_directory() &&
            entry.path().filename().string().rfind(package_prefix(), 0) == 0) {
            extracted_package = entry.path();
            break;
        }
    }
    if (extracted_package.empty() &&
        package_files_ready(extract_root)) {
        extracted_package = extract_root;
    }
    if (extracted_package.empty()) {
        throw std::runtime_error("В архиве Argos нет каталога пакета");
    }

    const auto staging = downloads_path() / dirname;
    if (extracted_package != staging) {
        fs_utils::remove_tree(staging);
        if (extracted_package == extract_root) {
            std::filesystem::create_directories(staging);
            for (const auto& entry :
                 std::filesystem::directory_iterator(extract_root)) {
                std::filesystem::rename(
                    entry.path(), staging / entry.path().filename());
            }
            fs_utils::remove_tree(extract_root);
        } else {
            std::filesystem::rename(extracted_package, staging);
            fs_utils::remove_tree(extract_root);
        }
    }
    fs_utils::remove_tree(zip_path);
    install_from_staging(progress);
}

void ArgosModelManager::install_from_staging(const ProgressCallback& progress) {
    if (is_installed()) {
        if (progress) {
            progress(1, 1, "Пакет уже установлен");
        }
        return;
    }
    const auto staging = staging_package_dir();
    if (!package_files_ready(staging)) {
        throw std::runtime_error(
            "Промежуточные файлы Argos неполные: " + staging.string());
    }
    if (progress) {
        progress(1, 1, "Устанавливаю пакет Argos...");
    }
    const auto installed = packages_root_ / staging.filename();
    fs_utils::atomic_replace_directory(staging, installed);
    if (!fs_utils::directory_has_entries(downloads_path())) {
        fs_utils::remove_tree(downloads_path());
    }
    if (!is_installed()) {
        throw std::runtime_error(
            "Пакет Argos скачан, но не найден среди установленных");
    }
}

void ArgosModelManager::uninstall(const std::function<void()>& unload_models) {
    if (unload_models) {
        unload_models();
    }
    const auto installed = package_path();
    if (std::filesystem::exists(installed) &&
        installed.filename().string().rfind(package_prefix(), 0) == 0) {
        fs_utils::remove_tree(installed);
    }
    const auto staging = staging_package_dir();
    if (std::filesystem::exists(staging) &&
        staging.parent_path() == downloads_path()) {
        fs_utils::remove_tree(staging);
    }
    const auto catalog = catalog_entry();
    const std::string zip_stem =
        catalog ? catalog->dirname
                : (package_prefix() + std::string(kDefaultPackageVersion));
    fs_utils::remove_tree(downloads_path() / (zip_stem + ".argosmodel"));
    fs_utils::remove_tree(std::filesystem::path(
        (downloads_path() / (zip_stem + ".argosmodel")).native() +
        std::filesystem::path(".part").native()));
    if (!fs_utils::directory_has_entries(downloads_path())) {
        fs_utils::remove_tree(downloads_path());
    }
    if (is_installed()) {
        throw std::runtime_error(
            "Пакет Argos " + source_code_ + " → " + target_code_ +
            " не удалось удалить");
    }
}

}  // пространство имён offline_translator
