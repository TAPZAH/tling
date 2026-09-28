#pragma once

#include "offline_translator/package_types.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace offline_translator {

// Запись единой таблицы языков интерфейса.
struct LanguageEntry {
    std::string code;  // ISO 639-1, например "en"
    std::string name;  // русское название, UTF-8, например "Английский"
};

// Единая таблица поддерживаемых языков (56 ISO-кодов NLLB-200).
// Используется комбо главного окна, окном пакетов и поиском.
const std::vector<LanguageEntry>& supported_languages();

// Русское название языка для интерфейса: сначала единая таблица,
// затем словарь selection.cpp, затем fallback (или сам код).
std::string language_store_name(std::string_view code,
                                std::string_view fallback = {});

// Пара языков в «магазине языков»: строка каталога Argos или модели NLLB.
struct StorePair {
    bool nllb{false};
    std::string from_code;
    std::string to_code;
    bool installed{false};
    bool incomplete{false};
};

// Слияние каталога Argos и установленных пакетов + строка NLLB.
// Дедупликация по (from, to); пары из установленного списка помечаются
// installed = true. Порядок не определён до sort_store_pairs().
std::vector<StorePair> merge_store_pairs(
    const std::vector<PackageInfo>& catalog_packages,
    const std::vector<PackageInfo>& installed_packages);

// Сортировка как в Python: NLLB первой, установленные и повреждённые
// выше, пары с ru/en ("популярные") выше прочих, далее по кодам from/to.
void sort_store_pairs(std::vector<StorePair>& pairs);

// Поиск без учёта регистра (ASCII и кириллица) по кодам пары и
// русским названиям. Пустой запрос совпадает со всем. Строка NLLB
// ищется также по "nllb", "600m" и названию модели.
bool store_pair_matches(const StorePair& pair, std::string_view query);

// Человекочитаемая подпись: "Английский → Русский" или название
// модели NLLB. Названия берутся из language_store_name().
std::string store_pair_label(const StorePair& pair);

}  // namespace offline_translator
