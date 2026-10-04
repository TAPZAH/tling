#include "compression.hpp"

#include <zconf.h>
#include <zlib.h>
#include <zstd.h>

#include <array>
#include <stdexcept>
#include <string>
#include <vector>

namespace offline_translator::compression {
namespace {

constexpr std::size_t kMinOutputReserve = 64 * 1024;

}  // namespace

bool gunzip(
    const std::uint8_t* data,
    std::size_t size,
    std::vector<std::uint8_t>& out) {
    if (!data || size == 0) {
        return false;
    }
    z_stream stream{};
    // windowBits + 32 → автоматическое определение gzip/zlib, только inflate.
    if (inflateInit2(&stream, 15 + 32) != Z_OK) {
        return false;
    }
    out.clear();
    out.reserve(size < kMinOutputReserve ? kMinOutputReserve : size * 4);
    stream.next_in = const_cast<Bytef*>(data);
    stream.avail_in = static_cast<uInt>(size);
    int status = Z_OK;
    std::array<std::uint8_t, 65536> buffer{};
    do {
        stream.next_out = buffer.data();
        stream.avail_out = static_cast<uInt>(buffer.size());
        status = inflate(&stream, Z_NO_FLUSH);
        if (status != Z_OK && status != Z_STREAM_END && status != Z_BUF_ERROR) {
            inflateEnd(&stream);
            return false;
        }
        out.insert(
            out.end(),
            buffer.begin(),
            buffer.begin() + (buffer.size() - stream.avail_out));
        if (status == Z_BUF_ERROR && stream.avail_in == 0) {
            break;
        }
    } while (status != Z_STREAM_END);
    inflateEnd(&stream);
    return status == Z_STREAM_END;
}

bool zunstd(
    const std::uint8_t* data,
    std::size_t size,
    std::vector<std::uint8_t>& out) {
    if (!data || size == 0) {
        return false;
    }
    const unsigned long long hint = ZSTD_getFrameContentSize(data, size);
    if (hint == ZSTD_CONTENTSIZE_ERROR ||
        hint == ZSTD_CONTENTSIZE_UNKNOWN) {
        // Кадр без размера в заголовке: распаковываем потоково.
        ZSTD_DStream* stream = ZSTD_createDStream();
        if (!stream) {
            return false;
        }
        ZSTD_initDStream(stream);
        out.clear();
        std::vector<std::uint8_t> buffer(ZSTD_DStreamOutSize());
        ZSTD_inBuffer input{data, size, 0};
        std::size_t status = 1;
        while (status != 0 || input.pos < input.size) {
            if (status != 0 && input.pos == input.size) {
                // Данные закончились, а кадр не закрыт: поток оборван.
                ZSTD_freeDStream(stream);
                return false;
            }
            ZSTD_outBuffer output{buffer.data(), buffer.size(), 0};
            status = ZSTD_decompressStream(stream, &output, &input);
            if (ZSTD_isError(status)) {
                ZSTD_freeDStream(stream);
                return false;
            }
            out.insert(out.end(), buffer.begin(), buffer.begin() + output.pos);
        }
        ZSTD_freeDStream(stream);
        return !out.empty();
    }
    if (hint > (1ULL << 31)) {
        return false;
    }
    out.resize(static_cast<std::size_t>(hint));
    const std::size_t result = ZSTD_decompress(
        out.data(), out.size(), data, size);
    if (ZSTD_isError(result) || result != hint) {
        return false;
    }
    return true;
}

}  // namespace offline_translator::compression
