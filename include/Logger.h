//
// Created by X on 2025/12/2.
//

#ifndef STREAMGATE_LOGGER_H
#define STREAMGATE_LOGGER_H
#include <iostream>
#include <string>
#include <mutex>
#include <chrono>
#include <iomanip>
#include <sstream>

enum LogLevel
{
    INFO,
    DEBUG,
    WARNING,
    ERROR,
    FATAL
};

class Logger
{
public:
    static Logger& instance()
    {
        static Logger instance;
        return instance;
    }

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    void log(LogLevel level, const std::string& message);

private:
    Logger() = default;
    ~Logger() = default;

    std::mutex log_mutex_;

    static std::string level_to_string(LogLevel level) ;

    static std::string get_timestamp() ;
};

#define LOG_INFO(msg) Logger::instance().log(INFO,msg)
#define LOG_DEBUG(msg)   Logger::instance().log(DEBUG, msg)
#define LOG_WARN(msg)    Logger::instance().log(WARNING, msg)
#define LOG_ERROR(msg)   Logger::instance().log(ERROR, msg)
#define LOG_FATAL(msg)   Logger::instance().log(FATAL, msg)
#endif //STREAMGATE_LOGGER_H