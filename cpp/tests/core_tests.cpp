#include "offline_translator/app_log.hpp"
#include "offline_translator/app_settings.hpp"
#include "offline_translator/app_language.hpp"
#include "offline_translator/app_update.hpp"
#include "offline_translator/argos_model_manager.hpp"
#include "offline_translator/autostart.hpp"
#include "offline_translator/clipboard.hpp"
#include "offline_translator/clipboard_history.hpp"
#include "offline_translator/firefox_model_manager.hpp"
#include "offline_translator/hotkey.hpp"
#include "offline_translator/language_store.hpp"
#include "offline_translator/marian_model_manager.hpp"
#include "offline_translator/nllb_language.hpp"
#include "offline_translator/nllb_model_manager.hpp"
#include "offline_translator/selection.hpp"
#include "offline_translator/text_split.hpp"
#include "offline_translator/translation_service.hpp"
#include "offline_translator/window_policy.hpp"
#include "file_transfer.hpp"
#include "compression.hpp"
#include "fs_utils.hpp"
#include "zip_archive.hpp"

#include <array>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <zconf.h>
#include <zlib.h>
#include <zstd.h>

using namespace offline_translator;

namespace {

void write_dummy_file(const std::filesystem::path& path, std::size_t size = 1) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    const std::string payload(size, 'x');
    out.write(payload.data(), static_cast<std::streamsize>(payload.size()));
}

void write_argos_package_files(const std::filesystem::path& package) {
    std::filesystem::create_directories(package / "model");
    for (const auto file_name : {
             "model/model.bin",
             "model/config.json",
             "model/shared_vocabulary.json",
             "sentencepiece.model",
         }) {
        write_dummy_file(package / file_name);
    }
}

void write_marian_files(const std::filesystem::path& root, std::size_t model_size) {
    std::filesystem::create_directories(root);
    write_dummy_file(root / "model.bin", model_size);
    write_dummy_file(root / "shared_vocabulary.json", 32);
    write_dummy_file(root / "source.spm", 32);
    write_dummy_file(root / "target.spm", 32);
    write_dummy_file(root / "config.json", 32);
}

void write_nllb_files(const std::filesystem::path& root, std::size_t model_size) {
    write_dummy_file(root / "model.bin", model_size);
    write_dummy_file(root / "shared_vocabulary.json");
    write_dummy_file(root / "sentencepiece.bpe.model");
}

void require(bool ok, const std::string& message) {
    if (!ok) {
        throw std::runtime_error("FAIL: " + message);
    }
}

constexpr std::string_view kArgosIndexFixtureJson = R"JSON(
[
  {
    "package_version": "1.9",
    "argos_version": "1.9.0",
    "from_code": "en",
    "from_name": "English",
    "to_code": "ru",
    "to_name": "Russian",
    "links": ["https://argos-net.com/v1/translate-en_ru-1_9.argosmodel"],
    "code": "translate-en_ru"
  },
  {
    "package_version": "1.9",
    "argos_version": "1.9.0",
    "from_code": "ru",
    "from_name": "Russian",
    "to_code": "en",
    "to_name": "English",
    "links": ["https://argos-net.com/v1/translate-ru_en-1_9.argosmodel"],
    "code": "translate-ru_en"
  },
  {
    "package_version": "1.3",
    "argos_version": "1.3",
    "from_code": "de",
    "from_name": "German",
    "to_code": "en",
    "to_name": "English",
    "links": ["https://argos-net.com/v1/translate-de_en-1_3.argosmodel"],
    "code": "translate-de_en"
  },
  {
    "package_version": "1.5",
    "argos_version": "1.5",
    "from_code": "fr",
    "from_name": "French",
    "to_code": "en",
    "to_name": "English",
    "links": [
      "https://argos-net.com/v1/translate-fr_en-1_5.argosmodel",
      "ipfs://QmFixtureOnly"
    ],
    "code": "translate-fr_en"
  },
  {
    "type": "sbd",
    "from_code": "en",
    "from_name": "English",
    "to_code": "en",
    "to_name": "English",
    "links": ["https://example.invalid/sbd-en.argosmodel"],
    "code": "sbd-en"
  },
  {
    "package_version": "1.0",
    "from_code": "xx",
    "from_name": "Unused",
    "to_code": "yy",
    "to_name": "Unused",
    "links": ["ipfs://QmNoHttpLink"],
    "code": "translate-xx_yy"
  }
]
)JSON";

bool catalog_has_pair(
    const std::vector<PackageInfo>& catalog,
    std::string_view from_code,
    std::string_view to_code) {
    for (const auto& item : catalog) {
        if (item.from_code == from_code && item.to_code == to_code) {
            return true;
        }
    }
    return false;
}

class ArgosIndexGuard {
public:
    explicit ArgosIndexGuard(
        std::filesystem::path cache_path,
        std::string url = {}) {
        ArgosModelManager::set_index_cache_path(std::move(cache_path));
        ArgosModelManager::set_index_url(std::move(url));
    }
    ~ArgosIndexGuard() {
        ArgosModelManager::set_index_cache_path({});
        ArgosModelManager::set_index_url({});
    }
    ArgosIndexGuard(const ArgosIndexGuard&) = delete;
    ArgosIndexGuard& operator=(const ArgosIndexGuard&) = delete;
};

}  // анонимное пространство имён

using namespace offline_translator;

