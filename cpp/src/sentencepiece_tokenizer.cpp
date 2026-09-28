#include "offline_translator/sentencepiece_tokenizer.hpp"

#include <sentencepiece_processor.h>

#include <fstream>
#include <iterator>
#include <stdexcept>
#include <utility>

namespace offline_translator {
namespace {

std::string path_text(const std::filesystem::path& path) {
    const auto utf8 = path.u8string();
    return std::string(utf8.begin(), utf8.end());
}

std::string read_model_proto(const std::filesystem::path& model_path) {
    // ifstream на Windows открывает путь через wchar_t; Load() SentencePiece —
    // нет, и падает на каталогах с кириллицей.
    std::ifstream input(model_path, std::ios::binary);
    if (!input) {
        throw std::runtime_error(
            "Не удалось открыть SentencePiece: " + path_text(model_path));
    }
    std::string proto(
        (std::istreambuf_iterator<char>(input)),
        std::istreambuf_iterator<char>());
    if (proto.empty()) {
        throw std::runtime_error(
            "Пустой файл SentencePiece: " + path_text(model_path));
    }
    return proto;
}

}  // пространство имён

class SentencePieceTokenizer::State {
public:
    explicit State(const std::filesystem::path& model_path) {
        const auto status = processor.LoadFromSerializedProto(
            read_model_proto(model_path));
        if (!status.ok()) {
            throw std::runtime_error(
                "Не удалось загрузить SentencePiece: " + status.ToString());
        }
    }

    sentencepiece::SentencePieceProcessor processor;
};

SentencePieceTokenizer::SentencePieceTokenizer(std::filesystem::path model_path)
    : state_(std::make_unique<State>(model_path)) {}

SentencePieceTokenizer::~SentencePieceTokenizer() = default;

std::vector<std::string> SentencePieceTokenizer::tokenize(
    std::string_view text) const {
    const auto pieces = state_->processor.EncodeAsPieces(std::string(text));
    if (pieces.empty()) {
        throw std::runtime_error("SentencePiece вернул пустые токены");
    }
    return pieces;
}

std::string SentencePieceTokenizer::detokenize(
    const std::vector<std::string>& pieces) const {
    const std::string decoded = state_->processor.DecodePieces(pieces);
    if (decoded.empty()) {
        throw std::runtime_error("SentencePiece вернул пустой текст");
    }
    return decoded;
}

}  // пространство имён offline_translator
