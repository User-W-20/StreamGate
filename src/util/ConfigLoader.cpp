//
// Created by X on 2025/11/18.
//
#include "ConfigLoader.h"

#include <algorithm>

#include "Logger.h"
#include <fstream>
#include <ranges>
#include <utility>
#include <vector>

std::string ConfigLoader::trim(const std::string& str)
{
    const size_t first = str.find_first_not_of(" \t\n\r");
    if (first == std::string::npos)return "";
    const size_t last = str.find_last_not_of(" \t\n\r");
    return str.substr(first, (last - first + 1));
}

std::string ConfigLoader::unquote(const std::string& str)
{
    if (str.size() >= 2 &&
        ((str.front() == '"' && str.back() == '"') ||
            (str.front() == '\'' && str.back() == '\'')))
    {
        return str.substr(1, str.size() - 2);
    }

    return str;;
}

bool ConfigLoader::parseBool(const std::string& str)
{
    std::string lower = str;
    std::ranges::transform(lower, lower.begin(),
                           [](unsigned char c)
                           {
                               return std::tolower(c);
                           });

    if (lower == "true" || lower == "1" || lower == "yes" || lower == "on")return true;
    if (lower == "false" || lower == "0" || lower == "no" || lower == "off") return false;

    throw ConfigException("Invalid boolean value: " + str);
}

const std::vector<std::string>& ConfigLoader::getDefaultEnvKeys()
{
    static const std::vector<std::string> keys = {
        "DB_HOST", "DB_PORT", "DB_USER", "DB_PASS", "DB_NAME",
        "REDIS_HOST", "REDIS_PORT", "CACHE_TTL_SECONDS",
        "SERVER_PORT", "SERVER_ADDRESS", "SERVER_MAX_THREADS",
        "STREAM_SECRET_KEY"
    };
    return keys;
}

ConfigLoader::ConfigLoader()
    : _mutex(),
      _configMap(),
      _last_ini_file{},
      _last_env_file{},
      _last_options(),
      _validators()
{
}

//核心加载逻辑 (Strong Exception Guarantee)
bool ConfigLoader::load(const std::string& ini_filename, const std::string& env_filename, const LoadOptions& options)
{
    try
    {
        //创建临时容器，确保解析失败时不破坏当前内存中的配置
        std::map<std::string, std::string> nextConfig;
        // 解析 INI 文件
        parseFileToMap(ini_filename, nextConfig, options.allow_missing_ini);

        //解析 ENV 文件
        if (!env_filename.empty())
        {
            parseFileToMap(env_filename, nextConfig, options.allow_missing_env);
        }

        //覆盖环境变量
        if (options.override_from_environment)
        {
            const auto& keys = options.env_keys_to_load.empty() ? getDefaultEnvKeys() : options.env_keys_to_load;
            loadEnvToMap(keys, nextConfig);
        }

        //运行验证器 (针对临时容器进行校验)
        if (!validateMap(nextConfig))
        {
            LOG_ERROR("ConfigLoader: Configuration validation failed.");
            return false;
        }

        //只有所有步骤都成功，才加锁进行原子交换
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _configMap.swap(nextConfig); // O(1) 线程安全切换
            _last_ini_file = ini_filename;
            _last_env_file = env_filename;
            _last_options = options;
        }

        LOG_INFO("ConfigLoader: Configuration loaded and verified successfully.");
        return true;
    }
    catch (const std::exception& e)
    {
        LOG_ERROR("ConfigLoader: Load failed! Error: " + std::string(e.what()));
        return false;
    }
}

void ConfigLoader::reload()
{
    LoadOptions currentOptions;
    std::string ini, env;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        ini = _last_ini_file;
        env = _last_env_file;
        currentOptions = _last_options;
    }

    load(ini, env, currentOptions);
}

void ConfigLoader::parseFileToMap(const std::string& filename, std::map<std::string, std::string>& targetMap,
                                  bool optional)
{
    std::ifstream file(filename);
    if (!file.is_open())
    {
        if (optional)return;
        throw ConfigException("Cannot open config file: " + filename);
    }

    std::string line;
    while (std::getline(file, line))
    {
        line = trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';')continue;

        if (size_t sep = line.find('='); sep != std::string::npos)
        {
            std::string key = trim(line.substr(0, sep));
            std::string val = unquote(line.substr(sep + 1));
            if (!key.empty())
                targetMap[key] = val;
        }
    }
}

void ConfigLoader::loadEnvToMap(const std::vector<std::string>& keys, std::map<std::string, std::string>& targetMap)
{
    for (const auto& key : keys)
    {
        if (const char* env_val = std::getenv(key.c_str()))
        {
            targetMap[key] = env_val;
            LOG_INFO("ConfigLoader: Overridden '" + key + "' from environment.");
        }
    }
}

bool ConfigLoader::validateMap(const std::map<std::string, std::string>& targetMap) const
{
    std::lock_guard<std::mutex> lock(_mutex);

    // std::ranges::all_of 返回 bool，当所有谓词都为 true 时返回 true
    return std::ranges::all_of(_validators, [&targetMap](const auto& pair)
    {
        const auto& [key,info] = pair;
        auto it = targetMap.find(key);

        if (it == targetMap.end())
        {
            LOG_ERROR("ConfigLoader: Required key missing: " + key);
            return false;
        }

        if (!info.func(it->second))
        {
            LOG_ERROR("ConfigLoader: Validation failed for '" + key + "': " + info.error_message);
            return false;
        }

        return true;
    });
}

//类型安全 Getters (带全量互斥保护)
std::string ConfigLoader::getString(const std::string& key) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    const auto it = _configMap.find(key);
    if (it == _configMap.end())
        throw ConfigException("Missing required key: " + key);
    return it->second;
}

std::string ConfigLoader::getString(const std::string& key, const std::string& default_value) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    const auto it = _configMap.find(key);
    return (it != _configMap.end()) ? it->second : default_value;
}

int ConfigLoader::getInt(const std::string& key) const
{
    const std::string val = getString(key);
    return std::stoi(val);
}

int ConfigLoader::getInt(const std::string& key, int default_value) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    const auto it = _configMap.find(key);
    if (it == _configMap.end())
        return default_value;

    try
    {
        return std::stoi(it->second);
    }
    catch (...)
    {
        return default_value;
    }
}

bool ConfigLoader::getBool(const std::string& key) const
{
    return parseBool(getString(key));
}

bool ConfigLoader::getBool(const std::string& key, bool default_value) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    const auto it = _configMap.find(key);
    if (it == _configMap.end())
        return default_value;

    try
    {
        return parseBool(it->second);
    }
    catch (...)
    {
        return default_value;
    }
}

double ConfigLoader::getDouble(const std::string& key) const
{
    return std::stod(getString(key));
}

double ConfigLoader::getDouble(const std::string& key, double default_value) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    const auto it = _configMap.find(key);
    if (it == _configMap.end())
        return default_value;

    try
    {
        return std::stod(it->second);
    }
    catch (...)
    {
        return default_value;
    }
}

bool ConfigLoader::has(const std::string& key) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _configMap.contains(key);
}

void ConfigLoader::set(const std::string& key, const std::string& value)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _configMap[key] = value;
}

void ConfigLoader::addValidator(const std::string& key, Validator validator, const std::string& err_msg)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _validators[key] = {std::move(validator), err_msg};
}
