#include "offline_translator/app_update.hpp"
#include "offline_translator/app_version.hpp"

#include "file_transfer.hpp"
#include "fs_utils.hpp"
#include "zip_archive.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace offline_translator {
namespace {

std::string to_lower_copy(std::string text) {
    for (char& ch : text) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return text;
}

bool is_setup_asset(std::string_view name) {
    const auto lower = to_lower_copy(std::string{name});
    return lower.size() >= 4 &&
        lower.compare(lower.size() - 4, 4, ".exe") == 0 &&
        lower.find("setup") != std::string::npos;
}

bool is_portable_asset(std::string_view name) {
    const auto lower = to_lower_copy(std::string{name});
    if (lower.size() < 4 || lower.compare(lower.size() - 4, 4, ".zip") != 0) {
        return false;
    }
    if (lower.find("portable") == std::string::npos) {
        return false;
    }
    if (lower.find("with-models") != std::string::npos ||
        lower.find("-src") != std::string::npos) {
        return false;
    }
    return true;
}

int compare_numbers(const VersionKey& left, const VersionKey& right) {
    for (int index = 0; index < 3; ++index) {
        if (left.numbers[index] != right.numbers[index]) {
            return left.numbers[index] < right.numbers[index] ? -1 : 1;
        }
    }
    if (left.release_rank != right.release_rank) {
        return left.release_rank < right.release_rank ? -1 : 1;
    }
    if (left.pre != right.pre) {
        return left.pre < right.pre ? -1 : 1;
    }
    return 0;
}

}  // анонимное пространство имён

bool VersionKey::operator>(const VersionKey& other) const {
    return compare_numbers(*this, other) > 0;
}

bool VersionKey::operator==(const VersionKey& other) const {
    return compare_numbers(*this, other) == 0;
}

VersionKey parse_version(std::string_view spec) {
    std::string text{spec};
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
        text.erase(text.begin());
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) {
        text.pop_back();
    }
    auto lowered = to_lower_copy(text);
    if (lowered.size() > 1 && lowered.front() == 'v' &&
        std::isdigit(static_cast<unsigned char>(lowered[1]))) {
        lowered.erase(lowered.begin());
        text.erase(text.begin());
    }
    VersionKey key;
    std::string pre;
    if (lowered.rfind("beta ", 0) == 0) {
        lowered.erase(0, 5);
        pre = "beta";
    } else if (lowered.rfind("beta-", 0) == 0) {
        lowered.erase(0, 5);
        pre = "beta";
    }
    const auto dash = lowered.find('-');
    std::string main = lowered;
    if (dash != std::string::npos) {
        main = lowered.substr(0, dash);
        pre = lowered.substr(dash + 1);
    }
    std::stringstream stream(main);
    std::string part;
    int index = 0;
    while (std::getline(stream, part, '.') && index < 3) {
        try {
            key.numbers[index] = std::stoi(part);
        } catch (...) {
            key.numbers[index] = 0;
        }
        ++index;
    }
    key.release_rank = pre.empty() ? 1 : 0;
    key.pre = pre;
    return key;
}

bool version_is_newer(std::string_view remote, std::string_view local) {
    return parse_version(remote) > parse_version(local);
}

InstallKind detect_install_kind(const std::filesystem::path& app_dir) {
    try {
        if (std::filesystem::is_regular_file(app_dir / "unins000.exe") ||
            std::filesystem::is_regular_file(app_dir / "unins001.exe")) {
            return InstallKind::installer;
        }
    } catch (const std::filesystem::filesystem_error&) {
    }
    return InstallKind::portable;
}

const ReleaseAsset* choose_asset(
    const GithubRelease& release,
    InstallKind install_kind) {
    const ReleaseAsset* setup = nullptr;
    const ReleaseAsset* portable = nullptr;
    for (const auto& asset : release.assets) {
        if (setup == nullptr && is_setup_asset(asset.name)) {
            setup = &asset;
        }
        if (portable == nullptr && is_portable_asset(asset.name)) {
            portable = &asset;
        }
    }
    if (install_kind == InstallKind::portable && portable != nullptr) {
        return portable;
    }
    if (setup != nullptr) {
        return setup;
    }
    return portable;
}

