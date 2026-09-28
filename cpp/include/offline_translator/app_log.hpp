#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace offline_translator {

// Журнал работы: data/app.log (портативно) или явный путь.
// Пока init_app_log не вызван, записи игнорируются.
void init_app_log(const std::filesystem::path& path = {});
void shutdown_app_log();
void app_log(std::string_view level, std::string_view message);
void app_log_info(std::string_view message);
void app_log_warn(std::string_view message);
void app_log_error(std::string_view message);
std::filesystem::path app_log_path();
bool app_log_enabled();

}  // пространство имён offline_translator
