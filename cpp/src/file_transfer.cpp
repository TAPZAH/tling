#include "file_transfer.hpp"
#include "fs_utils.hpp"
#include "offline_translator/app_log.hpp"

#include <algorithm>
#include <cwchar>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>
#endif

namespace offline_translator {
namespace {

constexpr std::size_t kChunkSize = 64 * 1024;
constexpr std::uint64_t kProgressStep = 1024 * 1024;
constexpr wchar_t kUserAgent[] =
    L"offline-translator/0.99.5-beta (cpp-model-management)";

void report_progress(
    const ProgressCallback& progress,
    std::uint64_t downloaded,
    std::uint64_t total,
    std::string_view message) {
    if (progress) {
        progress(downloaded, total == 0 ? downloaded : total, message);
    }
}

bool is_http_url(std::string_view url) {
    return url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0;
}

void copy_local_resumable(
    const std::filesystem::path& source,
    const std::filesystem::path& part_path,
    std::uintmax_t existing,
    const ProgressCallback& progress,
    std::string_view message) {
    const auto total = fs_utils::file_size_or_zero(source);
    if (total == 0) {
        throw std::runtime_error(
            "Пустой локальный источник: " + source.string());
    }
    if (existing > total) {
        existing = 0;
        std::filesystem::remove(part_path);
    }

    std::ifstream in(source, std::ios::binary);
    if (!in) {
        throw std::runtime_error("Не удалось открыть источник: " + source.string());
    }
    in.seekg(static_cast<std::streamoff>(existing), std::ios::beg);

    std::ofstream out(
        part_path,
        existing ? (std::ios::binary | std::ios::app)
                 : (std::ios::binary | std::ios::trunc));
    if (!out) {
        throw std::runtime_error(
            "Не удалось открыть временный файл: " + part_path.string());
    }

    std::uint64_t downloaded = existing;
    std::uint64_t last_report =
        downloaded > kProgressStep ? downloaded - kProgressStep : 0;
    std::vector<char> buffer(kChunkSize);
    while (in) {
        in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto got = in.gcount();
        if (got <= 0) {
            break;
        }
        out.write(buffer.data(), got);
        downloaded += static_cast<std::uint64_t>(got);
        if (progress &&
            (downloaded - last_report >= kProgressStep || downloaded >= total)) {
            last_report = downloaded;
            report_progress(progress, downloaded, total, message);
        }
    }
    if (!out) {
        throw std::runtime_error(
            "Ошибка записи временного файла: " + part_path.string());
    }
    if (downloaded < total) {
        throw std::runtime_error(
            "Локальная копия оборвалась: " + source.string());
    }
}

#ifdef _WIN32

std::wstring utf8_to_wide(const std::string& text) {
    if (text.empty()) {
        return {};
    }
    const int needed = MultiByteToWideChar(
        CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    if (needed <= 1) {
        throw std::runtime_error("Не удалось преобразовать URL в UTF-16");
    }
    std::wstring wide(static_cast<std::size_t>(needed - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, wide.data(), needed);
    return wide;
}

class WinHttpHandle {
public:
    WinHttpHandle() = default;
    explicit WinHttpHandle(HINTERNET handle) : handle_(handle) {}
    ~WinHttpHandle() { reset(); }

    WinHttpHandle(const WinHttpHandle&) = delete;
    WinHttpHandle& operator=(const WinHttpHandle&) = delete;

    WinHttpHandle(WinHttpHandle&& other) noexcept : handle_(other.handle_) {
        other.handle_ = nullptr;
    }
    WinHttpHandle& operator=(WinHttpHandle&& other) noexcept {
        if (this != &other) {
            reset();
            handle_ = other.handle_;
            other.handle_ = nullptr;
        }
        return *this;
    }

    HINTERNET get() const { return handle_; }
    explicit operator bool() const { return handle_ != nullptr; }

    void reset() {
        if (handle_ != nullptr) {
            WinHttpCloseHandle(handle_);
            handle_ = nullptr;
        }
    }

private:
    HINTERNET handle_{nullptr};
};

std::string last_winhttp_error() {
    return std::to_string(GetLastError());
}

void download_http_resumable(
    const std::string& url,
    const std::filesystem::path& part_path,
    std::uintmax_t existing,
    const ProgressCallback& progress,
    std::string_view message) {
    const auto wide_url = utf8_to_wide(url);
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    wchar_t scheme[16]{};
    wchar_t host[256]{};
    wchar_t path[2048]{};
    wchar_t extra[512]{};
    parts.lpszScheme = scheme;
    parts.dwSchemeLength = static_cast<DWORD>(std::size(scheme));
    parts.lpszHostName = host;
    parts.dwHostNameLength = static_cast<DWORD>(std::size(host));
    parts.lpszUrlPath = path;
    parts.dwUrlPathLength = static_cast<DWORD>(std::size(path));
    parts.lpszExtraInfo = extra;
    parts.dwExtraInfoLength = static_cast<DWORD>(std::size(extra));
    if (!WinHttpCrackUrl(wide_url.c_str(), 0, 0, &parts)) {
        throw std::runtime_error("Некорректный URL: " + url);
    }

    WinHttpHandle session(WinHttpOpen(
        kUserAgent,
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0));
    if (!session) {
        throw std::runtime_error(
            "WinHttpOpen не удался, код " + last_winhttp_error());
    }
    DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
#ifdef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
    protocols |= WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
#endif
    WinHttpSetOption(
        session.get(),
        WINHTTP_OPTION_SECURE_PROTOCOLS,
        &protocols,
        sizeof(protocols));
    WinHttpSetTimeouts(session.get(), 15000, 15000, 30000, 120000);

    WinHttpHandle connection(WinHttpConnect(
        session.get(),
        parts.lpszHostName,
        parts.nPort,
        0));
    if (!connection) {
        throw std::runtime_error(
            "WinHttpConnect не удался, код " + last_winhttp_error());
    }

    std::wstring object = parts.lpszUrlPath;
    if (parts.dwExtraInfoLength > 0 && parts.lpszExtraInfo != nullptr) {
        object += parts.lpszExtraInfo;
    }
    const DWORD flags =
        parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
    WinHttpHandle request(WinHttpOpenRequest(
        connection.get(),
        L"GET",
        object.c_str(),
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        flags));
    if (!request) {
        throw std::runtime_error(
            "WinHttpOpenRequest не удался, код " + last_winhttp_error());
    }

    std::wstring headers = L"Accept: */*\r\n";
    const std::wstring hostname(parts.lpszHostName);
    if (hostname.find(L"github.com") != std::wstring::npos) {
        headers += L"Accept: application/vnd.github+json\r\n";
    }
    if (existing > 0) {
        headers += L"Range: bytes=" + std::to_wstring(existing) + L"-\r\n";
    }
    if (!WinHttpSendRequest(
            request.get(),
            headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(),
            headers.empty() ? 0 : static_cast<DWORD>(-1L),
            WINHTTP_NO_REQUEST_DATA,
            0,
            0,
            0)) {
        throw std::runtime_error(
            "WinHttpSendRequest не удался, код " + last_winhttp_error());
    }
    if (!WinHttpReceiveResponse(request.get(), nullptr)) {
        throw std::runtime_error(
            "WinHttpReceiveResponse не удался, код " + last_winhttp_error());
    }

    DWORD status = 0;
    DWORD status_size = sizeof(status);
    if (!WinHttpQueryHeaders(
            request.get(),
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX,
            &status,
            &status_size,
            WINHTTP_NO_HEADER_INDEX)) {
        throw std::runtime_error("Не удалось прочитать HTTP-статус");
    }

    if (status == 416 && existing > 0) {
        return;
    }
    if (status != 200 && status != 206) {
        app_log_warn(
            "HTTP " + std::to_string(status) + " " + url);
        throw std::runtime_error(
            "HTTP " + std::to_string(status) + " для " + url);
    }

    std::uintmax_t write_from = existing;
    std::ios::openmode mode = std::ios::binary | std::ios::app;
    if (existing > 0 && status == 200) {
        write_from = 0;
        mode = std::ios::binary | std::ios::trunc;
    }

    DWORD length_size = 0;
    WinHttpQueryHeaders(
        request.get(),
        WINHTTP_QUERY_CONTENT_RANGE,
        WINHTTP_HEADER_NAME_BY_INDEX,
        WINHTTP_NO_OUTPUT_BUFFER,
        &length_size,
        WINHTTP_NO_HEADER_INDEX);

    std::uint64_t total = write_from;
    if (GetLastError() == ERROR_INSUFFICIENT_BUFFER && length_size > 0) {
        std::wstring range(length_size / sizeof(wchar_t), L'\0');
        if (WinHttpQueryHeaders(
                request.get(),
                WINHTTP_QUERY_CONTENT_RANGE,
                WINHTTP_HEADER_NAME_BY_INDEX,
                range.data(),
                &length_size,
                WINHTTP_NO_HEADER_INDEX)) {
            const auto slash = range.find(L'/');
            if (slash != std::wstring::npos) {
                total = std::wcstoull(range.c_str() + slash + 1, nullptr, 10);
            }
        }
    } else {
        DWORD content_length = 0;
        DWORD content_length_size = sizeof(content_length);
        if (WinHttpQueryHeaders(
                request.get(),
                WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
                WINHTTP_HEADER_NAME_BY_INDEX,
                &content_length,
                &content_length_size,
                WINHTTP_NO_HEADER_INDEX)) {
            total = write_from + content_length;
        }
    }

    std::ofstream out(part_path, mode);
    if (!out) {
        throw std::runtime_error(
            "Не удалось открыть временный файл: " + part_path.string());
    }

    std::uint64_t downloaded = write_from;
    std::uint64_t last_report =
        downloaded > kProgressStep ? downloaded - kProgressStep : 0;
    std::vector<char> buffer(kChunkSize);
    while (true) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request.get(), &available)) {
            throw std::runtime_error(
                "WinHttpQueryDataAvailable, код " + last_winhttp_error());
        }
        if (available == 0) {
            break;
        }
        DWORD got = 0;
        const DWORD to_read = std::min<DWORD>(
            available, static_cast<DWORD>(buffer.size()));
        if (!WinHttpReadData(request.get(), buffer.data(), to_read, &got)) {
            throw std::runtime_error(
                "WinHttpReadData, код " + last_winhttp_error());
        }
        if (got == 0) {
            break;
        }
        out.write(buffer.data(), static_cast<std::streamsize>(got));
        downloaded += got;
        if (progress &&
            (downloaded - last_report >= kProgressStep ||
             (total > 0 && downloaded >= total))) {
            last_report = downloaded;
            report_progress(progress, downloaded, total, message);
        }
    }
    if (!out) {
        throw std::runtime_error(
            "Ошибка записи временного файла: " + part_path.string());
    }
}

#endif  // _WIN32

void download_one_url(
    const std::string& url,
    const std::filesystem::path& part_path,
    std::uintmax_t existing,
    const ProgressCallback& progress,
    std::string_view message) {
    if (!is_http_url(url)) {
        copy_local_resumable(url, part_path, existing, progress, message);
        return;
    }
#ifdef _WIN32
    download_http_resumable(url, part_path, existing, progress, message);
#else
    throw std::runtime_error("HTTP-загрузка поддерживается только на Windows");
#endif
}

}  // анонимное пространство имён