int main() {
    try {
    const NllbModelManager model_manager("C:/offline-translator-test-models");
    require(model_manager.model_path().filename() == "nllb-200-distilled-600M", "путь модели NLLB");
    require(!model_manager.is_installed(), "пустой корень NLLB не установлен");
    bool model_validation_thrown = false;
    try {
        model_manager.validate();
    } catch (const std::runtime_error&) {
        model_validation_thrown = true;
    }
    require(model_validation_thrown, "validate() бросает без модели");

    require(nllb_language_code("en") == "eng_Latn", "NLLB-код en");
    require(nllb_language_code("ru") == "rus_Cyrl", "NLLB-код ru");
    require(nllb_language_code("rus_Cyrl") == "rus_Cyrl", "готовый NLLB-код сохраняется");
    bool unknown_language_thrown = false;
    try {
        static_cast<void>(nllb_language_code("xx"));
    } catch (const std::runtime_error&) {
        unknown_language_thrown = true;
    }
    require(unknown_language_thrown, "неизвестный язык NLLB бросает");

    {
        require(split_sentences("").empty(), "пустой текст без предложений");
        const auto one = split_sentences("Hello world");
        require(one.size() == 1 && one[0] == "Hello world", "одно предложение без точки");
        const auto two = split_sentences("Hello. World.");
        require(two.size() == 2, "два предложения по точке");
        require(two[0] == "Hello." && two[1] == "World.", "границы предложений");
        const auto lines = split_sentences("A\n\nB");
        require(lines.size() == 2 && lines[0] == "A" && lines[1] == "B", "разрез по переводам строк");
        const auto ellipsis = split_sentences("Ждём… Потом.");
        require(
            ellipsis.size() == 2 && ellipsis[0] == "Ждём…" && ellipsis[1] == "Потом.",
            "разрез после многоточия");
    }

    const std::set<LanguagePair> installed{
        {"ru", "en"},
        {"en", "ru"},
        {"en", "fr"},
    };
    const IsInstalled is_installed =
        [&installed](std::string_view source, std::string_view target) {
            return installed.contains(
                LanguagePair{std::string(source), std::string(target)});
        };
    const DirectTranslate direct_translate =
        [](std::string_view text, std::string_view source, std::string_view target) {
            return "[" + std::string(source) + ">" + std::string(target) +
                   "] " + std::string(text);
        };

    require(english_pivot_route(is_installed, "ru", "en") == "direct", "ru->en прямой маршрут");
    require(!english_pivot_route(is_installed, "ru", "ru").has_value(), "ru->ru без маршрута");
    require(!english_pivot_route(is_installed, "de", "fr").has_value(), "de->fr без моделей");

    // В наборе уже есть en->fr, поэтому для de->fr не хватает только de->en.
    const auto needed = needed_english_pivot_pairs("de", "fr", is_installed);
    require(needed.size() == 1, "нужна одна модель для пивота de->fr");
    require((needed[0] == LanguagePair{"de", "en"}), "недостающая нога de->en");

    const auto argos_test_root =
        std::filesystem::temp_directory_path() / "offline-translator-argos-test";
    std::filesystem::remove_all(argos_test_root);
    const auto argos_package =
        argos_test_root / "translate-en_ru-2_0";
    std::filesystem::create_directories(argos_package / "model");
    for (const auto file_name : {
             "model/model.bin",
             "model/config.json",
             "model/shared_vocabulary.json",
             "sentencepiece.model",
         }) {
        std::ofstream(argos_package / file_name).put('\0');
    }
    const ArgosModelManager argos_manager(argos_test_root, "en", "ru");
    require(argos_manager.package_path() == argos_package, "путь пакета Argos");
    require(argos_manager.is_installed(), "пакет Argos установлен");

    // Новый формат пакета Argos (1.5+): без model/config.json и со
    // shared_vocabulary.txt вместо shared_vocabulary.json.
    const auto argos_new_package = argos_test_root / "translate-eo_en-1_5";
    std::filesystem::create_directories(argos_new_package / "model");
    for (const auto file_name : {
             "model/model.bin",
             "model/shared_vocabulary.txt",
             "sentencepiece.model",
         }) {
        std::ofstream(argos_new_package / file_name).put('\0');
    }
    const ArgosModelManager argos_new_manager(argos_test_root, "eo", "en");
    require(
        argos_new_manager.is_installed(),
        "пакет Argos нового формата распознаётся");

    // Пакет без словаря любого формата не считается установленным.
    const auto argos_broken_package = argos_test_root / "translate-en_tl-1_9";
    std::filesystem::create_directories(argos_broken_package / "model");
    std::ofstream(argos_broken_package / "model" / "model.bin").put('\0');
    std::ofstream(argos_broken_package / "sentencepiece.model").put('\0');
    const ArgosModelManager argos_broken_manager(argos_test_root, "en", "tl");
    require(
        !argos_broken_manager.is_installed(),
        "пакет без словаря не считается установленным");
    std::filesystem::remove_all(argos_test_root);

    const auto marian_root =
        std::filesystem::temp_directory_path() / "offline-translator-marian-test";
    std::filesystem::remove_all(marian_root);
    {
        MarianModelManager missing(marian_root, "en", "ru", 8);
        require(!missing.is_installed(), "пустой корень Marian не установлен");
        const auto catalog = MarianModelManager::available_packages();
        bool has_en_ru = false;
        for (const auto& item : catalog) {
            if (item.from_code == "en" && item.to_code == "ru") {
                has_en_ru = true;
                require(item.architecture == "marian", "архитектура Marian");
            }
        }
        require(has_en_ru, "каталог Marian содержит en→ru");
        require(
            engine_kind_from_settings("marian") == EngineKind::marian,
            "settings marian → EngineKind");
        require(
            settings_engine_name(EngineKind::marian) == "marian",
            "EngineKind marian → settings");
    }
    {
        MarianModelManager manager(marian_root, "en", "ru", 8);
        write_marian_files(manager.staging_path(), 16);
        manager.install_from_staging();
        require(manager.is_installed(), "Marian установлен из staging");
        require(
            manager.model_path().filename() == "en-ru",
            "каталог пары Marian en-ru");
        const auto installed =
            MarianModelManager::installed_packages(marian_root);
        require(!installed.empty(), "installed_packages видит Marian en-ru");
        bool unloaded = false;
        manager.uninstall([&] { unloaded = true; });
        require(unloaded, "uninstall Marian вызывает выгрузку");
        require(!manager.is_installed(), "Marian удалён");
    }
    std::filesystem::remove_all(marian_root);

    TranslationService service(is_installed, direct_translate, "Test");
    const auto direct = service.translate(" hello ", "ru", "en");
    require(direct.text == "[ru>en] hello", "прямой перевод");
    require(!direct.intermediate.has_value(), "у прямого перевода нет промежуточного");

    const auto pivot = service.translate(" привет ", "ru", "fr");
    require(pivot.text == "[en>fr] [ru>en] привет", "пивот через английский");
    require(pivot.intermediate == "[ru>en] привет", "промежуточный текст пивота");
    require(pivot.pivot_code == "en", "код пивота en");

    const auto empty = service.translate("   ", "ru", "fr");
    require(empty.text == "   ", "пустой ввод возвращается как есть");
    require(!empty.intermediate.has_value(), "у пустого ввода нет пивота");

    bool missing_thrown = false;
    try {
        static_cast<void>(service.translate("text", "de", "fr"));
    } catch (const std::runtime_error& error) {
        missing_thrown = std::string(error.what()).find("de->en") !=
                         std::string::npos;
    }
    require(missing_thrown, "ошибка называет недостающую модель de->en");

    service.stop();
    bool stopped_thrown = false;
    try {
        static_cast<void>(service.translate("text", "ru", "en"));
    } catch (const std::runtime_error&) {
        stopped_thrown = true;
    }
    require(stopped_thrown, "остановленный сервис бросает");

    const auto nllb_root =
        std::filesystem::temp_directory_path() / "offline-translator-nllb-mgmt";
    std::filesystem::remove_all(nllb_root);
    {
        NllbModelManager missing(nllb_root, 1);
        require(!missing.is_installed(), "пустой корень NLLB не установлен");
        require(
            !missing.has_incomplete_package(),
            "пустой корень NLLB без обломков");
        const auto catalog = missing.available_packages();
        require(catalog.size() == 1, "каталог NLLB содержит один пакет");
        require(catalog[0].from_code == "nllb", "код пакета NLLB");
        missing.update_remote_index();
    }
    {
        NllbModelManager incomplete(nllb_root, 1);
        write_dummy_file(incomplete.staging_path() / "model.bin", 32);
        write_dummy_file(incomplete.staging_path() / "model.bin.part", 8);
        require(
            !incomplete.is_installed(),
            "остатки _downloads NLLB не считаются установленными");
        require(
            incomplete.has_incomplete_package(),
            "остатки _downloads NLLB — незавершённый пакет");
    }
    {
        NllbModelManager manager(nllb_root, 8);
        write_nllb_files(manager.staging_path(), 16);
        int progress_calls = 0;
        std::string last_message;
        manager.install_from_staging(
            [&](std::uint64_t, std::uint64_t, std::string_view message) {
                ++progress_calls;
                last_message = std::string(message);
            });
        require(manager.is_installed(), "NLLB установлен из staging");
        require(progress_calls > 0, "callback прогресса NLLB вызван");
        require(
            !manager.has_incomplete_package(),
            "после установки NLLB нет незавершённого пакета");
        require(
            !std::filesystem::exists(manager.downloads_path()),
            "пустой _downloads NLLB убран");
        bool unloaded = false;
        manager.uninstall([&] { unloaded = true; });
        require(unloaded, "uninstall NLLB вызывает выгрузку");
        require(!manager.is_installed(), "NLLB удалён");
        require(
            !std::filesystem::exists(manager.model_path()),
            "каталог модели NLLB удалён");
    }
    {
        const auto sources =
            std::filesystem::temp_directory_path() / "offline-translator-nllb-src";
        std::filesystem::remove_all(sources);
        write_nllb_files(sources, 64);
        NllbModelManager manager(nllb_root, 32);
        manager.set_source_url("model.bin", (sources / "model.bin").string());
        manager.set_source_url(
            "shared_vocabulary.json",
            (sources / "shared_vocabulary.json").string());
        manager.set_source_url(
            "sentencepiece.bpe.model",
            (sources / "sentencepiece.bpe.model").string());
        const auto part = std::filesystem::path(
            manager.staging_path() / "model.bin.part");
        std::filesystem::create_directories(part.parent_path());
        {
            std::ifstream in(sources / "model.bin", std::ios::binary);
            std::string prefix(20, '\0');
            in.read(prefix.data(), 20);
            std::ofstream(part, std::ios::binary)
                .write(prefix.data(), in.gcount());
        }
        int progress_calls = 0;
        manager.download_and_install(
            [&](std::uint64_t, std::uint64_t, std::string_view) {
                ++progress_calls;
            });
        require(manager.is_installed(), "NLLB установлен после докачки");
        require(progress_calls > 0, "докачка NLLB вызывает прогресс");
        require(
            std::filesystem::file_size(manager.model_path() / "model.bin") == 64,
            "докачанный model.bin полного размера");
        manager.uninstall();
        std::filesystem::remove_all(sources);
    }

    const auto argos_mgmt_root =
        std::filesystem::temp_directory_path() / "offline-translator-argos-mgmt";
    std::filesystem::remove_all(argos_mgmt_root);
    const auto argos_index_root =
        std::filesystem::temp_directory_path() / "offline-translator-argos-index";
    std::filesystem::remove_all(argos_index_root);
    std::filesystem::create_directories(argos_index_root);
    const auto isolated_index = argos_index_root / "index.json";
    const auto missing_index_source = argos_index_root / "missing-source.json";
    const auto fixture_path = argos_index_root / "argospm_index.json";
    fs_utils::write_text_file(fixture_path, kArgosIndexFixtureJson);
    ArgosIndexGuard argos_index_guard(isolated_index, missing_index_source.string());
    {
        ArgosModelManager missing(argos_mgmt_root, "en", "ru");
        require(!missing.is_installed(), "пустой корень Argos не установлен");
        require(
            !missing.has_incomplete_package(),
            "пустой корень Argos без обломков");
        require(
            ArgosModelManager::available_packages().size() == 2,
            "нет кэша → встроенный каталог Argos en↔ru");
        ArgosModelManager::update_remote_index();
        require(
            ArgosModelManager::available_packages().size() == 2,
            "неудачное обновление индекса не роняет каталог");
        require(
            !std::filesystem::exists(isolated_index),
            "при ошибке загрузки кэш индекса не создаётся");
    }
    {
        std::filesystem::copy_file(
            fixture_path,
            isolated_index,
            std::filesystem::copy_options::overwrite_existing);
        const auto catalog = ArgosModelManager::available_packages();
        require(
            catalog.size() > 2,
            "фикстура индекса Argos даёт больше двух пар");
        require(catalog_has_pair(catalog, "en", "ru"), "фикстура содержит en→ru");
        require(catalog_has_pair(catalog, "de", "en"), "фикстура содержит de→en");
        require(catalog_has_pair(catalog, "fr", "en"), "фикстура содержит fr→en");
        require(
            !catalog_has_pair(catalog, "xx", "yy"),
            "пакет только с ipfs:// не попадает в каталог");
        bool de_en_has_url = false;
        for (const auto& item : catalog) {
            if (item.from_code == "de" && item.to_code == "en") {
                de_en_has_url = !item.download_url.empty() &&
                    item.dirname.find("translate-de_en") == 0;
            }
        }
        require(de_en_has_url, "у de→en есть URL и dirname пакета");
        bool de_en_has_mirror = false;
        for (const auto& item : catalog) {
            if (item.from_code == "de" && item.to_code == "en") {
                for (const auto& url : item.download_urls) {
                    if (url.find("data.argosopentech.com") != std::string::npos) {
                        de_en_has_mirror = true;
                    }
                }
            }
        }
        require(de_en_has_mirror, "у de→en есть зеркало data.argosopentech.com");
        static_cast<void>(
            ArgosModelManager(argos_mgmt_root, "de", "en").has_incomplete_package());
    }
    {
        fs_utils::write_text_file(isolated_index, "{это не индекс Argos");
        const auto catalog = ArgosModelManager::available_packages();
        require(
            catalog.size() == 2,
            "битый индекс → встроенные en↔ru");
        require(catalog_has_pair(catalog, "en", "ru"), "fallback en→ru");
        require(catalog_has_pair(catalog, "ru", "en"), "fallback ru→en");
    }
    {
        std::filesystem::remove(isolated_index);
        ArgosModelManager::set_index_url(fixture_path.string());
        ArgosModelManager::update_remote_index();
        require(
            std::filesystem::is_regular_file(
                ArgosModelManager::index_cache_path()),
            "update_remote_index пишет кэш из локального файла");
        const auto catalog = ArgosModelManager::available_packages();
        require(
            catalog.size() > 2,
            "кэш после update_remote_index читается как каталог");
        require(catalog_has_pair(catalog, "de", "en"), "кэш содержит de→en");
        ArgosModelManager::set_index_url(missing_index_source.string());
    }
    {
        const auto live_cache = argos_index_root / "live-index.json";
        ArgosModelManager::set_index_cache_path(live_cache);
        ArgosModelManager::set_index_url({});
        ArgosModelManager::update_remote_index();
        const auto live = ArgosModelManager::available_packages();
        if (live.size() > 2 && std::filesystem::is_regular_file(live_cache)) {
            std::cout << "live argos index: " << live.size() << " packages\n";
        } else {
            std::cout << "skip live argos index: сеть недоступна, fallback\n";
        }
        ArgosModelManager::set_index_cache_path(isolated_index);
        ArgosModelManager::set_index_url(missing_index_source.string());
        std::filesystem::remove(isolated_index);
    }
    {
        ArgosModelManager incomplete(argos_mgmt_root, "en", "ru");
        write_argos_package_files(
            incomplete.downloads_path() / "translate-en_ru-1_9");
        require(
            !incomplete.is_installed(),
            "staging Argos в _downloads не считается установленным");
        require(
            incomplete.has_incomplete_package(),
            "staging Argos — незавершённый пакет");
        int progress_calls = 0;
        incomplete.install_from_staging(
            [&](std::uint64_t, std::uint64_t, std::string_view) {
                ++progress_calls;
            });
        require(incomplete.is_installed(), "Argos установлен из staging");
        require(progress_calls > 0, "callback прогресса Argos вызван");
        require(
            incomplete.package_path().filename() == "translate-en_ru-1_9",
            "имя установленного пакета Argos");
        bool unloaded = false;
        incomplete.uninstall([&] { unloaded = true; });
        require(unloaded, "uninstall Argos вызывает выгрузку");
        require(!incomplete.is_installed(), "Argos удалён");
    }
    {
        const auto source_pkg =
            std::filesystem::temp_directory_path() / "offline-translator-argos-src" /
            "translate-en_ru-1_9";
        std::filesystem::remove_all(source_pkg.parent_path());
        write_argos_package_files(source_pkg);
        const auto zip_path = source_pkg.parent_path() / "en_ru.argosmodel";
        write_store_zip(
            zip_path,
            {
                {"translate-en_ru-1_9/model/model.bin",
                 source_pkg / "model" / "model.bin"},
                {"translate-en_ru-1_9/model/config.json",
                 source_pkg / "model" / "config.json"},
                {"translate-en_ru-1_9/model/shared_vocabulary.json",
                 source_pkg / "model" / "shared_vocabulary.json"},
                {"translate-en_ru-1_9/sentencepiece.model",
                 source_pkg / "sentencepiece.model"},
            });
        ArgosModelManager manager(argos_mgmt_root, "en", "ru");
        manager.set_package_url(zip_path.string());
        int progress_calls = 0;
        manager.download_and_install(
            [&](std::uint64_t, std::uint64_t, std::string_view) {
                ++progress_calls;
            });
        require(manager.is_installed(), "Argos установлен из локального zip");
        require(progress_calls > 0, "установка Argos из zip вызывает прогресс");
        const auto installed = ArgosModelManager::installed_packages(argos_mgmt_root);
        require(!installed.empty(), "список установленных Argos не пуст");
        manager.uninstall();
        std::filesystem::remove_all(source_pkg.parent_path());
    }
    {
        const auto& languages = supported_languages();
        require(languages.size() == 64, "таблица языков: 64 записи");
        std::set<std::string> unique_codes;
        for (const auto& entry : languages) {
            unique_codes.insert(entry.code);
            require(
                !entry.name.empty() && entry.name != entry.code,
                "у языка " + entry.code + " есть русское название");
        }
        require(unique_codes.size() == languages.size(), "коды языков уникальны");
        require(language_store_name("en") == "Английский", "имя en");
        require(language_store_name("ru") == "Русский", "имя ru");
        require(
            language_store_name("nb") == "Норвежский (букмол)",
            "имя nb с уточнением");
        require(
            language_store_name("zz", "Запасное имя") == "Запасное имя",
            "неизвестный код → fallback");
        require(
            language_store_name("zz") == "zz",
            "неизвестный код без fallback → сам код");
        // Языки, которые предлагает Argos, должны быть в списке окна.
        for (const auto code : {"eo", "eu", "ga", "ky", "pb", "sw", "tl", "zt"}) {
            require(
                language_store_name(code) != code,
                std::string("язык Argos есть в списке: ") + code);
        }

        PackageInfo en_ru;
        en_ru.from_code = "en";
        en_ru.to_code = "ru";
        PackageInfo de_en;
        de_en.from_code = "de";
        de_en.to_code = "en";
        PackageInfo ru_en;
        ru_en.from_code = "ru";
        ru_en.to_code = "en";
        PackageInfo zz_qq;
        zz_qq.from_code = "zz";
        zz_qq.to_code = "qq";

        const auto merged = merge_store_pairs({en_ru, de_en}, {ru_en, de_en});
        require(merged.size() == 4, "слияние: NLLB + 3 пары Argos без дублей");
        bool has_nllb = false;
        int de_en_rows = 0;
        for (const auto& pair : merged) {
            has_nllb = has_nllb || pair.nllb;
            if (!pair.nllb && pair.from_code == "de" && pair.to_code == "en") {
                ++de_en_rows;
                require(pair.installed, "de→en из установленного списка помечен");
            }
            if (!pair.nllb && pair.from_code == "ru" && pair.to_code == "en") {
                require(pair.installed, "ru→en помечен установленным");
            }
            if (!pair.nllb && pair.from_code == "en" && pair.to_code == "ru") {
                require(!pair.installed, "en→ru только в каталоге — не установлен");
            }
        }
        require(has_nllb, "в магазине есть строка NLLB");
        require(de_en_rows == 1, "de→en не дублируется между каталогом и диском");

        auto pairs = merge_store_pairs({en_ru, de_en, zz_qq}, {ru_en});
        require(pairs.size() == 5, "перед сортировкой 5 записей");
        for (auto& pair : pairs) {
            if (!pair.nllb && pair.from_code == "zz") {
                pair.incomplete = true;
            }
        }
        sort_store_pairs(pairs);
        require(pairs.front().nllb, "NLLB всегда первая");
        const auto* first_argos = &pairs[1];
        require(
            first_argos->from_code == "ru" && first_argos->to_code == "en",
            "установленная популярная пара сразу после NLLB");
        const auto* second_argos = &pairs[2];
        require(
            second_argos->from_code == "zz" && second_argos->to_code == "qq",
            "повреждённая непопулярная пара выше непопулярных без пакета");
        const auto* third_argos = &pairs[3];
        require(
            third_argos->from_code == "de" || third_argos->from_code == "en",
            "дальше идут популярные пары по алфавиту");

        StorePair search_pair;
        search_pair.from_code = "de";
        search_pair.to_code = "en";
        require(store_pair_matches(search_pair, ""), "пустой запрос совпадает со всем");
        require(store_pair_matches(search_pair, "DE"), "поиск по коду без регистра");
        require(
            store_pair_matches(search_pair, "немецкий"),
            "поиск по русскому названию источника");
        require(
            store_pair_matches(search_pair, "Английский"),
            "поиск по русскому названию цели");
        require(!store_pair_matches(search_pair, "qq"), "мимо — нет совпадения");
        StorePair ru_en_pair;
        ru_en_pair.from_code = "ru";
        ru_en_pair.to_code = "en";
        require(store_pair_matches(ru_en_pair, "рус"), "кириллица в нижнем регистре");
        StorePair nllb_search;
        nllb_search.nllb = true;
        require(store_pair_matches(nllb_search, "600M"), "строка NLLB ищется по имени");
        require(!store_pair_matches(nllb_search, "argos"), "NLLB не отвечает на argos");

        require(
            store_pair_label(search_pair) == "Немецкий → Английский",
            "подпись пары из русских названий");
        require(
            store_pair_label(nllb_search) == "NLLB-200 Distilled 600M",
            "подпись строки NLLB");
    }
    {
        // Распаковка gzip и zstd (модели Firefox приходят сжатыми).
        const std::string payload = "model bytes for firefox smoke test payload";
        std::vector<std::uint8_t> decompressed;
        {
            // Готовим gzip-файстуру через zlib (deflate c gzip-обёрткой).
            z_stream stream{};
            deflateInit2(&stream, 6, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY);
            stream.next_in = reinterpret_cast<Bytef*>(
                const_cast<char*>(payload.data()));
            stream.avail_in = static_cast<uInt>(payload.size());
            std::vector<std::uint8_t> gz;
            std::array<std::uint8_t, 4096> buffer{};
            int status = Z_OK;
            do {
                stream.next_out = buffer.data();
                stream.avail_out = static_cast<uInt>(buffer.size());
                status = deflate(&stream, Z_FINISH);
                gz.insert(gz.end(), buffer.begin(), buffer.end() - stream.avail_out);
            } while (status == Z_OK);
            deflateEnd(&stream);
            require(status == Z_STREAM_END, "gzip-фикстура создана");
            require(
                compression::gunzip(gz.data(), gz.size(), decompressed),
                "gunzip распаковывает поток");
            require(
                std::string(decompressed.begin(), decompressed.end()) == payload,
                "содержимое после gunzip совпадает");
        }
        {
            const std::size_t bound = ZSTD_compressBound(payload.size());
            std::vector<std::uint8_t> zstd_payload(bound);
            const std::size_t compressed = ZSTD_compress(
                zstd_payload.data(),
                zstd_payload.size(),
                payload.data(),
                payload.size(),
                3);
            require(!ZSTD_isError(compressed), "zstd-фикстура создана");
            require(
                compression::zunstd(
                    zstd_payload.data(), compressed, decompressed),
                "zunstd распаковывает кадр");
            require(
                std::string(decompressed.begin(), decompressed.end()) == payload,
                "содержимое после zunstd совпадает");
            // Регрессия: поток, оборванный посередине кадра, раньше
            // считался успешно распакованным, и битый model.bin
            // устанавливался как модель.
            if (compressed > 4) {
                require(
                    !compression::zunstd(
                        zstd_payload.data(), compressed - 3, decompressed),
                    "zunstd отвергает обрезанный кадр");
            }
        }
        require(!compression::gunzip(nullptr, 0, decompressed), "gunzip: пустой вход");
        require(!compression::zunstd(nullptr, 0, decompressed), "zunstd: пустой вход");
    }
    {
        // Менеджер моделей Firefox: раскладка как language_packages.py.
        const auto root =
            std::filesystem::temp_directory_path() / "offline-translator-fx";
        std::filesystem::remove_all(root);
        const auto write_ready_model = [](const std::filesystem::path& dir) {
            std::filesystem::create_directories(dir);
            fs_utils::write_text_file(dir / "model.bin", "model");
            fs_utils::write_text_file(dir / "vocab.spm", "vocab");
        };
        const auto base_pair = root / "base" / "en-ru";
        write_ready_model(base_pair);
        const auto tiny_staging = root / "_downloads" / "tiny-en-fr";
        std::filesystem::create_directories(tiny_staging);
        fs_utils::write_text_file(tiny_staging / "model.bin", "part");

        offline_translator::FirefoxModelManager manager(root, "base", "en", "ru");
        require(manager.is_installed(), "Firefox base/en-ru установлен");
        require(
            manager.resolve_model_path().value() == base_pair,
            "resolve указывает на base/en-ru");

        offline_translator::FirefoxModelManager missing(root, "base", "de", "en");
        require(!missing.is_installed(), "de→en не установлен");
        require(missing.has_incomplete_package() == false,
                "staging другого размера не считается повреждением de→en");
        offline_translator::FirefoxModelManager broken(root, "tiny", "en", "fr");
        require(broken.has_incomplete_package(),
                "недокачанный tiny staging — незавершённый пакет");

        // Legacy-плоский каталог для tiny.
        write_ready_model(root / "uk-en");
        offline_translator::FirefoxModelManager legacy(root, "tiny", "uk", "en");
        require(legacy.is_installed(), "legacy-плоский каталог tiny находится");

        const auto installed =
            offline_translator::FirefoxModelManager::installed_packages(root);
        require(installed.size() >= 2, "installed_packages видит обе пары");
        bool saw_en_ru = false;
        for (const auto& package : installed) {
            if (package.from_code == "en" && package.to_code == "ru") {
                saw_en_ru = true;
                require(package.architecture == "base",
                        "metadata/каталог даёт архитектуру base");
            }
        }
        require(saw_en_ru, "en→ru в списке установленных");

        // Пивот через английский для Firefox: tr→ru возможен только если
        // установлены tr→en и en→ru. Проверяем, что при наличии только
        // en→ru недостающей называется именно tr→en (случай из отчёта).
        const auto firefox_installed =
            [&root](std::string_view from, std::string_view to) {
                return offline_translator::FirefoxModelManager(
                           root,
                           "tiny",
                           std::string(from),
                           std::string(to))
                    .is_installed();
            };
        require(
            firefox_installed("en", "ru"),
            "Firefox en→ru найден для пивота");
        require(
            !firefox_installed("tr", "en"),
            "Firefox tr→en отсутствует");
        const auto tr_ru_needed = offline_translator::needed_english_pivot_pairs(
            "tr", "ru", firefox_installed);
        require(
            tr_ru_needed.size() == 1,
            "для tr→ru нужна ровно одна модель");
        require(
            tr_ru_needed[0] == offline_translator::LanguagePair{"tr", "en"},
            "недостающая модель для tr→ru — tr→en");

        // Каталог: без кэша → встроенный fallback; запись en→ru есть.
        const auto available_base =
            offline_translator::FirefoxModelManager::available_packages(root, "base");
        bool fallback_has_en_ru = false;
        for (const auto& package : available_base) {
            if (package.from_code == "en" && package.to_code == "ru") {
                fallback_has_en_ru = true;
                require(package.dirname == "enru",
                        "fallback dirname пары enru");
            }
        }
        require(fallback_has_en_ru, "fallback-каталог base содержит en→ru");
        const auto available_tiny =
            offline_translator::FirefoxModelManager::available_packages(root, "tiny");
        require(available_tiny.size() > 70, "fallback tiny содержит ~80 пар");

        // Удаление убирает и установленный каталог, и staging.
        manager.uninstall();
        require(!manager.is_installed(), "после uninstall модель не найдена");
        broken.uninstall();
        require(!broken.has_incomplete_package(),
                "uninstall чистит _downloads");

        // Каталог firefox-models может отсутствовать (чистая установка):
        // перечисление установленных пакетов не должно бросать исключение.
        const auto missing_root =
            std::filesystem::temp_directory_path() /
            "offline-translator-firefox-missing";
        std::filesystem::remove_all(missing_root);
        const auto none =
            offline_translator::FirefoxModelManager::installed_packages(
                missing_root);
        require(
            none.empty(),
            "installed_packages на отсутствующем каталоге пуст");
        std::filesystem::remove_all(root);
    }
    {
        const unsigned char deflate_zip[] = {
            0x50,0x4b,0x03,0x04,0x14,0x00,0x00,0x00,0x08,0x00,0x53,0xaa,0x19,0x5d,
            0xf0,0x2c,0x10,0x7f,0x0f,0x00,0x00,0x00,0x0d,0x00,0x00,0x00,0x0d,0x00,
            0x00,0x00,0x64,0x69,0x72,0x2f,0x68,0x65,0x6c,0x6c,0x6f,0x2e,0x74,0x78,
            0x74,0xcb,0x48,0xcd,0xc9,0xc9,0x57,0x48,0x49,0x4d,0xcb,0x49,0x2c,0x49,
            0x05,0x00,0x50,0x4b,0x01,0x02,0x14,0x00,0x14,0x00,0x00,0x00,0x08,0x00,
            0x53,0xaa,0x19,0x5d,0xf0,0x2c,0x10,0x7f,0x0f,0x00,0x00,0x00,0x0d,0x00,
            0x00,0x00,0x0d,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
            0x80,0x01,0x00,0x00,0x00,0x00,0x64,0x69,0x72,0x2f,0x68,0x65,0x6c,0x6c,
            0x6f,0x2e,0x74,0x78,0x74,0x50,0x4b,0x05,0x06,0x00,0x00,0x00,0x00,0x01,
            0x00,0x01,0x00,0x3b,0x00,0x00,0x00,0x3a,0x00,0x00,0x00,0x00,0x00};
        const auto zip_root =
            std::filesystem::temp_directory_path() / "offline-translator-deflate-zip";
        std::filesystem::remove_all(zip_root);
        const auto zip_path = zip_root / "tiny.argosmodel";
        std::filesystem::create_directories(zip_root);
        {
            std::ofstream out(zip_path, std::ios::binary);
            out.write(
                reinterpret_cast<const char*>(deflate_zip),
                sizeof(deflate_zip));
        }
        extract_zip(zip_path, zip_root / "out");
        {
            std::ifstream in(zip_root / "out" / "dir" / "hello.txt");
            std::string text;
            std::getline(in, text);
            require(text == "hello deflate", "распаковка DEFLATE ZIP");
        }
        std::filesystem::remove_all(zip_root);
    }

    {
        // Регрессия: битый CRC и завышенное смещение локального заголовка
        // раньше проходили как «успешная» распаковка (uint32 переполнялся,
        // CRC не проверялся) — extract_zip обязан бросать исключение.
        const auto guard_root =
            std::filesystem::temp_directory_path() /
            "offline-translator-deflate-guard";
        std::filesystem::remove_all(guard_root);
        std::filesystem::create_directories(guard_root);
        const auto good_zip = guard_root / "good.zip";
        const auto payload_file = guard_root / "payload.txt";
        fs_utils::write_text_file(payload_file, "hello guard");
        write_store_zip(good_zip, {{"payload.txt", payload_file}});
        extract_zip(good_zip, guard_root / "out-good");
        require(
            fs_utils::file_size_or_zero(guard_root / "out-good" / "payload.txt") ==
                11,
            "эталонный ZIP распаковывается");
        auto bytes = std::vector<unsigned char>{};
        {
            std::ifstream in(good_zip, std::ios::binary);
            bytes.assign(
                std::istreambuf_iterator<char>(in),
                std::istreambuf_iterator<char>());
        }
        require(!bytes.empty(), "фикстура ZIP прочитана");
        std::size_t central_offset = 0;
        for (std::size_t i = 0; i + 4 <= bytes.size(); ++i) {
            if (bytes[i] == 0x50 && bytes[i + 1] == 0x4b &&
                bytes[i + 2] == 0x01 && bytes[i + 3] == 0x02) {
                central_offset = i;
                break;
            }
        }
        require(central_offset > 0, "центральный каталог найден в фикстуре");
        bool threw = false;
        try {
            auto broken = bytes;
            // CRC32 в записи центрального каталога (+16).
            broken[central_offset + 16] =
                static_cast<unsigned char>(broken[central_offset + 16] ^ 0xFFu);
            const auto broken_path = guard_root / "bad-crc.zip";
            fs_utils::write_bytes(broken_path, broken.data(), broken.size());
            extract_zip(broken_path, guard_root / "out-crc");
        } catch (const std::exception&) {
            threw = true;
        }
        require(threw, "битый CRC в ZIP отвергается");
        threw = false;
        try {
            auto broken = bytes;
            // Смещение локального заголовка (+42) = 0xFFFFFFF0: раньше
            // сумма переполнялась по модулю 2^32 и проверка проходила.
            for (std::size_t i = 0; i < 4; ++i) {
                broken[central_offset + 42 + i] = 0xFFu;
            }
            broken[central_offset + 42] = 0xF0u;
            const auto broken_path = guard_root / "bad-offset.zip";
            fs_utils::write_bytes(broken_path, broken.data(), broken.size());
            extract_zip(broken_path, guard_root / "out-offset");
        } catch (const std::exception&) {
            threw = true;
        }
        require(threw, "переполненное смещение в ZIP отвергается");
        require(
            !std::filesystem::exists(
                guard_root / "out-offset" / "payload.txt"),
            "повреждённая запись не распаковывается на диск");
        std::filesystem::remove_all(guard_root);
    }

    {
        // Реальные пакеты *.argosmodel (DEFLATE от Python-ziplib). Проверка
        // включается переменной окружения TLING_TEST_ARGOS_MODEL со списком
        // путей через ';', чтобы ctest не зависел от больших файлов и сети.
        if (const char* model_env = std::getenv("TLING_TEST_ARGOS_MODEL")) {
            const std::string list(model_env);
            std::size_t start = 0;
            std::size_t index = 0;
            while (start <= list.size()) {
                const auto end = list.find(';', start);
                const std::string path = list.substr(
                    start,
                    end == std::string::npos ? std::string::npos : end - start);
                if (!path.empty()) {
                    const auto out_dir =
                        std::filesystem::temp_directory_path() /
                        ("offline-translator-argos-" + std::to_string(index));
                    std::filesystem::remove_all(out_dir);
                    extract_zip(path, out_dir);
                    std::size_t files = 0;
                    for (const auto& entry :
                         std::filesystem::recursive_directory_iterator(out_dir)) {
                        if (entry.is_regular_file()) {
                            ++files;
                        }
                    }
                    require(files > 0, "распаковка реального пакета Argos: " + path);
                    std::cout << "argos package: " << path << " -> " << files
                              << " files\n";
                    std::filesystem::remove_all(out_dir);
                    ++index;
                }
                if (end == std::string::npos) {
                    break;
                }
                start = end + 1;
            }
        }
    }

    {
        const auto settings_dir =
            std::filesystem::temp_directory_path() /
            "offline-translator-settings-test";
        std::filesystem::remove_all(settings_dir);
        std::filesystem::create_directories(settings_dir);
        const auto path = settings_dir / "settings.json";
        const auto missing = load_settings(settings_dir / "missing.json");
        require(missing.engine == "argos", "engine по умолчанию");
        require(missing.source_language == "en", "язык источника по умолчанию");
        // Автоопределение языка источника: «Авто» хранится в настройках
        // как обычный код и переживает сохранение.
        {
            auto auto_settings = missing;
            auto_settings.source_language = "auto";
            save_settings(path, auto_settings);
            require(
                load_settings(path).source_language == "auto",
                "source_language=auto сохраняется");
            // Определение языка по письму текста.
            require(
                detect_script_language("Привет, мир") == "ru",
                "автоопределение: русский");
            require(
                detect_script_language("Добрий день, світе") == "uk",
                "автоопределение: украинский");
            require(
                detect_script_language("Hello world") == "en",
                "автоопределение: английский");
            require(
                detect_script_language("你好世界") == "zh",
                "автоопределение: китайский");
            require(
                detect_script_language("こんにちは") == "ja",
                "автоопределение: японский");
            require(
                detect_script_language("안녕하세요") == "ko",
                "автоопределение: корейский");
            require(
                detect_script_language("مرحبا") == "ar",
                "автоопределение: арабский");
            require(
                detect_script_language("12345 !!!").empty(),
                "автоопределение: без букв пусто");
        }
        require(missing.target_language == "ru", "язык перевода по умолчанию");
        require(missing.ui_theme == "light", "тема по умолчанию light");
        require(!missing.auto_copy_selection, "автокопирование по умолчанию выкл");
        require(!missing.turbo_translation, "турбо перевод по умолчанию выкл");
        require(
            missing.clipboard_history_limit == 10,
            "история по умолчанию 10");
        fs_utils::write_text_file(
            path,
            "{\n  \"engine\": \"argos\",\n  \"architecture\": \"tiny\",\n"
            "  \"popup_requires_ctrl\": true\n}\n");
        auto loaded = load_settings(path);
        require(loaded.engine == "argos", "чтение engine из JSON");
        require(
            loaded.popup_modifier == "ctrl",
            "legacy popup_requires_ctrl → ctrl");
        loaded.engine = "nllb";
        loaded.source_language = "de";
        loaded.target_language = "fr";
        loaded.window_width = 640;
        loaded.window_height = 480;
        save_settings(path, loaded);
        const auto roundtrip = load_settings(path);
        require(roundtrip.engine == "nllb", "engine сохраняется");
        require(
            roundtrip.source_language == "de",
            "source_language сохраняется");
        require(
            roundtrip.target_language == "fr",
            "target_language сохраняется");
        require(roundtrip.window_width == 640, "window_width сохраняется");
        require(roundtrip.window_height == 480, "window_height сохраняется");
        std::string raw;
        {
            std::ifstream in(path);
            raw.assign(
                (std::istreambuf_iterator<char>(in)),
                std::istreambuf_iterator<char>());
        }
        require(
            raw.find("architecture") != std::string::npos,
            "ключ architecture Python сохраняется");
        require(
            raw.find("popup_requires_ctrl") != std::string::npos,
            "ключ popup_requires_ctrl сохраняется");
        require(
            raw.find("\"nllb\"") != std::string::npos,
            "в файле записан движок nllb");
        require(
            default_settings_path().filename() == "settings.json",
            "файл настроек называется settings.json");
        loaded.translate_hotkey = "Alt+F9";
        loaded.popup_requires_ctrl = true;
        loaded.popup_modifier = "alt";
        loaded.double_ctrl_c_translation = true;
        loaded.selection_popup_enabled = false;
        loaded.result_window_mode = std::string{kResultWindowSelectable};
        save_settings(path, loaded);
        const auto hotkey_roundtrip = load_settings(path);
        require(
            hotkey_roundtrip.translate_hotkey == "Alt+F9",
            "translate_hotkey сохраняется");
        require(
            hotkey_roundtrip.popup_modifier == "alt",
            "popup_modifier сохраняется");
        require(
            !hotkey_roundtrip.popup_requires_ctrl,
            "popup_requires_ctrl следует за modifier != ctrl");
        require(
            hotkey_roundtrip.double_ctrl_c_translation,
            "double_ctrl_c_translation читается");
        require(
            !hotkey_roundtrip.selection_popup_enabled,
            "selection_popup_enabled сохраняется");
        require(
            hotkey_roundtrip.result_window_mode == kResultWindowSelectable,
            "result_window_mode сохраняется");
        require(
            hotkey_roundtrip.ui_theme == "light",
            "ui_theme по умолчанию light");
        loaded.ui_theme = "dark";
        save_settings(path, loaded);
        const auto theme_roundtrip = load_settings(path);
        require(
            theme_roundtrip.ui_theme == "dark",
            "ui_theme сохраняется");
        require(
            normalize_ui_theme("DARK") == "light",
            "неизвестная тема становится light");
        require(
            normalize_ui_theme("dark") == "dark",
            "dark нормализуется");
        loaded.auto_copy_selection = true;
        loaded.clipboard_history_limit = 7;
        save_settings(path, loaded);
        const auto copy_roundtrip = load_settings(path);
        require(
            copy_roundtrip.auto_copy_selection,
            "auto_copy_selection сохраняется");
        loaded.turbo_translation = true;
        save_settings(path, loaded);
        require(
            load_settings(path).turbo_translation,
            "turbo_translation сохраняется");
        loaded.turbo_translation = false;
        save_settings(path, loaded);
        require(
            !load_settings(path).turbo_translation,
            "turbo_translation выключается обратно");
        require(
            copy_roundtrip.clipboard_history_limit == 7,
            "clipboard_history_limit сохраняется");
        loaded.clipboard_history_limit = 99;
        save_settings(path, loaded);
        require(
            load_settings(path).clipboard_history_limit == 50,
            "clipboard_history_limit ограничивается 50");
        {
            // Регрессия: битая горячая клавиша в settings.json больше не
            // сбрасывает все настройки (раньше stoi бросал исключение
            // наружу, и load_settings возвращал настройки по умолчанию).
            const auto broken_path = settings_dir / "broken-hotkey.json";
            fs_utils::write_text_file(
                broken_path,
                "{\"translate_hotkey\":\"F999999999\",\"ui_theme\":\"dark\","
                "\"engine\":\"firefox\"}");
            const auto broken_hotkey = load_settings(broken_path);
            require(
                broken_hotkey.ui_theme == "dark",
                "битый хоткей не сбрасывает тему");
            require(
                broken_hotkey.engine == "firefox",
                "битый хоткей не сбрасывает движок");
            std::filesystem::remove(broken_path);
        }
        {
            std::ifstream in(path);
            raw.assign(
                (std::istreambuf_iterator<char>(in)),
                std::istreambuf_iterator<char>());
        }
        require(
            raw.find("translate_hotkey") != std::string::npos,
            "ключ translate_hotkey записан");
        require(
            raw.find("architecture") != std::string::npos,
            "ключ architecture Python сохраняется после hotkey");
        std::filesystem::remove_all(settings_dir);
    }

    {
        const auto recover_dir =
            std::filesystem::temp_directory_path() /
            "offline-translator-settings-recover";
        std::filesystem::remove_all(recover_dir);
        std::filesystem::create_directories(recover_dir);
        const auto path = recover_dir / "settings.json";
        fs_utils::write_text_file(path, "{\n  \"engine\": \"nllb\",\n");
        auto recovered = load_settings(path);
        require(
            recovered.engine == "argos" && recovered.source_language == "en" &&
                recovered.target_language == "ru",
            "обрезанный JSON не падает и даёт значения по умолчанию");
        fs_utils::write_text_file(path, "это не json {");
        recovered = load_settings(path);
        require(
            recovered.engine == "argos" &&
                recovered.translate_hotkey == "Ctrl+Shift+T",
            "невалидный JSON не падает и даёт значения по умолчанию");
        fs_utils::write_text_file(path, "");
        recovered = load_settings(path);
        require(
            recovered.engine == "argos",
            "пустой файл настроек не падает");
        fs_utils::write_text_file(path, "[1, 2, 3]");
        recovered = load_settings(path);
        require(
            recovered.engine == "argos" && recovered.window_width == 0,
            "JSON-массив не падает и даёт значения по умолчанию");
        fs_utils::write_text_file(
            path,
            "{\n  \"engine\": 1,\n  \"source_language\": false,\n"
            "  \"window_width\": \"wide\",\n  \"popup_requires_ctrl\": \"yes\"\n}\n");
        recovered = load_settings(path);
        require(
            recovered.engine == "argos" && recovered.source_language == "en" &&
                recovered.window_width == 0 && !recovered.popup_requires_ctrl,
            "неверные типы полей JSON не падают и дают значения по умолчанию");
        std::filesystem::remove_all(recover_dir);
    }

    {
        const auto log_dir =
            std::filesystem::temp_directory_path() / "offline-translator-app-log";
        std::filesystem::remove_all(log_dir);
        std::filesystem::create_directories(log_dir);
        const auto path = log_dir / "app.log";
        require(!app_log_enabled(), "журнал выключен до init");
        init_app_log(path);
        require(app_log_enabled(), "журнал включён после init");
        require(app_log_path() == path, "путь журнала");
        app_log_info("тест журнала");
        app_log_warn("предупреждение");
        std::string raw;
        {
            std::ifstream in(path);
            raw.assign(
                (std::istreambuf_iterator<char>(in)),
                std::istreambuf_iterator<char>());
        }
        require(raw.find("[info] тест журнала") != std::string::npos, "info пишется");
        require(
            raw.find("[warn] предупреждение") != std::string::npos,
            "warn пишется");
        shutdown_app_log();
        require(!app_log_enabled(), "журнал выключается");
        std::filesystem::remove_all(log_dir);
    }

    {
        const auto parsed = parse_hotkey("Ctrl+Shift+T");
        require(parsed.has_value(), "разбор Ctrl+Shift+T");
        require(parsed->control && parsed->shift && !parsed->alt, "модификаторы");
        require(parsed->vk == static_cast<unsigned>('T'), "клавиша T");
        require(
            format_hotkey(*parsed) == "Ctrl+Shift+T",
            "каноническая запись Ctrl+Shift+T");
        require(
            hotkey_win32_modifiers(*parsed) == (0x0002u | 0x0004u),
            "маска MOD_CONTROL|MOD_SHIFT");
        const auto f9 = parse_hotkey("alt+f9");
        require(f9.has_value() && f9->alt && f9->vk == 0x78u, "Alt+F9");
        require(!parse_hotkey("Ctrl+").has_value(), "неполная комбинация");
        require(!parse_hotkey("").has_value(), "пустая комбинация");
        // Регрессия: std::stoi бросал std::out_of_range на «F999999999»,
        // а значение приходит из settings.json и сбрасывало все настройки.
        require(
            !parse_hotkey("F999999999").has_value(),
            "слишком длинный номер F-клавиши отвергается");
        require(
            !parse_hotkey("F0").has_value(),
            "F0 не является клавишей");
        const auto f24 = parse_hotkey("Ctrl+F24");
        require(
            f24.has_value() && f24->vk == 0x87u,
            "верхняя граница F24 разбирается");
    }

    {
        require(
            effect_for(UiCommand::window_close) == UiEffect::hide_to_tray,
            "крестик скрывает в трей");
        require(
            effect_for(UiCommand::tray_open) == UiEffect::restore_window,
            "Открыть восстанавливает окно");
        require(
            effect_for(UiCommand::tray_exit) == UiEffect::destroy_and_quit,
            "Выход завершает процесс");
        require(
            effect_for(UiCommand::start_minimized) ==
                UiEffect::start_hidden_in_tray,
            "--minimized стартует скрытым");
        require(
            effect_for(UiCommand::smoke_start) ==
                UiEffect::run_smoke_then_destroy,
            "smoke не прячет окно навсегда");
        require(!smoke_hides_forever(), "smoke_hides_forever == false");
        require(
            !smoke_may_enable_autostart(),
            "smoke не включает автозагрузку");
        require(tray_menu_has(TrayMenuItem::open), "меню трея: Открыть");
        require(tray_menu_has(TrayMenuItem::exit), "меню трея: Выход");
    }

    {
        require(should_show_selection_button(false, false), "кнопка без Ctrl");
        require(should_show_selection_button(true, true), "кнопка с Ctrl");
        require(
            !should_show_selection_button(true, false),
            "кнопка скрыта без Ctrl");
        require(
            should_show_selection_button("none", false, false, false),
            "кнопка без модификатора");
        require(
            should_show_selection_button("alt", false, true, false),
            "кнопка при Alt");
        require(
            !should_show_selection_button("alt", true, false, false),
            "кнопка скрыта без Alt");
        require(
            should_show_selection_button("shift", false, false, true),
            "кнопка при Shift");
        require(
            should_skip_selection_copy(true, false),
            "пропуск копирования при C");
        require(
            should_skip_selection_copy(false, true),
            "пропуск копирования при V");
        require(
            !should_skip_selection_copy(false, false),
            "захват без C/V разрешён");
        require(normalize_popup_modifier("ALT") == "none", "неизвестный modifier");
        require(normalize_popup_modifier("ctrl") == "ctrl", "ctrl нормализуется");
        require(
            popup_modifier_from_legacy(true) == "ctrl",
            "legacy true → ctrl");
        require(
            should_trigger_double_ctrl_c(10.0, 10.5, true, true),
            "Ctrl+C+C в окне 0.7с");
        require(
            !should_trigger_double_ctrl_c(10.0, 10.8, true, true),
            "слишком долгий интервал");
        require(
            should_capture_selection(true, false, 30, 0.25, false),
            "жест выделения");
        require(
            !should_capture_selection(false, false, 80, 0.5, false),
            "не клиентская область");
        const auto hello = choose_selection_direction("Hello world");
        require(hello.first == "en" && hello.second == "ru", "en→ru");
        const auto russian = choose_selection_direction(
            "Привет мир, это проверка");
        require(russian.first == "ru" && russian.second == "en", "ru→en");
    }

    {
        // Неудачный захват с keep_in_clipboard не оставляет служебную
        // метку и возвращает прежний буфер.
        MemoryClipboard clipboard;
        clipboard.set_text(L"previous text");
        const auto failed = capture_selected_text(
            clipboard,
            []() {},
            L"__sentinel__",
            true);
        require(failed.empty(), "неудачный захват возвращает пусто");
        require(
            clipboard.get_text() != L"__sentinel__",
            "служебная метка не остаётся в буфере");
        require(
            clipboard.get_text() == L"previous text",
            "прежний буфер восстановлен после неудачи");
    }

    {
        MemoryClipboard clipboard;
        clipboard.set_text(L"user clipboard");
        const auto captured = capture_selected_text(
            clipboard,
            [&clipboard]() { clipboard.set_text(L"selected text"); },
            L"__ot_sel_test__");
        require(captured == L"selected text", "захват выделения");
        require(
            clipboard.get_text() == L"user clipboard",
            "буфер пользователя восстановлен");
        clipboard.set_text(L"user clipboard");
        const auto kept = capture_selected_text(
            clipboard,
            [&clipboard]() { clipboard.set_text(L"keep selected"); },
            L"__ot_sel_test__",
            true);
        require(kept == L"keep selected", "захват с автокопированием");
        require(
            clipboard.get_text() == L"keep selected",
            "автокопирование оставляет выделение в буфере");
        clipboard.set_text(L"user clipboard");
        const auto empty = capture_selected_text(
            clipboard,
            []() {},
            L"__ot_sel_test__");
        require(empty.empty(), "пустое выделение");
        require(
            clipboard.get_text() == L"user clipboard",
            "буфер восстановлен при пустом выделении");
        ClipboardRestorer restorer(clipboard);
        clipboard.set_text(L"temporary");
        require(clipboard.get_text() == L"temporary", "временная запись");
        // restorer восстановит при выходе из блока
    }
    {
        MemoryClipboard clipboard;
        clipboard.set_text(L"keep me");
        {
            ClipboardRestorer restorer(clipboard);
            clipboard.set_text(L"overwrite");
        }
        require(
            clipboard.get_text() == L"keep me",
            "RAII восстанавливает буфер");
    }
    {
        MemoryClipboard clipboard;
        clipboard.set_text(L"keep me");
        {
            ClipboardRestorer restorer(clipboard);
            clipboard.set_text(L"user copy");
            restorer.disarm();
        }
        require(
            clipboard.get_text() == L"user copy",
            "disarm не затирает буфер пользователя");
    }

    {
        const auto history_dir =
            std::filesystem::temp_directory_path() /
            "offline-translator-history-test";
        std::filesystem::remove_all(history_dir);
        std::filesystem::create_directories(history_dir);
        const auto path = history_dir / "clipboard_history.json";
        require(
            normalize_clipboard_history_limit(0) == 1,
            "нижняя граница истории");
        require(
            normalize_clipboard_history_limit(99) == 50,
            "верхняя граница истории");
        add_clipboard_history_item(path, "один", 3);
        add_clipboard_history_item(path, "два", 3);
        add_clipboard_history_item(path, "три", 3);
        add_clipboard_history_item(path, "четыре", 3);
        auto items = load_clipboard_history(path);
        require(items.size() == 3, "история обрезается по лимиту");
        require(items[0] == "четыре" && items[2] == "два", "новые сверху");
        add_clipboard_history_item(path, "два", 3);
        items = load_clipboard_history(path);
        require(items[0] == "два", "повтор поднимается вверх");
        require(
            clipboard_history_preview("  a\n b  ") == "a b",
            "превью сжимает пробелы");
        clear_clipboard_history(path);
        require(load_clipboard_history(path).empty(), "очистка истории");
        std::filesystem::remove_all(history_dir);
    }

    {
        require(
            parse_version("v0.995-beta") == parse_version("0.995-beta"),
            "v и без v совпадают");
        require(
            parse_version("beta 0.995") == parse_version("0.995-beta"),
            "подпись beta 0.995 совпадает с тегом");
        require(
            version_is_newer("0.996-beta", "0.995-beta"),
            "0.996-beta новее");
        require(
            version_is_newer("0.995", "0.995-beta"),
            "релиз новее beta");
        require(
            !version_is_newer("0.995-beta", "0.995-beta"),
            "та же версия не новее");
        GithubRelease release;
        release.tag = "v1.0.0";
        release.assets = {
            {"offline-translator-1.0.0-src.zip", "http://x/src", 1},
            {"offline-translator-1.0.0-portable-with-models.zip", "http://x/full", 2},
            {"offline-translator-1.0.0-portable.zip", "http://x/port", 3},
            {"offline-translator-1.0.0-setup.exe", "http://x/setup", 4},
        };
        const auto* portable = choose_asset(release, InstallKind::portable);
        require(
            portable && portable->name.find("portable.zip") != std::string::npos,
            "портатив без моделей");
        const auto* setup = choose_asset(release, InstallKind::installer);
        require(
            setup && setup->name.find("setup.exe") != std::string::npos,
            "установщик setup.exe");
        const auto parsed = parse_github_releases(
            R"([{"tag_name":"v0.1.0","draft":false,"assets":[]},
                {"tag_name":"v0.2.0","draft":true,"assets":[]}])");
        require(parsed.size() == 1 && parsed[0].tag == "v0.1.0", "черновик пропускается");
        {
            const auto extract_dir =
                std::filesystem::temp_directory_path() / "ot-update-extract-test";
            std::filesystem::remove_all(extract_dir);
            const auto payload_dir = extract_dir / "payload";
            std::filesystem::create_directories(payload_dir);
            const auto exe_path = payload_dir / "TLing.exe";
            {
                std::ofstream out(exe_path, std::ios::binary);
                out << "exe";
            }
            const auto zip_path = extract_dir / "portable.zip";
            write_store_zip(zip_path, {{"TLing.exe", exe_path}});
            const auto unpacked = extract_portable_payload(
                zip_path,
                "TLing.exe");
            require(
                std::filesystem::is_regular_file(unpacked / "TLing.exe"),
                "распаковка находит exe");
            std::filesystem::remove_all(extract_dir);
        }
    }

#ifdef _WIN32
    {
        const auto real_before = get_run_value(kAutostartValueName);
        struct TestAutostartGuard {
            ~TestAutostartGuard() {
                try {
                    delete_run_value(kAutostartTestValueName);
                } catch (...) {
                }
            }
        } guard;
        delete_run_value(kAutostartTestValueName);
        const std::wstring command =
            autostart_command_for_exe(
                std::filesystem::path(L"C:\\fake\\TLing.exe"));
        require(
            command.find(L"--minimized") != std::wstring::npos,
            "команда автозапуска содержит --minimized");
        require(
            command.find(L"TLing.exe") != std::wstring::npos,
            "команда автозапуска содержит exe");
        set_autostart(true, kAutostartTestValueName, command);
        require(
            is_autostart_enabled(kAutostartTestValueName),
            "тестовая автозагрузка включена");
        const auto stored = get_run_value(kAutostartTestValueName);
        require(stored.has_value() && *stored == command, "значение Run совпадает");
        set_autostart(false, kAutostartTestValueName, command);
        require(
            !is_autostart_enabled(kAutostartTestValueName),
            "тестовая автозагрузка выключена");
        delete_run_value(kAutostartTestValueName);
        const auto real_after = get_run_value(kAutostartValueName);
        require(
            real_before == real_after,
            "тест не должен менять значение TLing");
    }
#endif

    {
        // Язык интерфейса: нормализация, определение и перевод строк.
        const auto& options = ui_language_options();
        require(options.size() == 5, "пять языков интерфейса");
        std::set<std::string> codes;
        for (const auto& option : options) {
            codes.insert(option.code);
        }
        for (const auto expected : {"ru", "en", "de", "fr", "es"}) {
            require(
                codes.count(expected) == 1,
                std::string("язык интерфейса доступен: ") + expected);
        }
        require(
            codes.count("uk") == 0,
            "украинский больше не поддерживается");
        require(
            normalize_ui_language("auto") == detect_ui_language(),
            "auto нормализуется в язык системы");
        require(
            normalize_ui_language("") == detect_ui_language(),
            "пустой код — язык системы");
        require(normalize_ui_language("de") == "de", "известный код сохраняется");
        require(
            codes.count(normalize_ui_language("zz")) == 1,
            "неизвестный код заменяется поддерживаемым");

        set_ui_language("ru");
        require(tr(L"Настройки") == L"Настройки", "русский — исходная строка");
        set_ui_language("en");
        require(tr(L"Настройки") == L"Settings", "английский перевод настроек");
        require(tr(L"Отмена") == L"Cancel", "английский перевод отмены");
        require(
            tr(L"Неизвестная строка") == L"Неизвестная строка",
            "неизвестный ключ не переводится");
        set_ui_language("de");
        require(tr(L"Настройки") == L"Einstellungen", "немецкий перевод");
        set_ui_language("uk");
        require(
            codes.count(current_ui_language()) == 1,
            "украинский код заменяется поддерживаемым языком");
        set_ui_language("ru");
        require(tr(L"Сохранить") == L"Сохранить", "возврат к русскому");
    }

    const auto user_nllb = NllbModelManager::default_models_root();
    NllbModelManager real_nllb(user_nllb);
    require(
        real_nllb.is_installed(),
        "Python-установленная NLLB должна определяться как установленная: " +
            real_nllb.model_path().string());
    std::cout << "user NLLB: installed\n";
    const auto user_argos = ArgosModelManager::default_packages_root();
    ArgosModelManager real_argos(user_argos, "en", "ru");
    require(
        real_argos.is_installed(),
        "Python-установленный Argos en→ru должен определяться как установленный: " +
            real_argos.package_path().string());
    std::cout << "user Argos en-ru: installed\n";

    std::filesystem::remove_all(nllb_root);
    std::filesystem::remove_all(argos_mgmt_root);
    std::filesystem::remove_all(argos_index_root);

    std::cout << "core_tests: ok\n";
    return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
