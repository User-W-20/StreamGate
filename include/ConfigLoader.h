//
// Created by X on 2025/11/18.
//

#ifndef STREAMGATE_CONFIGLOADER_H
#define STREAMGATE_CONFIGLOADER_H
#include "Poco/Util/AbstractConfiguration.h"
#include <string>
#include "Poco/AutoPtr.h"
#include "Poco/Data/SessionPool.h"

class ConfigLoader
{
public:
    static ConfigLoader& instance();

    [[nodiscard]] std::string getString(const std::string& key) const;

    [[nodiscard]] int getInt(const std::string& key) const;

    [[nodiscard]] std::string getDBConnectionString() const;

    ConfigLoader(const ConfigLoader&) = delete;
    ConfigLoader& operator=(const ConfigLoader&) = delete;

    [[nodiscard]] bool has(const std::string& key) const;

private:
    ConfigLoader();
    ~ConfigLoader() = default;

private:
   Poco::AutoPtr<Poco::Util::AbstractConfiguration> _pConfig;

    const std::string CONFIG_FILE = ".env";
};
#endif //STREAMGATE_CONFIGLOADER_H