#include "zip_archive.hpp"
#include "fs_utils.hpp"

#include <zlib.h>

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

// Распаковка сырого DEFLATE-потока (метод ZIP 8) средствами zlib.
// Раньше здесь был собственный декодер Хаффмана, который не поддерживал
// часть корректных потоков (например, неполные деревья кодов), из-за чего
// установка реальных пакетов Argos падала с «Неизвестный символ Huffman».
std::vector<unsigned char> inflate_raw(
    const unsigned char* data,
    std::size_t size,
    std::size_t uncompressed_size) {
    if (size > 0xFFFFFFFFULL) {
        throw std::runtime_error("DEFLATE-поток слишком большой для распаковки");
    }
    z_stream stream{};
    if (inflateInit2(&stream, -15) != Z_OK) {
        throw std::runtime_error("Не удалось запустить распаковку DEFLATE");
    }
    std::vector<unsigned char> out;
    out.reserve(uncompressed_size > 0 ? uncompressed_size : size * 4);
    stream.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(data));
    stream.avail_in = static_cast<uInt>(size);
    std::array<unsigned char, 65536> buffer{};
    int status = Z_OK;
    do {
        stream.next_out = reinterpret_cast<Bytef*>(buffer.data());
        stream.avail_out = static_cast<uInt>(buffer.size());
        status = inflate(&stream, Z_NO_FLUSH);
        if (status != Z_OK && status != Z_STREAM_END && status != Z_BUF_ERROR) {
            const std::string detail =
                stream.msg != nullptr ? stream.msg : "повреждённый поток";
            inflateEnd(&stream);
            throw std::runtime_error("Ошибка распаковки DEFLATE: " + detail);
        }
        out.insert(
            out.end(),
            buffer.begin(),
            buffer.begin() + static_cast<std::ptrdiff_t>(
                                  buffer.size() - stream.avail_out));
        if (status == Z_BUF_ERROR && stream.avail_in == 0) {
            break;
        }
    } while (status != Z_STREAM_END);
    inflateEnd(&stream);
    if (status != Z_STREAM_END) {
        throw std::runtime_error("Обрыв DEFLATE-потока");
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
