#include "offline_translator/paddle_ocr.hpp"

#include "image_file.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <gdiplus.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: paddle_ocr_smoke <models_dir> <image> "
                     "[lang] [out.txt]\n";
        return 2;
    }
    const std::filesystem::path models_dir = argv[1];
    const std::filesystem::path image_path = argv[2];
    const std::string lang = argc >= 4 ? argv[3] : "en";
    const std::filesystem::path out_path =
        argc >= 5 ? argv[4] : "paddle_ocr_smoke.out.txt";

    Gdiplus::GdiplusStartupInput gdiplus_input;
    ULONG_PTR gdiplus_token = 0;
    Gdiplus::GdiplusStartup(&gdiplus_token, &gdiplus_input, nullptr);

    auto image = offline_translator::load_image_file(image_path);
    if (!image) {
        std::cerr << "не удалось открыть изображение\n";
        Gdiplus::GdiplusShutdown(gdiplus_token);
        return 1;
    }
    std::printf("image %dx%d\n", image->width, image->height);

    try {
        offline_translator::PaddleOcrEngine engine(models_dir);
        std::vector<std::string> languages;
        {
            std::string spec = lang;
            std::size_t start = 0;
            while (true) {
                const std::size_t comma = spec.find(',', start);
                languages.push_back(spec.substr(
                    start,
                    comma == std::string::npos ? std::string::npos
                                               : comma - start));
                if (comma == std::string::npos) {
                    break;
                }
                start = comma + 1;
            }
        }
        engine.initialize_multi(languages);
        int rotation = 0;
        const auto layout = engine.recognize_layout_oriented(
            image->bgra.data(),
            image->width,
            image->height,
            image->width * 4,
            4,
            &rotation);
        std::printf(
            "rotation=%d words=%zu\n", rotation, layout.words.size());

        std::ofstream out(out_path, std::ios::binary);
        for (const auto& word : layout.words) {
            out << word.line << '\t' << word.left << '\t' << word.top << '\t'
                << word.width << '\t' << word.height << '\t'
                << static_cast<int>(word.confidence) << '\t' << word.text
                << '\n';
        }
        std::puts(out_path.string().c_str());
    } catch (const std::exception& error) {
        std::cerr << "ошибка: " << error.what() << '\n';
        Gdiplus::GdiplusShutdown(gdiplus_token);
        return 1;
    }
    Gdiplus::GdiplusShutdown(gdiplus_token);
    return 0;
}
