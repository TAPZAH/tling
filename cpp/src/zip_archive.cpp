#include "zip_archive.hpp"
#include "fs_utils.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace offline_translator {
namespace {

constexpr std::uint32_t kLocalSig = 0x04034b50;
constexpr std::uint32_t kCentralSig = 0x02014b50;
constexpr std::uint32_t kEocdSig = 0x06054b50;
constexpr int kMaxBits = 15;
constexpr int kMaxLits = 288;
constexpr int kMaxDists = 32;

std::uint16_t read_u16(const unsigned char* data) {
    return static_cast<std::uint16_t>(data[0] | (data[1] << 8));
}

std::uint32_t read_u32(const unsigned char* data) {
    return static_cast<std::uint32_t>(
        data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24));
}

void append_u16(std::vector<unsigned char>& out, std::uint16_t value) {
    out.push_back(static_cast<unsigned char>(value));
    out.push_back(static_cast<unsigned char>(value >> 8));
}

void append_u32(std::vector<unsigned char>& out, std::uint32_t value) {
    out.push_back(static_cast<unsigned char>(value));
    out.push_back(static_cast<unsigned char>(value >> 8));
    out.push_back(static_cast<unsigned char>(value >> 16));
    out.push_back(static_cast<unsigned char>(value >> 24));
}

std::uint32_t crc32_of(const unsigned char* data, std::size_t size) {
    static std::array<std::uint32_t, 256> table{};
    static bool ready = false;
    if (!ready) {
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int k = 0; k < 8; ++k) {
                c = (c & 1U) ? (0xEDB88320U ^ (c >> 1)) : (c >> 1);
            }
            table[i] = c;
        }
        ready = true;
    }
    std::uint32_t crc = 0xFFFFFFFFU;
    for (std::size_t i = 0; i < size; ++i) {
        crc = table[(crc ^ data[i]) & 0xFFU] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFU;
}

std::vector<unsigned char> read_all(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("Не удалось открыть ZIP: " + path.string());
    }
    in.seekg(0, std::ios::end);
    const auto size = static_cast<std::size_t>(in.tellg());
    in.seekg(0, std::ios::beg);
    std::vector<unsigned char> data(size);
    if (size > 0) {
        in.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(size));
    }
    if (!in) {
        throw std::runtime_error("Не удалось прочитать ZIP: " + path.string());
    }
    return data;
}

struct Huffman {
    std::array<int, kMaxLits> code{};
    std::array<int, kMaxLits> length{};
    int max_symbol = 0;
    int min_length = kMaxBits;
    int max_length = 0;
};

void build_huffman(Huffman& tree, const int* lengths, int n) {
    tree.code.fill(0);
    tree.length.fill(0);
    tree.max_symbol = n;
    tree.min_length = kMaxBits;
    tree.max_length = 0;
    std::array<int, kMaxBits + 1> count{};
    for (int i = 0; i < n; ++i) {
        if (lengths[i] < 0 || lengths[i] > kMaxBits) {
            throw std::runtime_error("Некорректная длина кода Huffman");
        }
        tree.length[i] = lengths[i];
        ++count[lengths[i]];
        if (lengths[i] > 0) {
            tree.min_length = std::min(tree.min_length, lengths[i]);
            tree.max_length = std::max(tree.max_length, lengths[i]);
        }
    }
    if (count[0] == n) {
        tree.min_length = 0;
        tree.max_length = 0;
        return;
    }
    int left = 1;
    for (int len = 1; len <= kMaxBits; ++len) {
        left <<= 1;
        left -= count[len];
        if (left < 0) {
            throw std::runtime_error("Переполненное дерево Huffman");
        }
    }
    std::array<int, kMaxBits + 1> next_code{};
    int code = 0;
    for (int bits = 1; bits <= kMaxBits; ++bits) {
        code = (code + count[bits - 1]) << 1;
        next_code[bits] = code;
    }
    for (int symbol = 0; symbol < n; ++symbol) {
        const int len = lengths[symbol];
        if (len != 0) {
            tree.code[symbol] = next_code[len]++;
        }
    }
}

class BitStream {
public:
    BitStream(const unsigned char* data, std::size_t size)
        : data_(data), size_(size) {}

    int get_bits(int count) {
        while (bit_count_ < count) {
            if (offset_ >= size_) {
                throw std::runtime_error("Обрыв DEFLATE-потока");
            }
            bit_buffer_ |= static_cast<unsigned>(data_[offset_++]) << bit_count_;
            bit_count_ += 8;
        }
        const int value = static_cast<int>(bit_buffer_ & ((1U << count) - 1U));
        bit_buffer_ >>= count;
        bit_count_ -= count;
        return value;
    }

