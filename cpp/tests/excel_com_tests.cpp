#include "excel_com.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <iostream>
#include <string>

namespace {

std::string to_utf8(const std::wstring& text) {
    if (text.empty()) {
        return {};
    }
    const int size = WideCharToMultiByte(
        CP_UTF8,
        0,
        text.data(),
        static_cast<int>(text.size()),
        nullptr,
        0,
        nullptr,
        nullptr);
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(
        CP_UTF8,
        0,
        text.data(),
        static_cast<int>(text.size()),
        result.data(),
        size,
        nullptr,
        nullptr);
    return result;
}

}  // анонимное пространство имён

int main() {
    std::wstring message;
    const auto result = tling_excel::excel_com_self_test(&message);
    if (result == tling_excel::ExcelSelfTestResult::passed) {
        std::cout << "excel_com_tests: ok - " << to_utf8(message) << "\n";
        return 0;
    }
    if (result == tling_excel::ExcelSelfTestResult::skipped) {
        std::cout << "excel_com_tests: skip - " << to_utf8(message) << "\n";
        return 0;
    }
    std::cout << "excel_com_tests: FAIL - " << to_utf8(message) << "\n";
    return 1;
}
