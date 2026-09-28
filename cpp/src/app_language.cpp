#include "offline_translator/app_language.hpp"

#include <array>
#include <cstdlib>
#include <unordered_map>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace offline_translator {
namespace {

using Dict = std::unordered_map<std::wstring, std::wstring>;
using Pair = std::pair<const wchar_t*, const wchar_t*>;

#include "i18n/en.inc"
#include "i18n/de.inc"
#include "i18n/fr.inc"
#include "i18n/es.inc"

template <std::size_t Size>
Dict build_dict(const std::array<Pair, Size>& items) {
    Dict dict;
    dict.reserve(items.size());
    for (const auto& item : items) {
        dict.emplace(item.first, item.second);
    }
    return dict;
}

const Dict& dict_for(std::string_view language) {
    static const Dict ru{};
    static const Dict en = build_dict(kStringsEn);
    static const Dict de = build_dict(kStringsDe);
    static const Dict fr = build_dict(kStringsFr);
    static const Dict es = build_dict(kStringsEs);
    if (language == "en") {
        return en;
    }
    if (language == "de") {
        return de;
    }
    if (language == "fr") {
        return fr;
    }
    if (language == "es") {
        return es;
    }
    return ru;
}

std::string g_language;  // пусто — ещё не выбрали

}  // namespace

const std::vector<UiLanguageOption>& ui_language_options() {
    static const std::vector<UiLanguageOption> options{
        {"ru", L"Русский"},
        {"en", L"English"},
        {"de", L"Deutsch"},
        {"fr", L"Français"},
        {"es", L"Español"},
    };
    return options;
}

std::string detect_ui_language() {
#ifdef _WIN32
    const LANGID lang_id = GetUserDefaultUILanguage();
    switch (PRIMARYLANGID(lang_id)) {
        case LANG_RUSSIAN:
            return "ru";
        case LANG_GERMAN:
            return "de";
        case LANG_FRENCH:
            return "fr";
        case LANG_SPANISH:
            return "es";
        default:
            break;
    }
    return "en";
#else
    return "en";
#endif
}

std::string normalize_ui_language(std::string_view code) {
    if (code.empty() || code == "auto") {
        return detect_ui_language();
    }
    for (const auto& option : ui_language_options()) {
        if (code == option.code) {
            return option.code;
        }
    }
    return detect_ui_language();
}

std::string current_ui_language() {
    if (g_language.empty()) {
        g_language = detect_ui_language();
    }
    return g_language;
}

void set_ui_language(std::string_view code) {
    g_language = normalize_ui_language(code);
}

std::wstring tr(std::wstring_view russian) {
    const std::string language = current_ui_language();
    if (language == "ru") {
        return std::wstring(russian);
    }
    const Dict& dict = dict_for(language);
    const auto found = dict.find(std::wstring(russian));
    if (found == dict.end()) {
        return std::wstring(russian);
    }
    return found->second;
}

}  // namespace offline_translator
