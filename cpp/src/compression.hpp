#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace offline_translator::compression {

// Распаковка gzip-потока (модели GitHub) и zstd-кадра (GCS/CDN Firefox).
// Возвращает false, если данные повреждены или не являются сжатым потоком.
bool gunzip(
    const std::uint8_t* data,
    std::size_t size,
    std::vector<std::uint8_t>& out);

bool zunstd(
    const std::uint8_t* data,
    std::size_t size,
    std::vector<std::uint8_t>& out);

}  // namespace offline_translator::compression
