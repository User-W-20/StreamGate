//
// Created by X on 2025/11/18.
//
#include "ConfigLoader.h"
#include "Poco/Util/AbstractConfiguration.h"
#include "Poco/Exception.h"
#include "Poco/Util/PropertyFileConfiguration.h"
#include "Poco/Format.h"
#include <iostream>

using namespace Poco::Util;

ConfigLoader& ConfigLoader::instance()
{
    static ConfigLoader instance;
    return instance;
}

ConfigLoader::ConfigLoader()
{
    try
    {
        _pConfig = new PropertyFileConfiguration(CONFIG_FILE);

        std::cout << "[ConfigLoader] Configuration loaded successfully from " << CONFIG_FILE << std::endl;
    }
    catch (const Poco::FileNotFoundException& e)
    {
        std::cerr << "[ConfigLoader ERROR] Configuration file '" << CONFIG_FILE << "' not found." << std::endl;
        std::cerr << "Please ensure a local .env file exists and contains all required keys." << std::endl;
    }
    catch (const Poco::Exception& e)
    {
        std::cerr << "[ConfigLoader ERROR] Failed to load configuration: " << e.displayText() << std::endl;
        throw;
    }
}

std::string ConfigLoader::getString(const std::string& key) const
{
    return _pConfig->getString(key);
}

int ConfigLoader::getInt(const std::string& key) const
{
    return _pConfig->getInt(key);
}

std::string ConfigLoader::getDBConnectionString() const
{
    std::string port_str = std::to_string(getInt("DB_PORT"));

    std::string connStr = Poco::format(
        "host=%s;user=%s;password=%s;db=%s;port=%s",
        getString("DB_HOST"),
        getString("DB_USER"),
        getString("DB_PASS"),
        getString("DB_NAME"),
        port_str
        );

    return connStr;
}

bool ConfigLoader::has(const std::string& key) const
{
    return _pConfig->has(key);
}