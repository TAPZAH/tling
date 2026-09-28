#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace offline_translator {

// Совпадает с Python: progress_callback(downloaded, total, message).
using ProgressCallback = std::function<void(
    std::uint64_t downloaded,
    std::uint64_t total,
    std::string_view message)>;

struct PackageInfo {
    std::string from_code;
    std::string to_code;
    std::string from_name;
    std::string to_name;
    std::string dirname;
    std::string architecture;
    std::string download_url;
    std::vector<std::string> download_urls;
};

}  // пространство имён offline_translator
