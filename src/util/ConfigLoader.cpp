//
// Created by X on 2025/11/18.
//
#include "ConfigLoader.h"
#include "Logger.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <vector>
#include <stdexcept>

static std::string trim(const std::string& str)
{
    size_t first = str.find_first_not_of(" \t\n\r");
    if (std::string::npos == first)
    {
        return "";
    }

    size_t last = str.find_last_not_of(" \t\n\r");

    return str.substr(first, (last - first + 1));
}

ConfigLoader& ConfigLoader::instance()
{
    static ConfigLoader instance;
    return instance;
}

ConfigLoader::ConfigLoader() = default;

void ConfigLoader::parseFile(const std::string& filename)
{
    std::ifstream file(filename);
    if (! file.is_open())
    {
        throw std::runtime_error("ConfigLoader: Could not open configuration file: " + filename);
    }

    std::string line;
    while (std::getline(file, line))
    {
        line = trim(line);
        if (line.empty() || line[0] == '#')
        {
            continue;
        }

        size_t delimiterPos = line.find('=');
        if (delimiterPos != std::string::npos)
        {
            std::string key = trim(line.substr(0, delimiterPos));
            std::string value = trim(line.substr(delimiterPos + 1));

            if (value.size() >= 2 &&
                (value.front() == '"' && value.back() == '"') ||
                (value.front() == '\'' && value.back() == '\''))
            {
                value = value.substr(1, value.size() - 2);
            }

            if (! key.empty())
            {
                _configMap[key] = value;
            }
        }
    }
}

void ConfigLoader::loadEnvironmentVariables()
{
    const std::vector<std::string> critical_keys = {
        "DB_HOST", "DB_PORT", "DB_USER", "DB_PASS", "DB_NAME",
        "REDIS_HOST", "REDIS_PORT", "CACHE_TTL_SECONDS",
        "SERVER_PORT", "SERVER_ADDRESS", "SERVER_MAX_THREADS",
        "STREAM_SECRET_KEY"
    };

    for (const auto& key : critical_keys)
    {
        if (const char* env_value = std::getenv(key.c_str()))
        {
            _configMap[key] = env_value;
            LOG_INFO("ConfigLoader: Overriding '"+key+"' from OS Environment.");
        }
    }
}

void ConfigLoader::load(const std::string& ini_filename, const std::string& env_filename)
{
    _configMap.clear();

    //加载config.ini
    try
    {
        parseFile(ini_filename);
        LOG_INFO("ConfigLoader: Base configuration loaded from "+ini_filename);
    }
    catch (const std::exception& e)
    {
        LOG_FATAL("ConfigLoader FATAL Error: Could not load base INI file: "+std::string(e.what()));
        throw;
    }

    //加载.env
    try
    {
        parseFile(env_filename);
        LOG_INFO("ConfigLoader: Environment configuration loaded from "+env_filename);
    }
    catch (const std::exception& e)
    {
        LOG_WARN("ConfigLoader Warning: Could not load optional .env file: "+std::string(e.what()));
    }

    //读取OS环境变量
    loadEnvironmentVariables();

    LOG_INFO("ConfigLoader: Final configuration readiness check completed.");
}


bool ConfigLoader::has(const std::string& key) const
{
    return _configMap.count(key) > 0;
}

std::string ConfigLoader::getString(const std::string& key) const
{
    if (! has(key))
    {
        throw std::out_of_range("ConfigLoader: Missing configuration key: " + key);
    }

    return _configMap.at(key);
}

int ConfigLoader::getInt(const std::string& key) const
{
    return std::stoi(getString(key));
}

int ConfigLoader::getInt(const std::string& key, int defaultValue) const
{
    if (! has(key))
    {
        return defaultValue;
    }

    try
    {
        return std::stoi(getString(key));
    }
    catch (const std::exception& e)
    {
        LOG_WARN(
            "ConfigLoader Warning: Key '"+key+"' is not a valid integer: " +std::string(e.what())+
            ". Using default value.");
        return defaultValue;
    }
}