#pragma once
#include <string>
#include <string_view>
#include <sstream>

enum class LogLevel {
    Debug,
    Info,
    Warn,
    Error
};

class Logger {
public:
    static void set_level(LogLevel level);
    static LogLevel get_level();
    
    static void debug(std::string_view msg);
    static void info(std::string_view msg);
    static void warn(std::string_view msg);
    static void error(std::string_view msg);
    
    template<typename... Args>
    static void debug(Args&&... args) {
        if (get_level() <= LogLevel::Debug) {
            std::ostringstream oss;
            (oss << ... << args);
            debug(std::string_view(oss.str()));
        }
    }
    
    template<typename... Args>
    static void info(Args&&... args) {
        if (get_level() <= LogLevel::Info) {
            std::ostringstream oss;
            (oss << ... << args);
            info(std::string_view(oss.str()));
        }
    }
    
    template<typename... Args>
    static void warn(Args&&... args) {
        if (get_level() <= LogLevel::Warn) {
            std::ostringstream oss;
            (oss << ... << args);
            warn(std::string_view(oss.str()));
        }
    }
    
    template<typename... Args>
    static void error(Args&&... args) {
        std::ostringstream oss;
        (oss << ... << args);
        error(std::string_view(oss.str()));
    }
};
