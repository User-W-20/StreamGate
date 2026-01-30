//
// Created by X on 2025/12/2.
//

#ifndef STREAMGATE_LOGGER_H
#define STREAMGATE_LOGGER_H
#include <string>
#include <mutex>
#include <atomic>
#include <fstream>
#include <bits/shared_ptr_atomic.h>

enum class LogLevel
{
    DEBUG = 0,
    INFO,
    WARNING,
    ERROR,
    FATAL
};

class Logger
{
public:
    struct Config
    {
        LogLevel min_level = LogLevel::INFO;
        bool log_to_console = true;
        bool log_to_file = false;
        std::string log_file_path;
        bool include_milliseconds = true;
    };

    /**
     * @brief 泄露单例模式：确保 Logger 在进程析构博弈中始终存活
     * 由 OS 在进程结束时统一回收，避免 static 对象析构顺序导致的崩溃
     */
    static Logger& instance()
    {
        static auto* instance = new Logger();
        return *instance;
    }

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    // Fast-path: 宏调用的原子级别检查
    LogLevel get_min_level() const
    {
        return _min_level.load(std::memory_order_relaxed);
    }

    void log(LogLevel level, const std::string& message, const char* file = nullptr, int line = -1) const;

    void set_config(const Config& config);
    void set_min_level(LogLevel level);
    Config get_config() const;

private:
    Logger()
    {
        _min_level.store(LogLevel::INFO);

        std::lock_guard<std::mutex> lock1(_config_mutex);
        std::lock_guard<std::mutex> lock2(_file_mutex);

        _config = Config{};
        _file_stream = nullptr;
    }

    ~Logger() = default;

    static const char* level_to_string(LogLevel level);
    static const char* level_to_color_code(LogLevel level);
    static std::string get_timestamp(bool include_ms);

    void write_log(LogLevel level, const std::string& message, const char* file, int line) const;

    // 细粒度锁设计
    mutable std::mutex _config_mutex; // 保护配置信息
    mutable std::mutex _file_mutex; // 独立保护文件IO，避免文件卡顿影响全局

    Config _config;
    std::unique_ptr<std::ofstream> _file_stream;
    std::atomic<LogLevel> _min_level;
};

//安全的日志宏定义：do-while(0) 结构 + 原子 Fast-Path
#define LOG_BASE(level,msg,file,line)do{\
    if(level>=Logger::instance().get_min_level()){\
        Logger::instance().log(level,msg,file,line);\
    }\
}while (0)

#define LOG_DEBUG(msg) LOG_BASE(LogLevel::DEBUG, msg, nullptr, -1)
#define LOG_INFO(msg)  LOG_BASE(LogLevel::INFO,  msg, nullptr, -1)
#define LOG_WARN(msg)  LOG_BASE(LogLevel::WARNING, msg, nullptr, -1)
#define LOG_ERROR(msg) LOG_BASE(LogLevel::ERROR, msg, nullptr, -1)
#define LOG_FATAL(msg) LOG_BASE(LogLevel::FATAL, msg, nullptr, -1)

#define LOG_DEBUG_LOC(msg) LOG_BASE(LogLevel::DEBUG, msg, __FILE__, __LINE__)
#define LOG_INFO_LOC(msg)  LOG_BASE(LogLevel::INFO,  msg, __FILE__, __LINE__)
#define LOG_WARN_LOC(msg)  LOG_BASE(LogLevel::WARNING, msg, __FILE__, __LINE__)
#define LOG_ERROR_LOC(msg) LOG_BASE(LogLevel::ERROR, msg, __FILE__, __LINE__)
#define LOG_FATAL_LOC(msg) LOG_BASE(LogLevel::FATAL, msg, __FILE__, __LINE__)
#endif //STREAMGATE_LOGGER_H
