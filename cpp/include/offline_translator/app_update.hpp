#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace offline_translator {

inline constexpr std::string_view kGithubOwner = "TAPZAH";
inline constexpr std::string_view kGithubRepo = "tling";
inline constexpr std::string_view kGithubReleasesApi =
    "https://api.github.com/repos/TAPZAH/tling/releases";
inline constexpr std::string_view kGithubReleasesPage =
    "https://github.com/TAPZAH/tling/releases";

enum class InstallKind { installer, portable, source };

struct ReleaseAsset {
    std::string name;
    std::string url;
    int size{0};
};

struct GithubRelease {
    std::string tag;
    std::string title;
    std::string notes;
    std::string html_url;
    std::vector<ReleaseAsset> assets;
};

struct UpdateInfo {
    GithubRelease release;
    std::optional<ReleaseAsset> asset;
    InstallKind install_kind{InstallKind::portable};
};

struct VersionKey {
    int numbers[3]{0, 0, 0};
    int release_rank{1};
    std::string pre;

    bool operator>(const VersionKey& other) const;
    bool operator==(const VersionKey& other) const;
};

VersionKey parse_version(std::string_view spec);
bool version_is_newer(std::string_view remote, std::string_view local);
InstallKind detect_install_kind(const std::filesystem::path& app_dir);
const ReleaseAsset* choose_asset(
    const GithubRelease& release,
    InstallKind install_kind);
std::vector<GithubRelease> parse_github_releases(std::string_view json_text);
std::optional<UpdateInfo> check_for_update(
    std::string_view local_version,
    const std::filesystem::path& app_dir);
void download_update_asset(
    const ReleaseAsset& asset,
    const std::filesystem::path& destination);
std::filesystem::path extract_portable_payload(
    const std::filesystem::path& archive,
    std::string_view exe_name);
enum class ApplyUpdateAction { keep, quit };
ApplyUpdateAction apply_downloaded_update(
    const std::filesystem::path& path,
    InstallKind install_kind,
    const std::filesystem::path& app_dir,
    const std::filesystem::path& current_exe);

}  // пространство имён offline_translator