std::vector<GithubRelease> parse_github_releases(std::string_view json_text) {
    nlohmann::json parsed = nlohmann::json::parse(json_text, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_array()) {
        throw std::runtime_error("Неожиданный ответ GitHub");
    }
    std::vector<GithubRelease> releases;
    for (const auto& item : parsed) {
        if (!item.is_object() || item.value("draft", false)) {
            continue;
        }
        GithubRelease release;
        release.tag = item.value("tag_name", "");
        if (release.tag.empty()) {
            continue;
        }
        release.title = item.value("name", release.tag);
        release.notes = item.value("body", "");
        release.html_url = item.value("html_url", std::string{kGithubReleasesPage});
        if (item.contains("assets") && item["assets"].is_array()) {
            for (const auto& raw_asset : item["assets"]) {
                if (!raw_asset.is_object()) {
                    continue;
                }
                ReleaseAsset asset;
                asset.name = raw_asset.value("name", "");
                asset.url = raw_asset.value("browser_download_url", "");
                asset.size = raw_asset.value("size", 0);
                if (asset.name.empty() || asset.url.empty()) {
                    continue;
                }
                release.assets.push_back(std::move(asset));
            }
        }
        releases.push_back(std::move(release));
    }
    std::sort(
        releases.begin(),
        releases.end(),
        [](const GithubRelease& left, const GithubRelease& right) {
            return parse_version(left.tag) > parse_version(right.tag);
        });
    return releases;
}

std::optional<UpdateInfo> check_for_update(
    std::string_view local_version,
    const std::filesystem::path& app_dir) {
    const auto json_path =
        std::filesystem::temp_directory_path() / "ot-github-releases.json";
    // Ответ GitHub должен быть свежим: старый файл из прошлой проверки
    // нельзя переиспользовать, иначе приложение навсегда увидит один и тот
    // же список релизов. download_resumable пропускает существующий файл,
    // поэтому удаляем и ответ, и его .part.
    std::error_code cache_error;
    std::filesystem::remove(json_path, cache_error);
    std::filesystem::remove(
        std::filesystem::path(json_path).concat(".part"), cache_error);
    download_resumable(
        {std::string{kGithubReleasesApi}},
        json_path,
        {},
        "Проверяю обновления GitHub");
    std::ifstream in(json_path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("Не удалось прочитать ответ GitHub");
    }
    std::string text(
        (std::istreambuf_iterator<char>(in)),
        std::istreambuf_iterator<char>());
    auto releases = parse_github_releases(text);
    if (releases.empty()) {
        throw std::runtime_error("На GitHub пока нет опубликованных релизов");
    }
    if (!version_is_newer(releases.front().tag, local_version)) {
        return std::nullopt;
    }
    UpdateInfo info;
    info.release = std::move(releases.front());
    info.install_kind = detect_install_kind(app_dir);
    if (const auto* asset = choose_asset(info.release, info.install_kind)) {
        info.asset = *asset;
    }
    return info;
}

void download_update_asset(
    const ReleaseAsset& asset,
    const std::filesystem::path& destination) {
    const auto parent = destination.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }
    download_resumable(
        {asset.url},
        destination,
        {},
        "Скачиваю " + asset.name);
}

namespace {

std::string path_to_utf8(const std::filesystem::path& path) {
    const auto utf8 = path.u8string();
    return std::string(
        reinterpret_cast<const char*>(utf8.c_str()),
        utf8.size());
}

bool file_name_equals(const std::filesystem::path& path, std::string_view name) {
    const auto left = to_lower_copy(path.filename().string());
    const auto right = to_lower_copy(std::string{name});
    return left == right;
}

bool directory_has_exe(
    const std::filesystem::path& directory,
    std::string_view exe_name) {
    try {
        if (!std::filesystem::is_directory(directory)) {
            return false;
        }
        for (const auto& entry : std::filesystem::directory_iterator(directory)) {
            if (entry.is_regular_file() &&
                (file_name_equals(entry.path(), exe_name) ||
                 file_name_equals(entry.path(), "OfflineTranslator.exe"))) {
                return true;
            }
        }
    } catch (const std::filesystem::filesystem_error&) {
        return false;
    }
    return false;
}

#ifdef _WIN32
void start_detached_process(
    const wchar_t* application,
    std::wstring command_line,
    const wchar_t* current_directory) {
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(
            application,
            command_line.empty() ? nullptr : command_line.data(),
            nullptr,
            nullptr,
            FALSE,
            DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP,
            nullptr,
            current_directory,
            &startup,
            &process)) {
        throw std::runtime_error("Не удалось запустить обновление");
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
}

void write_portable_replacer_script(
    const std::filesystem::path& script_path,
    const std::filesystem::path& staging,
    const std::filesystem::path& app_dir,
    const std::filesystem::path& restart_exe,
    DWORD pid) {
    const std::string staging_text = path_to_utf8(staging);
    const std::string app_text = path_to_utf8(app_dir);
    const std::string exe_text = path_to_utf8(restart_exe);
    std::ostringstream body;
    body << "@echo off\r\n"
         << "chcp 65001 >nul\r\n"
         << ":wait\r\n"
         << "tasklist /FI \"PID eq " << pid << "\" | find \"" << pid << "\" >nul\r\n"
         << "if not errorlevel 1 (\r\n"
         << "  timeout /t 1 /nobreak >nul\r\n"
         << "  goto wait\r\n"
         << ")\r\n"
         << "robocopy \"" << staging_text << "\" \"" << app_text
         << "\" /E /XD data /NFL /NDL /NJH /NJS /NC /NS /NP\r\n"
         << "if exist \"" << exe_text << "\" start \"\" \"" << exe_text << "\"\r\n"
         << "rmdir /s /q \"" << staging_text << "\"\r\n"
         << "del \"%~f0\"\r\n";
    const std::string text = body.str();
    std::ofstream out(script_path, std::ios::binary);
    if (!out) {
        throw std::runtime_error("Не удалось записать скрипт обновления");
    }
    const unsigned char bom[] = {0xEF, 0xBB, 0xBF};
    out.write(reinterpret_cast<const char*>(bom), 3);
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
}
#endif

}  // анонимное пространство имён

