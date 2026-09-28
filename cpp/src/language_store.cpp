#include "offline_translator/language_store.hpp"

#include "offline_translator/selection.hpp"

#include <algorithm>
#include <set>

namespace offline_translator {
namespace {

// Единая таблица: те же 56 ISO-кодов, что в nllb_language.cpp,
// и русские названия как LANGUAGE_NAMES в Python (language_detect.py).
const std::vector<LanguageEntry>& language_table() {
    static const std::vector<LanguageEntry> table{
        {"en", "Английский"},
        {"ru", "Русский"},
        {"de", "Немецкий"},
        {"fr", "Французский"},
        {"es", "Испанский"},
        {"it", "Итальянский"},
        {"pt", "Португальский"},
        {"zh", "Китайский"},
        {"ja", "Японский"},
        {"ko", "Корейский"},
        {"ar", "Арабский"},
        {"uk", "Украинский"},
        {"pl", "Польский"},
        {"tr", "Турецкий"},
        {"nl", "Нидерландский"},
        {"cs", "Чешский"},
        {"sv", "Шведский"},
        {"fi", "Финский"},
        {"el", "Греческий"},
        {"he", "Иврит"},
        {"hi", "Хинди"},
        {"id", "Индонезийский"},
        {"az", "Азербайджанский"},
        {"be", "Белорусский"},
        {"bg", "Болгарский"},
        {"bn", "Бенгальский"},
        {"bs", "Боснийский"},
        {"ca", "Каталанский"},
        {"da", "Датский"},
        {"et", "Эстонский"},
        {"fa", "Персидский"},
        {"gu", "Гуджарати"},
        {"hr", "Хорватский"},
        {"hu", "Венгерский"},
        {"is", "Исландский"},
        {"kn", "Каннада"},
        {"lt", "Литовский"},
        {"lv", "Латышский"},
        {"ml", "Малаялам"},
        {"ms", "Малайский"},
        {"mt", "Мальтийский"},
        {"nb", "Норвежский (букмол)"},
        {"nn", "Норвежский (нюнорск)"},
        {"ro", "Румынский"},
        {"sk", "Словацкий"},
        {"sl", "Словенский"},
        {"sq", "Албанский"},
        {"sr", "Сербский"},
        {"ta", "Тамильский"},
        {"te", "Телугу"},
        {"th", "Тайский"},
        {"vi", "Вьетнамский"},
        {"mk", "Македонский"},
        {"gl", "Галисийский"},
        {"ur", "Урду"},
        {"ka", "Грузинский"},
    };
    return table;
}

constexpr char32_t kCyrillicUpperFirst = U'А';  // U+0410
constexpr char32_t kCyrillicUpperLast = U'Я';   // U+042F
constexpr char32_t kCyrillicLowerShift = 0x20;
constexpr char32_t kYoUpper = U'Ё';         // U+0401
constexpr char32_t kYoLower = U'ё';         // U+0451

char32_t decode_utf8(std::string_view text, std::size_t& index) {
    if (index >= text.size()) {
        return 0;
    }
    const auto lead = static_cast<unsigned char>(text[index]);
    auto next = [&](int extra, char32_t value) -> char32_t {
        ++index;
        for (int i = 0; i < extra; ++i) {
            if (index >= text.size()) {
                return value;
            }
            const auto cont = static_cast<unsigned char>(text[index]);
            if ((cont & 0xC0) != 0x80) {
                return value;
            }
            value = (value << 6) | (cont & 0x3F);
            ++index;
        }
        return value;
    };
    if (lead < 0x80) {
        ++index;
        return lead;
    }
    if ((lead & 0xE0) == 0xC0) {
        return next(1, lead & 0x1F);
    }
    if ((lead & 0xF0) == 0xE0) {
        return next(2, lead & 0x0F);
    }
    if ((lead & 0xF8) == 0xF0) {
        return next(3, lead & 0x07);
    }
    ++index;
    return lead;
}

void append_utf8(std::string& out, char32_t ch) {
    if (ch < 0x80) {
        out.push_back(static_cast<char>(ch));
    } else if (ch < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (ch >> 6)));
        out.push_back(static_cast<char>(0x80 | (ch & 0x3F)));
    } else if (ch < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (ch >> 12)));
        out.push_back(
            static_cast<char>(0x80 | ((ch >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (ch & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (ch >> 18)));
        out.push_back(
            static_cast<char>(0x80 | ((ch >> 12) & 0x3F)));
        out.push_back(
            static_cast<char>(0x80 | ((ch >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (ch & 0x3F)));
    }
}

std::string utf8_lower(std::string_view text) {
    std::string result;
    result.reserve(text.size());
    std::size_t index = 0;
    while (index < text.size()) {
        char32_t ch = decode_utf8(text, index);
        if (ch >= U'A' && ch <= U'Z') {
            ch += static_cast<char32_t>('a' - 'A');
        } else if (ch >= kCyrillicUpperFirst && ch <= kCyrillicUpperLast) {
            ch += kCyrillicLowerShift;
        } else if (ch == kYoUpper) {
            ch = kYoLower;
        }
        append_utf8(result, ch);
    }
    return result;
}

std::pair<std::string, std::string> store_pair_names(const StorePair& pair) {
    return {language_store_name(pair.from_code),
            language_store_name(pair.to_code)};
}

bool is_popular_pair(const StorePair& pair) {
    return pair.from_code == "ru" || pair.from_code == "en" ||
        pair.to_code == "ru" || pair.to_code == "en";
}

std::string_view nllb_search_haystack() {
    return "nllb nllb-200 distilled 600m модель";
}

}  // namespace

const std::vector<LanguageEntry>& supported_languages() {
    return language_table();
}

std::string language_store_name(std::string_view code,
                                std::string_view fallback) {
    for (const auto& entry : language_table()) {
        if (entry.code == code) {
            return entry.name;
        }
    }
    const auto selection_name = language_display_name(code);
    if (!selection_name.empty() && selection_name != code) {
        return selection_name;
    }
    return fallback.empty() ? std::string(code) : std::string(fallback);
}

std::vector<StorePair> merge_store_pairs(
    const std::vector<PackageInfo>& catalog_packages,
    const std::vector<PackageInfo>& installed_packages) {
    std::vector<StorePair> pairs;
    StorePair nllb;
    nllb.nllb = true;
    pairs.push_back(std::move(nllb));

    std::set<std::pair<std::string, std::string>> seen;
    auto key = [](const PackageInfo& item) {
        return std::make_pair(item.from_code, item.to_code);
    };
    for (const auto& item : installed_packages) {
        StorePair pair;
        pair.from_code = item.from_code;
        pair.to_code = item.to_code;
        pair.installed = true;
        pairs.push_back(std::move(pair));
        seen.insert(key(item));
    }
    for (const auto& item : catalog_packages) {
        if (seen.count(key(item)) > 0) {
            continue;
        }
        StorePair pair;
        pair.from_code = item.from_code;
        pair.to_code = item.to_code;
        pairs.push_back(std::move(pair));
    }
    return pairs;
}

void sort_store_pairs(std::vector<StorePair>& pairs) {
    std::stable_sort(pairs.begin(), pairs.end(),
                     [](const StorePair& left, const StorePair& right) {
                         if (left.nllb != right.nllb) {
                             return left.nllb;
                         }
                         const bool left_active =
                             left.installed || left.incomplete;
                         const bool right_active =
                             right.installed || right.incomplete;
                         if (left_active != right_active) {
                             return left_active;
                         }
                         const bool left_popular = is_popular_pair(left);
                         const bool right_popular = is_popular_pair(right);
                         if (left_popular != right_popular) {
                             return left_popular;
                         }
                         if (left.from_code != right.from_code) {
                             return left.from_code < right.from_code;
                         }
                         return left.to_code < right.to_code;
                     });
}

bool store_pair_matches(const StorePair& pair, std::string_view query) {
    const std::string needle = utf8_lower(query);
    if (needle.empty()) {
        return true;
    }
    if (pair.nllb) {
        return utf8_lower(nllb_search_haystack())
            .find(needle) != std::string::npos;
    }
    const auto [from_name, to_name] = store_pair_names(pair);
    std::string haystack;
    haystack.reserve(pair.from_code.size() + pair.to_code.size() +
                     from_name.size() + to_name.size() + 3);
    haystack += pair.from_code;
    haystack += ' ';
    haystack += pair.to_code;
    haystack += ' ';
    haystack += utf8_lower(from_name);
    haystack += ' ';
    haystack += utf8_lower(to_name);
    return haystack.find(needle) != std::string::npos;
}

std::string store_pair_label(const StorePair& pair) {
    if (pair.nllb) {
        return "NLLB-200 Distilled 600M";
    }
    return language_store_name(pair.from_code) + " → " +
        language_store_name(pair.to_code);
}

}  // namespace offline_translator