void download_resumable(
    const std::vector<std::string>& urls,
    const std::filesystem::path& destination,
    const ProgressCallback& progress,
    std::string_view message,
    std::optional<std::uintmax_t> skip_if_at_least) {
    if (urls.empty()) {
        throw std::invalid_argument("Не заданы URL для загрузки");
    }
    if (fs_utils::is_regular_file(destination)) {
        const auto size = fs_utils::file_size_or_zero(destination);
        const bool complete =
            skip_if_at_least.has_value() ? size >= *skip_if_at_least : size > 0;
        if (complete) {
            report_progress(progress, 1, 1, std::string(message) + " — уже скачан");
            return;
        }
    }

    std::filesystem::create_directories(destination.parent_path());
    const auto part_path = std::filesystem::path(
        destination.native() + std::filesystem::path(".part").native());

    std::string last_error;
    for (const auto& url : urls) {
        try {
            app_log_info("скачивание " + url);
            auto existing = fs_utils::file_size_or_zero(part_path);
            download_one_url(url, part_path, existing, progress, message);
            if (std::filesystem::exists(destination)) {
                std::filesystem::remove(destination);
            }
            std::filesystem::rename(part_path, destination);
            report_progress(
                progress,
                fs_utils::file_size_or_zero(destination),
                fs_utils::file_size_or_zero(destination),
                message);
            return;
        } catch (const std::exception& error) {
            last_error = error.what();
        }
    }
    throw std::runtime_error(
        "Не удалось скачать " + destination.filename().string() + ": " +
        last_error);
}

}  // пространство имён offline_translator
