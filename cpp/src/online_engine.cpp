#include "offline_translator/online_engine.hpp"

#include "offline_translator/app_log.hpp"

#include <windows.h>
#include <winhttp.h>

#include <nlohmann/json.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace offline_translator {
namespace {

using json = nlohmann::json;

// --- HTTP ------------------------------------------------------------------

class WinHttpHandle {
public:
    explicit WinHttpHandle(HINTERNET handle) : handle_(handle) {}
    ~WinHttpHandle() {
        if (handle_ != nullptr) {
            ::WinHttpCloseHandle(handle_);
        }
    }
    WinHttpHandle(const WinHttpHandle&) = delete;
    WinHttpHandle& operator=(const WinHttpHandle&) = delete;

    HINTERNET get() const { return handle_; }

private:
    HINTERNET handle_{nullptr};
};

std::wstring utf8_to_wide(const std::string& text) {
    if (text.empty()) {
        return {};
    }
    const int size = ::MultiByteToWideChar(
        CP_UTF8,
        0,
        text.data(),
        static_cast<int>(text.size()),
        nullptr,
        0);
    if (size <= 0) {
        return {};
    }
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    ::MultiByteToWideChar(
        CP_UTF8,
        0,
        text.data(),
        static_cast<int>(text.size()),
        result.data(),
        size);
    return result;
}

void parse_url(const std::wstring& url, std::wstring& host, std::wstring& path) {
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwSchemeLength = static_cast<DWORD>(-1);
    parts.dwHostNameLength = static_cast<DWORD>(-1);
    parts.dwUrlPathLength = static_cast<DWORD>(-1);
    parts.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!::WinHttpCrackUrl(
            url.c_str(),
            static_cast<DWORD>(url.size()),
            0,
            &parts)) {
        throw std::runtime_error("Некорректный адрес онлайн-перевода");
    }
    host.assign(parts.lpszHostName, parts.dwHostNameLength);
    path.assign(parts.lpszUrlPath, parts.dwUrlPathLength);
    if (parts.dwExtraInfoLength > 0) {
        path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    }
}

// GET-запрос с таймаутами; возвращает тело ответа (UTF-8).
std::string http_get_text(const std::string& url) {
    const std::wstring wide_url = utf8_to_wide(url);
    if (wide_url.empty()) {
        throw std::runtime_error("Пустой адрес онлайн-перевода");
    }
    std::wstring host;
    std::wstring path;
    parse_url(wide_url, host, path);

    WinHttpHandle session(::WinHttpOpen(
        L"TLing/0.99.7 (online translate)",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0));
    if (!session.get()) {
        throw std::runtime_error("Не удалось открыть HTTP-сессию");
    }
    const int timeouts[3] = {10000, 20000, 20000};
    ::WinHttpSetTimeouts(session.get(), timeouts[0], timeouts[1], timeouts[1], timeouts[2]);
    {
        DWORD decompression = WINHTTP_DECOMPRESSION_FLAG_ALL;
        ::WinHttpSetOption(
            session.get(),
            WINHTTP_OPTION_DECOMPRESSION,
            &decompression,
            sizeof(decompression));
        DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 |
            WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
        ::WinHttpSetOption(
            session.get(),
            WINHTTP_OPTION_SECURE_PROTOCOLS,
            &protocols,
            sizeof(protocols));
    }

    WinHttpHandle connection(::WinHttpConnect(
        session.get(),
        host.c_str(),
        INTERNET_DEFAULT_HTTPS_PORT,
        0));
    if (!connection.get()) {
        throw std::runtime_error("Не удалось подключиться к сервису перевода");
    }
    WinHttpHandle request(::WinHttpOpenRequest(
        connection.get(),
        L"GET",
        path.c_str(),
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE));
    if (!request.get()) {
        throw std::runtime_error("Не удалось создать HTTP-запрос");
    }
    if (!::WinHttpSendRequest(
            request.get(),
            WINHTTP_NO_ADDITIONAL_HEADERS,
            0,
            WINHTTP_NO_REQUEST_DATA,
            0,
            0,
            0)) {
        throw std::runtime_error("Не удалось отправить запрос к сервису перевода");
    }
    if (!::WinHttpReceiveResponse(request.get(), nullptr)) {
        throw std::runtime_error("Сервис перевода не ответил");
    }
    DWORD status = 0;
    DWORD status_size = sizeof(status);
    if (!::WinHttpQueryHeaders(
            request.get(),
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX,
            &status,
            &status_size,
            WINHTTP_NO_HEADER_INDEX)) {
        throw std::runtime_error("Не удалось прочитать ответ сервиса перевода");
    }

    std::string body;
    std::array<char, 8192> buffer{};
    while (true) {
        DWORD read = 0;
        if (!::WinHttpReadData(
                request.get(),
                buffer.data(),
                static_cast<DWORD>(buffer.size()),
                &read)) {
            throw std::runtime_error("Ошибка чтения ответа сервиса перевода");
        }
        if (read == 0) {
            break;
        }
        body.append(buffer.data(), read);
        if (body.size() > 4 * 1024 * 1024) {
            throw std::runtime_error("Ответ сервиса перевода слишком большой");
        }
    }
    if (status != 200) {
        throw std::runtime_error(
            "Сервис перевода ответил кодом " + std::to_string(status));
    }
    return body;
}

