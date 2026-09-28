#pragma once

#include <string>
#include <string_view>

namespace offline_translator {

// Возвращает код FLORES-200 для ISO-кода языка.
std::string nllb_language_code(std::string_view language_code);

}  // пространство имён offline_translator