std::filesystem::path extract_portable_payload(
    const std::filesystem::path& archive,
    std::string_view exe_name) {
    const auto extract_root =
        std::filesystem::temp_directory_path() /
        ("ot-update-" + std::to_string(
#ifdef _WIN32
             GetCurrentProcessId()
#else
             0
#endif
             ));
    std::error_code error;
    std::filesystem::remove_all(extract_root, error);
    std::filesystem::create_directories(extract_root);
    extract_zip(archive, extract_root);
    if (directory_has_exe(extract_root, exe_name)) {
        return extract_root;
    }
    try {
        for (const auto& entry : std::filesystem::directory_iterator(extract_root)) {
            if (entry.is_directory() && directory_has_exe(entry.path(), exe_name)) {
                return entry.path();
            }
        }
        for (const auto& entry :
             std::filesystem::recursive_directory_iterator(extract_root)) {
            if (!entry.is_regular_file()) {
                continue;
            }
            if (file_name_equals(entry.path(), exe_name) ||
                file_name_equals(entry.path(), "OfflineTranslator.exe")) {
                return entry.path().parent_path();
            }
        }
    } catch (const std::filesystem::filesystem_error& caught) {
        throw std::runtime_error(caught.what());
    }
    throw std::runtime_error("В архиве обновления нет OfflineTranslator.exe");
}

ApplyUpdateAction apply_downloaded_update(
    const std::filesystem::path& path,
    InstallKind install_kind,
    const std::filesystem::path& app_dir,
    const std::filesystem::path& current_exe) {
    const auto lower = to_lower_copy(path.filename().string());
    if (lower.size() >= 4 && lower.compare(lower.size() - 4, 4, ".exe") == 0) {
#ifdef _WIN32
        start_detached_process(path.c_str(), {}, nullptr);
#else
        (void)app_dir;
        (void)current_exe;
        throw std::runtime_error("Установка обновления доступна только в Windows");
#endif
        return install_kind == InstallKind::source
            ? ApplyUpdateAction::keep
            : ApplyUpdateAction::quit;
    }
    if (lower.size() < 4 || lower.compare(lower.size() - 4, 4, ".zip") != 0) {
        throw std::runtime_error("Неизвестный файл обновления: " + path.filename().string());
    }
    if (install_kind == InstallKind::source) {
        throw std::runtime_error(
            "Портативный архив ставится поверх exe-сборки, а не исходников.");
    }
#ifdef _WIN32
    const auto staging = extract_portable_payload(
        path,
        current_exe.filename().string());
    std::filesystem::path restart = app_dir / current_exe.filename();
    if (!std::filesystem::is_regular_file(staging / current_exe.filename()) &&
        std::filesystem::is_regular_file(staging / "OfflineTranslator.exe")) {
        restart = app_dir / "OfflineTranslator.exe";
    }
    const auto script =
        std::filesystem::temp_directory_path() /
        "offline-translator-apply-update.cmd";
    write_portable_replacer_script(
        script,
        staging,
        app_dir,
        restart,
        GetCurrentProcessId());
    std::wstring command = L"cmd.exe /c \"" + script.wstring() + L"\"";
    start_detached_process(nullptr, std::move(command), nullptr);
    return ApplyUpdateAction::quit;
#else
    throw std::runtime_error("Установка обновления доступна только в Windows");
#endif
}

}  // пространство имён offline_translator