// --- URL и коды языков ------------------------------------------------------

std::string online_text(std::string_view text) {
    return url_encode(text);
}

}  // namespace

std::string url_encode(std::string_view text) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string result;
    result.reserve(text.size() * 3);
    for (const unsigned char ch : text) {
        const bool unreserved =
            (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
            (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' ||
            ch == '.' || ch == '~';
        if (unreserved) {
            result.push_back(static_cast<char>(ch));
            continue;
        }
        result.push_back('%');
        result.push_back(kHex[ch >> 4]);
        result.push_back(kHex[ch & 0x0F]);
    }
    return result;
}

std::string online_language_code(
    OnlineProvider provider,
    std::string_view code) {
    if (code == "pb") {
        return "pt";
    }
    if (code == "zt") {
        return provider == OnlineProvider::google ? "zh-TW" : "zh";
    }
    if (code == "zh") {
        return provider == OnlineProvider::google ? "zh-CN" : "zh";
    }
    if (code == "he" && provider == OnlineProvider::google) {
        // Google исторически использует код iw для иврита.
        return "iw";
    }
    if (code == "nb" || code == "nn") {
        return "no";
    }
    if (code == "auto") {
        return "auto";
    }
    return std::string(code);
}

std::string build_google_translate_url(
    std::string_view source_code,
    std::string_view target_code,
    std::string_view text,
    std::string_view api_key) {
    const std::string source =
        online_language_code(OnlineProvider::google, source_code);
    const std::string target =
        online_language_code(OnlineProvider::google, target_code);
    const std::string encoded = online_text(text);
    if (!api_key.empty()) {
        return "https://translation.googleapis.com/language/translate/v2"
               "?key=" +
            url_encode(api_key) +
            "&source=" + url_encode(source) +
            "&target=" + url_encode(target) +
            "&format=text&q=" + encoded;
    }
    // Бесплатный эндпоинт веб-переводчика: ключ не нужен, но сервис может
    // ответить ограничением на автоматические запросы.
    return "https://translate.googleapis.com/translate_a/single"
           "?client=gtx&sl=" +
        url_encode(source) + "&tl=" + url_encode(target) + "&dt=t&q=" +
        encoded;
}

std::string build_yandex_translate_url(
    std::string_view source_code,
    std::string_view target_code,
    std::string_view text,
    std::string_view api_key) {
    const std::string source =
        online_language_code(OnlineProvider::yandex, source_code);
    const std::string target =
        online_language_code(OnlineProvider::yandex, target_code);
    std::string lang;
    if (source.empty() || source == "auto") {
        // Без исходного языка Яндекс определяет его сам.
        lang = target;
    } else {
        lang = source + "-" + target;
    }
    return "https://translate.yandex.net/api/v1.5/tr.json/translate"
           "?key=" +
        url_encode(api_key) + "&lang=" + url_encode(lang) +
        "&format=plain&text=" + online_text(text);
}

std::string parse_google_translation(std::string_view json_text) {
    // Официальный API возвращает объект, бесплатный эндпоинт — массив.
    if (json_text.empty() ||
        (json_text.front() != '{' && json_text.front() != '[')) {
        throw std::runtime_error(
            "Google вернул неожиданный ответ: возможно, сервис ограничил "
            "автоматические запросы — укажите API-ключ Google в настройках");
    }
    json parsed;
    try {
        parsed = json::parse(json_text);
    } catch (const std::exception&) {
        throw std::runtime_error("Не удалось разобрать ответ Google");
    }
    if (parsed.contains("error")) {
        const std::string message =
            parsed["error"].value("message", std::string{});
        throw std::runtime_error(
            "Google вернул ошибку" +
            (message.empty() ? std::string{} : ": " + message));
    }
    // Официальный API v2: {"data":{"translations":[{"translatedText":..}]}}
    if (parsed.contains("data") && parsed["data"].is_object() &&
        parsed["data"].contains("translations") &&
        parsed["data"]["translations"].is_array() &&
        !parsed["data"]["translations"].empty()) {
        return parsed["data"]["translations"][0].value(
            "translatedText", std::string{});
    }
    // Бесплатный эндпоинт: [[["перевод","исходный",..],[..]], ..]
    if (parsed.is_array() && !parsed.empty() && parsed[0].is_array()) {
        std::string result;
        for (const auto& part : parsed[0]) {
            if (part.is_array() && !part.empty() && part[0].is_string()) {
                result += part[0].get<std::string>();
            }
        }
        if (!result.empty()) {
            return result;
        }
    }
    throw std::runtime_error("Google вернул пустой перевод");
}

std::string parse_yandex_translation(std::string_view json_text) {
    if (json_text.empty() || json_text.front() != '{') {
        throw std::runtime_error("Яндекс вернул неожиданный ответ");
    }
    json parsed;
    try {
        parsed = json::parse(json_text);
    } catch (const std::exception&) {
        throw std::runtime_error("Не удалось разобрать ответ Яндекса");
    }
    const int code = parsed.value("code", 200);
    if (code != 200) {
        const std::string message =
            parsed.value("message", std::string{});
        if (message.find("key") != std::string::npos) {
            throw std::runtime_error(
                "Яндекс отклонил API-ключ — проверьте ключ в настройках");
        }
        throw std::runtime_error(
            "Яндекс вернул ошибку" +
            (message.empty() ? std::string{} : ": " + message));
    }
    if (!parsed.contains("text") || !parsed["text"].is_array() ||
        parsed["text"].empty()) {
        throw std::runtime_error("Яндекс вернул пустой перевод");
    }
    if (parsed["text"][0].is_string()) {
        return parsed["text"][0].get<std::string>();
    }
    return {};
}

OnlineEngine::OnlineEngine(OnlineProvider provider, std::string api_key)
    : provider_(provider), api_key_(std::move(api_key)) {}

TranslationResult OnlineEngine::translate(
    std::string_view text,
    std::string_view source_code,
    std::string_view target_code) {
    if (text.empty()) {
        return {std::string(text), std::nullopt, std::nullopt};
    }
    if (provider_ == OnlineProvider::yandex && api_key_.empty()) {
        throw std::runtime_error(
            "Для Яндекс.Переводчика нужен API-ключ: получите его на "
            "yandex.ru/dev/translate и укажите в настройках на странице "
            "«Языки»");
    }
    const std::string url =
        provider_ == OnlineProvider::google
        ? build_google_translate_url(
              source_code, target_code, text, api_key_)
        : build_yandex_translate_url(
              source_code, target_code, text, api_key_);
    app_log_info("онлайн-перевод " + url.substr(0, 64) + "...");
    const std::string body = http_get_text(url);
    const std::string translated =
        provider_ == OnlineProvider::google
        ? parse_google_translation(body)
        : parse_yandex_translation(body);
    if (translated.empty()) {
        throw std::runtime_error("Онлайн-сервис вернул пустой перевод");
    }
    return {translated, std::nullopt, std::nullopt};
}

}  // namespace offline_translator
