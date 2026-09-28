#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace offline_translator {

// Режет текст на предложения, как Python `_split_sentences` в argos_engine.py:
// после . ! ? … и пробелов либо по переводам строк.
std::vector<std::string> split_sentences(std::string_view text);

}  // пространство имён offline_translator
