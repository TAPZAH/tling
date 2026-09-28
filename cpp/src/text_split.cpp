#include "offline_translator/text_split.hpp"

namespace offline_translator {
namespace {

bool is_ascii_space(unsigned char byte) {
    return byte == ' ' || byte == '\t' || byte == '\r' || byte == '\n';
}

bool ends_with_sentence_punct(const std::string& current) {
    if (current.empty()) {
        return false;
    }
    const unsigned char last = static_cast<unsigned char>(current.back());
    if (last == '.' || last == '!' || last == '?') {
        return true;
    }
    // Многоточие U+2026 в UTF-8: E2 80 A6
    constexpr unsigned char kEllipsis[] = {0xE2, 0x80, 0xA6};
    if (current.size() < 3) {
        return false;
    }
    return static_cast<unsigned char>(current[current.size() - 3]) == kEllipsis[0] &&
           static_cast<unsigned char>(current[current.size() - 2]) == kEllipsis[1] &&
           last == kEllipsis[2];
}

std::string trim_copy(std::string_view text) {
    std::size_t begin = 0;
    while (begin < text.size() &&
           is_ascii_space(static_cast<unsigned char>(text[begin]))) {
        ++begin;
    }
    std::size_t end = text.size();
    while (end > begin &&
           is_ascii_space(static_cast<unsigned char>(text[end - 1]))) {
        --end;
    }
    return std::string(text.substr(begin, end - begin));
}

}  // анонимное пространство имён

std::vector<std::string> split_sentences(std::string_view text) {
    const std::string source = trim_copy(text);
    if (source.empty()) {
        return {};
    }

    std::vector<std::string> sentences;
    std::string current;
    current.reserve(source.size());

    for (std::size_t index = 0; index < source.size(); ++index) {
        const unsigned char byte = static_cast<unsigned char>(source[index]);
        if (byte == '\n' || byte == '\r') {
            const auto piece = trim_copy(current);
            if (!piece.empty()) {
                sentences.push_back(piece);
            }
            current.clear();
            while (index + 1 < source.size()) {
                const unsigned char next =
                    static_cast<unsigned char>(source[index + 1]);
                if (next != '\n' && next != '\r') {
                    break;
                }
                ++index;
            }
            continue;
        }

        current.push_back(source[index]);
        if (!ends_with_sentence_punct(current)) {
            continue;
        }
        if (index + 1 >= source.size()) {
            continue;
        }
        const unsigned char next = static_cast<unsigned char>(source[index + 1]);
        if (!is_ascii_space(next)) {
            continue;
        }
        sentences.push_back(trim_copy(current));
        current.clear();
        while (index + 1 < source.size() &&
               is_ascii_space(static_cast<unsigned char>(source[index + 1]))) {
            ++index;
        }
    }

    const auto tail = trim_copy(current);
    if (!tail.empty()) {
        sentences.push_back(tail);
    }
    if (sentences.empty()) {
        sentences.push_back(source);
    }
    return sentences;
}

}  // пространство имён offline_translator
