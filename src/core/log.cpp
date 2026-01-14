#include "core/log.hpp"
#include <iostream>
#include <iomanip>
#include <ctime>
#include <mutex>

static LogLevel g_log_level = LogLevel::Info;
static std::mutex g_log_mutex;

void Logger::set_level(LogLevel level) {
    g_log_level = level;
}

LogLevel Logger::get_level() {
    return g_log_level;
}

static std::string level_to_string(LogLevel level) {
    switch (level) {
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info: return "INFO ";
        case LogLevel::Warn: return "WARN ";
        case LogLevel::Error: return "ERROR";
    }
    return "UNKN ";
}

static void log(LogLevel level, std::string_view msg) {
    if (level < g_log_level) return;
    
    auto now = std::time(nullptr);
    auto tm = *std::localtime(&now);
    
    std::lock_guard<std::mutex> lock(g_log_mutex);
    std::cout << std::put_time(&tm, "%Y-%m-%d %H:%M:%S")
              << " [" << level_to_string(level) << "] "
              << msg << std::endl;
}

void Logger::debug(std::string_view msg) {
    log(LogLevel::Debug, msg);
}

void Logger::info(std::string_view msg) {
    log(LogLevel::Info, msg);
}

void Logger::warn(std::string_view msg) {
    log(LogLevel::Warn, msg);
}

void Logger::error(std::string_view msg) {
    log(LogLevel::Error, msg);
}
