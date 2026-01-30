//
// Created by X on 2025/12/2.
//
#include "Logger.h"

#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>

const char* Logger::level_to_string(LogLevel level)
{
    switch (level)
    {
    case LogLevel::DEBUG: return "DEBUG";
    case LogLevel::INFO: return "INFO ";
    case LogLevel::WARNING: return "WARN ";
    case LogLevel::ERROR: return "ERROR";
    case LogLevel::FATAL: return "FATAL";
    default: return "UNKNOWN";
    }
}

const char* Logger::level_to_color_code(LogLevel level)
{
    switch (level)
    {
    case LogLevel::DEBUG: return "\033[36m"; // Cyan
    case LogLevel::INFO: return "\033[32m"; // Green
    case LogLevel::WARNING: return "\033[33m"; // Yellow
    case LogLevel::ERROR: return "\033[31m"; // Red
    case LogLevel::FATAL: return "\033[35m"; // Magenta
    default: return "\033[0m";
    }
}

std::string Logger::get_timestamp(bool include_ms)
{
    auto now = std::chrono::system_clock::now();
    auto tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf;

#ifdef _WIN32
    localtime_s(&tm_buf, &tt);
#else
    localtime_r(&tt, &tm_buf);
#endif

    std::stringstream ss;
    ss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S");

    if (include_ms)
    {
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;
        ss << '.' << std::setfill('0') << std::setw(3) << ms.count();
    }

    return ss.str();
}

void Logger::log(LogLevel level, const std::string& message, const char* file, int line) const
{
    write_log(level, message, file, line);
}

void Logger::write_log(LogLevel level, const std::string& message, const char* file, int line) const
{
    //防御性检查
    if (level < _min_level.load(std::memory_order_relaxed))return;

    //局部配置快照 (使用 config_mutex)
    bool to_console, to_file, include_ms;
    {
        std::lock_guard<std::mutex> lock(_config_mutex);
        to_console = _config.log_to_console;
        to_file = _config.log_to_file;
        include_ms = _config.include_milliseconds;
    }

    //构建消息体 (无锁区域)
    std::stringstream ss;
    ss << "[" << get_timestamp(include_ms) << "][" << level_to_string(level) << "]";

    if (file && line >= 0)
    {
        const char* filename = strrchr(file, '/');
#ifdef _WIN32
        const char* backslash = strrchr(file, '\\');
        if (backslash && (!filename || backslash > filename))
            filename = backslash;
#endif
        ss << "[" << (filename ? filename + 1 : file) << ":" << line << "]";
    }

    ss << message << "\n";
    std::string log_entry = ss.str();

    //控制台原子输出：一次性拼接颜色代码后输出，防止高并发交织
    if (to_console)
    {
        std::string colored_entry;
        colored_entry.reserve(log_entry.size() + 32);
        colored_entry.append(level_to_color_code(level));
        colored_entry.append(log_entry);
        colored_entry.append("\033[0m");

        // 使用 std::cerr.write 保证更强的原子交付语义
        std::cerr.write(colored_entry.data(), static_cast<std::streamsize>(colored_entry.size()));
    }

    //文件输出 (使用独立的 file_mutex，避免写文件影响控制台性能)
    if (to_file)
    {
        std::lock_guard<std::mutex> lock(_file_mutex);
        if (_file_stream && _file_stream->is_open())
        {
            _file_stream->write(log_entry.data(), static_cast<std::streamsize>(log_entry.size()));
        }
    }
}

void Logger::set_config(const Config& config)
{
    std::lock_guard<std::mutex> lock(_config_mutex);
    _config = config;
    _min_level.store(config.min_level, std::memory_order_relaxed);

    std::lock_guard<std::mutex> file_lock(_file_mutex);
    if (_config.log_to_file && !_config.log_file_path.empty())
    {
        _file_stream = std::make_unique<std::ofstream>(_config.log_file_path, std::ios::app);
        if (!_file_stream->is_open())
        {
            std::cerr << "[Logger] Failed to open: " << _config.log_file_path << "\n";
            _config.log_to_file = false;
        }
    }
    else
    {
        _file_stream.reset();
    }
}

void Logger::set_min_level(LogLevel level)
{
    std::lock_guard<std::mutex> lock(_config_mutex);
    _config.min_level = level;
    _min_level.store(level, std::memory_order_relaxed);
}

Logger::Config Logger::get_config() const
{
    std::lock_guard<std::mutex> lock(_config_mutex);
    return _config;
}
