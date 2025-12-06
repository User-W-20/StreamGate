//
// Created by X on 2025/12/2.
//
#include "Logger.h"

std::string Logger::level_to_string(LogLevel level)
{
    switch (level)
    {
        case INFO:
            return "INFO";
        case DEBUG:
            return "DEBUG";
        case WARNING:
            return "WARNING";
        case ERROR:
            return "ERROR";
        case FATAL:
            return "FATAL";
        default:
            return "UNKNOWN";
    }
}

std::string Logger::get_timestamp()
{
    auto now = std::chrono::system_clock::now();
    auto tt = std::chrono::system_clock::to_time_t(now);

    std::tm tm = *std::localtime(&tt);
    std::stringstream ss;
    ss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");

    return ss.str();
}

void Logger::log(LogLevel level, const std::string& message)
{
    std::lock_guard<std::mutex> lock(log_mutex_);

    std::cerr << "[" << get_timestamp() << "]";
    std::cerr << "[" << level_to_string(level) << "]";
    std::cerr << message << std::endl;
}