    int decode(const Huffman& tree) {
        if (tree.max_length <= 0) {
            throw std::runtime_error("Пустое дерево Huffman");
        }
        int acc = 0;
        for (int len = 1; len <= tree.max_length; ++len) {
            acc = (acc << 1) | get_bits(1);
            if (len < tree.min_length) {
                continue;
            }
            for (int symbol = 0; symbol < tree.max_symbol; ++symbol) {
                if (tree.length[symbol] == len && tree.code[symbol] == acc) {
                    return symbol;
                }
            }
        }
        throw std::runtime_error("Неизвестный символ Huffman");
    }

    void align_byte() {
        bit_buffer_ = 0;
        bit_count_ = 0;
    }

    std::size_t offset() const { return offset_; }

private:
    const unsigned char* data_;
    std::size_t size_;
    std::size_t offset_{0};
    unsigned bit_buffer_{0};
    int bit_count_{0};
};

const std::array<int, 29> kLengthExtra{
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
    3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
const std::array<int, 29> kLengthBase{
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
    35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
const std::array<int, 30> kDistExtra{
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
    7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
const std::array<int, 30> kDistBase{
    1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
    257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193,
    12289, 16385, 24577};

void decode_codes(
    BitStream& bits,
    const Huffman& lit,
    const Huffman& dist,
    std::vector<unsigned char>& out) {
    while (true) {
        const int symbol = bits.decode(lit);
        if (symbol < 256) {
            out.push_back(static_cast<unsigned char>(symbol));
            continue;
        }
        if (symbol == 256) {
            return;
        }
        const int len_index = symbol - 257;
        if (len_index < 0 || len_index >= static_cast<int>(kLengthBase.size())) {
            throw std::runtime_error("Некорректный код длины DEFLATE");
        }
        int length = kLengthBase[static_cast<std::size_t>(len_index)] +
                     bits.get_bits(kLengthExtra[static_cast<std::size_t>(len_index)]);
        const int dist_symbol = bits.decode(dist);
        if (dist_symbol < 0 || dist_symbol >= static_cast<int>(kDistBase.size())) {
            throw std::runtime_error("Некорректный код дистанции DEFLATE");
        }
        const int distance =
            kDistBase[static_cast<std::size_t>(dist_symbol)] +
            bits.get_bits(kDistExtra[static_cast<std::size_t>(dist_symbol)]);
        if (distance <= 0 || static_cast<std::size_t>(distance) > out.size()) {
            throw std::runtime_error("Дистанция DEFLATE вне окна");
        }
        for (int i = 0; i < length; ++i) {
            out.push_back(out[out.size() - static_cast<std::size_t>(distance)]);
        }
    }
}

void inflate_dynamic(BitStream& bits, std::vector<unsigned char>& out) {
    const int nlit = bits.get_bits(5) + 257;
    const int ndist = bits.get_bits(5) + 1;
    const int ncode = bits.get_bits(4) + 4;
    static const int order[19] = {
        16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    int code_lengths[19]{};
    for (int i = 0; i < ncode; ++i) {
        code_lengths[order[i]] = bits.get_bits(3);
    }
    Huffman code_tree;
    build_huffman(code_tree, code_lengths, 19);
    std::vector<int> lengths(static_cast<std::size_t>(nlit + ndist), 0);
    int index = 0;
    while (index < nlit + ndist) {
        const int symbol = bits.decode(code_tree);
        if (symbol < 16) {
            lengths[static_cast<std::size_t>(index++)] = symbol;
        } else {
            int repeat = 0;
            int value = 0;
            if (symbol == 16) {
                if (index == 0) {
                    throw std::runtime_error("Повтор DEFLATE без предыдущей длины");
                }
                value = lengths[static_cast<std::size_t>(index - 1)];
                repeat = 3 + bits.get_bits(2);
            } else if (symbol == 17) {
                repeat = 3 + bits.get_bits(3);
            } else if (symbol == 18) {
                repeat = 11 + bits.get_bits(7);
            } else {
                throw std::runtime_error("Неизвестный код длин DEFLATE");
            }
            if (index + repeat > nlit + ndist) {
                throw std::runtime_error("Слишком много длин DEFLATE");
            }
            while (repeat-- > 0) {
                lengths[static_cast<std::size_t>(index++)] = value;
            }
        }
    }
    Huffman lit;
    Huffman dist;
    build_huffman(lit, lengths.data(), nlit);
    build_huffman(dist, lengths.data() + nlit, ndist);
    decode_codes(bits, lit, dist, out);
}

void inflate_fixed(BitStream& bits, std::vector<unsigned char>& out) {
    int lengths[kMaxLits + kMaxDists]{};
    for (int i = 0; i <= 143; ++i) {
        lengths[i] = 8;
    }
    for (int i = 144; i <= 255; ++i) {
        lengths[i] = 9;
    }
    for (int i = 256; i <= 279; ++i) {
        lengths[i] = 7;
    }
    for (int i = 280; i <= 287; ++i) {
        lengths[i] = 8;
    }
    for (int i = 0; i < kMaxDists; ++i) {
        lengths[kMaxLits + i] = 5;
    }
    Huffman lit;
    Huffman dist;
    build_huffman(lit, lengths, kMaxLits);
    build_huffman(dist, lengths + kMaxLits, kMaxDists);
    decode_codes(bits, lit, dist, out);
}

std::vector<unsigned char> inflate_raw(
    const unsigned char* data,
    std::size_t size,
    std::size_t uncompressed_size) {
    BitStream bits(data, size);
    std::vector<unsigned char> out;
    out.reserve(uncompressed_size);
    int last = 0;
    while (!last) {
        last = bits.get_bits(1);
        const int type = bits.get_bits(2);
        if (type == 0) {
            bits.align_byte();
            const int len = bits.get_bits(16);
            const int nlen = bits.get_bits(16);
            if ((len ^ 0xFFFF) != nlen) {
                throw std::runtime_error("Повреждён несжатый блок DEFLATE");
            }
            for (int i = 0; i < len; ++i) {
                out.push_back(static_cast<unsigned char>(bits.get_bits(8)));
            }
        } else if (type == 1) {
            inflate_fixed(bits, out);
        } else if (type == 2) {
            inflate_dynamic(bits, out);
        } else {
            throw std::runtime_error("Неподдерживаемый тип блока DEFLATE");
        }
    }
    return out;
}

bool is_safe_entry_name(std::string_view name) {
    if (name.empty() || name[0] == '/' || name[0] == '\\') {
        return false;
    }
    if (name.find(':') != std::string_view::npos) {
        return false;
    }
    std::filesystem::path relative(name);
    for (const auto& part : relative) {
        if (part == "..") {
            return false;
        }
    }
    return true;
}

void write_file(
    const std::filesystem::path& path,
    const unsigned char* data,
    std::size_t size) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        throw std::runtime_error("Не удалось создать файл из ZIP: " + path.string());
    }
    if (size > 0) {
        out.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
    }
    if (!out) {
        throw std::runtime_error("Ошибка записи файла из ZIP: " + path.string());
    }
}

}  // анонимное пространство имён

void extract_zip(
    const std::filesystem::path& zip_path,
    const std::filesystem::path& destination) {
    const auto data = read_all(zip_path);
    if (data.size() < 22) {
        throw std::runtime_error("Файл слишком мал, чтобы быть ZIP");
    }

    std::size_t eocd = data.size() - 22;
    bool found = false;
    for (std::size_t i = 0; i <= 65535 && eocd >= 22; ++i) {
        if (read_u32(data.data() + eocd) == kEocdSig) {
            found = true;
            break;
        }
        if (eocd == 0) {
            break;
        }
        --eocd;
    }
    if (!found) {
        throw std::runtime_error("Не найден каталог ZIP: " + zip_path.string());
    }

    const auto central_size = read_u32(data.data() + eocd + 12);
    const auto central_offset = read_u32(data.data() + eocd + 16);
    if (central_offset + central_size > data.size()) {
        throw std::runtime_error("Повреждён центральный каталог ZIP");
    }

    std::filesystem::create_directories(destination);
    std::size_t cursor = central_offset;
    const std::size_t central_end = central_offset + central_size;
    while (cursor + 46 <= central_end) {
        if (read_u32(data.data() + cursor) != kCentralSig) {
            throw std::runtime_error("Повреждена запись центрального каталога ZIP");
        }
        const auto method = read_u16(data.data() + cursor + 10);
        const auto compressed_size = read_u32(data.data() + cursor + 20);
        const auto uncompressed_size = read_u32(data.data() + cursor + 24);
        const auto name_len = read_u16(data.data() + cursor + 28);
        const auto extra_len = read_u16(data.data() + cursor + 30);
        const auto comment_len = read_u16(data.data() + cursor + 32);
        const auto local_offset = read_u32(data.data() + cursor + 42);
        if (cursor + 46 + name_len > data.size()) {
            throw std::runtime_error("Обрезанное имя файла в ZIP");
        }
        const std::string name(
            reinterpret_cast<const char*>(data.data() + cursor + 46),
            name_len);
        cursor += 46 + name_len + extra_len + comment_len;
        if (name.empty() || name.back() == '/' || name.back() == '\\') {
            continue;
        }
        if (!is_safe_entry_name(name)) {
            throw std::runtime_error("Небезопасное имя в ZIP: " + name);
        }
        if (local_offset + 30 > data.size()) {
            throw std::runtime_error("Смещение локального заголовка ZIP вне файла");
        }
        if (read_u32(data.data() + local_offset) != kLocalSig) {
            throw std::runtime_error("Повреждён локальный заголовок ZIP");
        }
        const auto local_name_len = read_u16(data.data() + local_offset + 26);
        const auto local_extra_len = read_u16(data.data() + local_offset + 28);
        const std::size_t data_offset =
            local_offset + 30 + local_name_len + local_extra_len;
        if (data_offset + compressed_size > data.size()) {
            throw std::runtime_error("Обрезанные данные ZIP: " + name);
        }
        const unsigned char* payload = data.data() + data_offset;
        std::vector<unsigned char> unpacked;
        const unsigned char* out_ptr = payload;
        std::size_t out_size = uncompressed_size;
        if (method == 0) {
            if (compressed_size < uncompressed_size) {
                out_size = compressed_size;
            }
        } else if (method == 8) {
            unpacked = inflate_raw(payload, compressed_size, uncompressed_size);
            out_ptr = unpacked.data();
            out_size = unpacked.size();
        } else {
            throw std::runtime_error(
                "Неподдерживаемый метод сжатия ZIP: " + std::to_string(method));
        }
        write_file(destination / name, out_ptr, out_size);
    }
}

void write_store_zip(
    const std::filesystem::path& zip_path,
    const std::vector<std::pair<std::string, std::filesystem::path>>&
        named_files) {
    std::vector<unsigned char> local;
    std::vector<unsigned char> central;
    std::uint16_t entries = 0;
    for (const auto& [name, source] : named_files) {
        const auto body = read_all(source);
        const auto crc = crc32_of(body.data(), body.size());
        const auto offset = static_cast<std::uint32_t>(local.size());
        append_u32(local, kLocalSig);
        append_u16(local, 20);
        append_u16(local, 0);
        append_u16(local, 0);
        append_u16(local, 0);
        append_u16(local, 0);
        append_u32(local, crc);
        append_u32(local, static_cast<std::uint32_t>(body.size()));
        append_u32(local, static_cast<std::uint32_t>(body.size()));
        append_u16(local, static_cast<std::uint16_t>(name.size()));
        append_u16(local, 0);
        local.insert(local.end(), name.begin(), name.end());
        local.insert(local.end(), body.begin(), body.end());

        append_u32(central, kCentralSig);
        append_u16(central, 20);
        append_u16(central, 20);
        append_u16(central, 0);
        append_u16(central, 0);
        append_u16(central, 0);
        append_u16(central, 0);
        append_u32(central, crc);
        append_u32(central, static_cast<std::uint32_t>(body.size()));
        append_u32(central, static_cast<std::uint32_t>(body.size()));
        append_u16(central, static_cast<std::uint16_t>(name.size()));
        append_u16(central, 0);
        append_u16(central, 0);
        append_u16(central, 0);
        append_u16(central, 0);
        append_u32(central, 0);
        append_u32(central, offset);
        central.insert(central.end(), name.begin(), name.end());
        ++entries;
    }

    std::vector<unsigned char> zip = std::move(local);
    const auto central_offset = static_cast<std::uint32_t>(zip.size());
    zip.insert(zip.end(), central.begin(), central.end());
    append_u32(zip, kEocdSig);
    append_u16(zip, 0);
    append_u16(zip, 0);
    append_u16(zip, entries);
    append_u16(zip, entries);
    append_u32(zip, static_cast<std::uint32_t>(central.size()));
    append_u32(zip, central_offset);
    append_u16(zip, 0);

    fs_utils::write_bytes(zip_path, zip.data(), zip.size());
}

}  // пространство имён offline_translator
