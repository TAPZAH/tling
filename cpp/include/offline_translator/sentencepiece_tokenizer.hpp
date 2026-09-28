#pragma once

#include "offline_translator/ctranslate2_engine.hpp"

#include <filesystem>
#include <memory>
#include <string>

namespace offline_translator {

class SentencePieceTokenizer {
public:
    explicit SentencePieceTokenizer(std::filesystem::path model_path);
    ~SentencePieceTokenizer();

    std::vector<std::string> tokenize(std::string_view text) const;
    std::string detokenize(const std::vector<std::string>& pieces) const;

private:
    class State;
    std::unique_ptr<State> state_;
};

}  // пространство имён offline_translator
